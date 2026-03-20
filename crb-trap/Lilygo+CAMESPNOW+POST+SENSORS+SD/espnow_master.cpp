/**
 * @file      espnow_master.cpp
 * @brief     ESP-NOW master-side communication implementation.
 *            WiFi AP management, ESP-NOW init/cleanup, slave pairing,
 *            photo command TX, and full photo RX with CRC32 verification.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "espnow_master.h"
#include "espnow_protocol.h"
#include "http_upload.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ==================== STATE VARIABLES ====================

volatile bool camReady = false;
volatile bool photoStartReceived = false;
volatile bool photoEndReceived = false;
volatile bool slaveError = false;
volatile uint8_t slaveErrorCode = 0;
volatile bool transferAborted = false;  // NEW: Abort flag for sequencing errors
volatile uint16_t lastAckedPacket = 0;  // NEW: Track last acked packet
volatile bool brownoutDetected = false; // NEW: Detect voltage sag during transfer

// ==================== DOUBLE BUFFER (SCRATCHPAD) ====================
// Local scratchpad in internal RAM to avoid PSRAM latency glitch
// When a new ESP-NOW packet arrives during memcpy to PSRAM, the DMA buffer can get
// corrupted ("Frankenstein" packet). Solution: copy to internal RAM scratchpad first,
// then move from scratchpad to PSRAM safely.
// INCREASED to 1500 bytes to support future ESP-NOW v2.0 (1470 byte packets)
static uint8_t scratchpad[1500];

uint8_t *photoBuffer = nullptr;
volatile uint32_t photoSize = 0;
volatile uint16_t totalPacketsExpected = 0;
volatile uint16_t packetsReceived = 0;
volatile uint32_t receivedCRC32 = 0;
volatile uint32_t lastPacketTime = 0;

uint8_t slaveMac[6] = {0};
bool slavePaired = false;

uint16_t capturedLuxValue = 0;
bool capturedIsFallen = false;

// ==================== INTERNAL CALLBACKS ====================

// Helper: Send PKT_NEXT acknowledgement to slave with exponential backoff retry
// Ensures ACK delivery before slave timeout
// CRITICAL: No Serial logging in this callback — it runs in interrupt context and stalls the CPU
static void sendNextSignal(uint16_t ackCounter) {
  uint8_t nextPacket[3];
  nextPacket[0] = PKT_NEXT;
  nextPacket[1] = (uint8_t)(ackCounter & 0xFF);
  nextPacket[2] = (uint8_t)((ackCounter >> 8) & 0xFF);

#define NEXT_SIGNAL_MAX_RETRIES 3     // Max attempts to send PKT_NEXT
#define NEXT_SIGNAL_RETRY_DELAY_MS 20 // Exponential backoff base delay

  for (uint8_t attempt = 0; attempt < NEXT_SIGNAL_MAX_RETRIES; attempt++) {
    esp_err_t result = esp_now_send(slaveMac, nextPacket, sizeof(nextPacket));
    if (result == ESP_OK) {
      // Success — return immediately (no Serial logging to avoid CPU stall)
      return;
    } else {
      // Send failed, retry with exponential backoff
      if (attempt < NEXT_SIGNAL_MAX_RETRIES - 1) {
        uint32_t delayMs = NEXT_SIGNAL_RETRY_DELAY_MS * (attempt + 1);
        delay(delayMs);
        // No Serial logging here — we're in interrupt context
      }
    }
  }
  // Silent failure: slave will timeout and retry. Logging here only stalls the interrupt.
}

static void handleRecvData(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  if (data_len < 1)
    return;

  lastPacketTime = millis();
  uint8_t pktType = data[0];

  switch (pktType) {

  case PKT_READY:
    if (!slavePaired) {
      memcpy(slaveMac, mac_addr, 6);
      Serial.printf("[ESPNOW] Slave MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    slaveMac[0], slaveMac[1], slaveMac[2],
                    slaveMac[3], slaveMac[4], slaveMac[5]);
    }
    camReady = true;
    Serial.println("[ESPNOW] READY received from slave");
    break;

  case PKT_PHOTO_START:
    if (data_len >= 7) {
      photoSize = (uint32_t)data[1] |
                  ((uint32_t)data[2] << 8) |
                  ((uint32_t)data[3] << 16) |
                  ((uint32_t)data[4] << 24);
      totalPacketsExpected = (uint16_t)data[5] | ((uint16_t)data[6] << 8);

      Serial.printf("[ESPNOW] PHOTO_START: %u bytes, %u packets expected\n",
                    photoSize, totalPacketsExpected);

      if (photoBuffer) {
        free(photoBuffer);
        photoBuffer = nullptr;
      }
      if (photoSize > 0 && photoSize < 500000) {
        Serial.printf("[ESPNOW] Before buffer alloc: Free PSRAM: %u, Free heap: %u\n",
                      ESP.getFreePsram(), ESP.getFreeHeap());
        photoBuffer = (uint8_t *)malloc(photoSize);
        if (photoBuffer) {
          Serial.printf("[ESPNOW] After buffer alloc: Free PSRAM: %u, Free heap: %u\n",
                        ESP.getFreePsram(), ESP.getFreeHeap());
          memset(photoBuffer, 0, photoSize);
          packetsReceived = 0;
          lastAckedPacket = 0;     // NEW: Reset ack counter
          transferAborted = false; // NEW: Clear abort flag
          photoStartReceived = true;
          Serial.printf("[ESPNOW] Buffer allocated: %u bytes\n", photoSize);
        } else {
          Serial.printf("[ESPNOW] ERROR: malloc failed! Needed: %u, Free PSRAM: %u, Free heap: %u\n",
                        photoSize, ESP.getFreePsram(), ESP.getFreeHeap());
        }
      } else {
        Serial.printf("[ESPNOW] ERROR: Invalid photo size: %u\n", photoSize);
      }
    }
    break;

  case PKT_PHOTO_DATA:
    if (data_len >= 3 && photoBuffer && photoStartReceived && !transferAborted) {
      uint16_t packetNum = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
      uint16_t dataLen = (uint16_t)(data_len - 3);
      uint32_t offset = (uint32_t)packetNum * ESPNOW_DATA_SIZE;

      // Skip verbose per-packet logging; only log on error/progress milestones
      // Serial.printf("[ESPNOW] << PKT_DATA[%u] received (%u bytes)\n", packetNum, dataLen);

      // NEW: Strict sequencing validation
      if (packetNum != packetsReceived) {
        Serial.printf("[ESPNOW] ERROR: Sequence violation! Expected packet %u, got %u\n",
                      packetsReceived, packetNum);
        transferAborted = true;

        // Send error to slave
        uint8_t errPacket[2];
        errPacket[0] = PKT_ERROR;
        errPacket[1] = ERR_SEQ_ERROR;
        esp_now_send(slaveMac, errPacket, sizeof(errPacket));
        break;
      }

      // Validate buffer bounds
      if (offset + dataLen > photoSize) {
        Serial.printf("[ESPNOW] ERROR: Packet %u offset %u + %u exceeds size %u\n",
                      packetNum, offset, dataLen, photoSize);
        transferAborted = true;

        // Send error to slave
        uint8_t errPacket[2];
        errPacket[0] = PKT_ERROR;
        errPacket[1] = ERR_SEND_FAILED;
        esp_now_send(slaveMac, errPacket, sizeof(errPacket));
        break;
      }

      // Validate scratchpad doesn't overflow (1500 bytes allocated)
      if (dataLen > 1500) {
        Serial.printf("[ESPNOW] ERROR: Packet %u payload %u exceeds SCRATCHPAD size\n",
                      packetNum, dataLen);
        transferAborted = true;
        break;
      }

      // ====== NEW: CRITICAL ATOMIC SECTION ======
      // Disable interrupts during PSRAM writes to prevent:
      // - WiFi ISR preempting mid-memcpy
      // - Camera DMA interfering with PSRAM writes
      // - Partial data corruption ("Frankenstein" packets)
      noInterrupts();

      // Step 1: Copy from ESP-NOW DMA buffer to scratchpad in internal RAM
      memcpy(scratchpad, data + 3, dataLen);

      // Step 2: Copy from scratchpad to PSRAM photoBuffer (now safe from interrupts)
      memcpy(photoBuffer + offset, scratchpad, dataLen);

      // NEW: Force cache flush to ensure PSRAM write completes immediately
      // Without this, PSRAM write-combining cache may delay actual write
      asm volatile("dsync"); // Data cache sync for ARM

      interrupts();
      // ====== END CRITICAL SECTION ======

      lastAckedPacket = packetNum;
      packetsReceived++;

      // Send handshake acknowledgement
      sendNextSignal(packetNum);
    } else if (transferAborted) {
      Serial.printf("[ESPNOW] Packet received but transfer aborted (packetNum error)\n");
    }
    break;

  case PKT_PHOTO_END:
    if (data_len >= 5) {
      receivedCRC32 = (uint32_t)data[1] |
                      ((uint32_t)data[2] << 8) |
                      ((uint32_t)data[3] << 16) |
                      ((uint32_t)data[4] << 24);
      photoEndReceived = true;
      Serial.printf("[ESPNOW] PHOTO_END: CRC32=%08X, received %u/%u packets\n",
                    receivedCRC32, packetsReceived, totalPacketsExpected);
    }
    break;

  case PKT_ERROR:
    if (data_len >= 2) {
      slaveErrorCode = data[1];
      slaveError = true;
      Serial.printf("[ESPNOW] ERROR from slave: code 0x%02X\n", slaveErrorCode);
    }
    break;

  default:
    Serial.printf("[ESPNOW] Unknown packet type: 0x%02X (len=%d)\n", pktType, data_len);
    break;
  }
}

static void espnowSendCb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("[ESPNOW] Send delivery failed");
  }
}

static void espnowRecvCb(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  handleRecvData(mac_addr, data, data_len);
}

// ==================== PUBLIC API ====================

void initESPNOW() {
  Serial.println("[WIFI] Starting AP mode for slave discovery...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(MASTER_AP_SSID, MASTER_AP_PASS, ESPNOW_CHANNEL);
  delay(100);

  // Force 802.11b mode for stable JPEG transfers
  // 802.11n is fast but sensitive to multipath interference; 802.11b is robust for binary data
  esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B);
  esp_wifi_set_ps(WIFI_PS_NONE); // Disable power save for consistent latency
  Serial.println("[WIFI] WiFi protocol forced to 802.11b, power save disabled");

  Serial.printf("[WIFI] AP started: SSID=%s, Channel=%d\n", MASTER_AP_SSID, ESPNOW_CHANNEL);
  Serial.printf("[WIFI] AP MAC: %s\n", WiFi.softAPmacAddress().c_str());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] FATAL: Init failed!");
    return;
  }
  Serial.println("[ESPNOW] Initialized");

  esp_now_register_recv_cb(espnowRecvCb);
  esp_now_register_send_cb(espnowSendCb);

  // Reset all reception state
  camReady = false;
  photoStartReceived = false;
  photoEndReceived = false;
  slaveError = false;
  slavePaired = false;
  packetsReceived = 0;
  lastAckedPacket = 0;     // NEW
  transferAborted = false; // NEW
  if (photoBuffer) {
    free(photoBuffer);
    photoBuffer = nullptr;
  }
}

void cleanupESPNOW() {
  Serial.println("[ESPNOW] Cleaning up...");
  esp_now_deinit();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  if (photoBuffer) {
    free(photoBuffer);
    photoBuffer = nullptr;
  }

  Serial.println("[ESPNOW] WiFi and ESP-NOW stopped");
}

bool waitForCameraReady() {
  camReady = false;
  uint32_t startTime = millis();

  Serial.println("[ESPNOW] Waiting for READY (polling flag)...");

  while (millis() - startTime < READY_TIMEOUT_MS) {
    if (camReady) {
      Serial.printf("[SUCCESS] READY received after %ldms\n", millis() - startTime);

      if (!slavePaired) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, slaveMac, 6);
        peerInfo.channel = ESPNOW_CHANNEL;
        peerInfo.ifidx = WIFI_IF_AP; // Master runs as AP — must use AP interface
        peerInfo.encrypt = false;

        if (esp_now_is_peer_exist(slaveMac)) {
          esp_now_del_peer(slaveMac);
        }

        esp_err_t addStatus = esp_now_add_peer(&peerInfo);
        if (addStatus == ESP_OK) {
          slavePaired = true;
          Serial.println("[ESPNOW] Slave added as peer");
        } else {
          Serial.printf("[ESPNOW] ERROR: Failed to add slave peer: 0x%x\n", addStatus);
          return false;
        }
      }

      return true;
    }
    delay(10);
  }

  Serial.printf("[TIMEOUT] No READY received after %dms\n", READY_TIMEOUT_MS);
  return false;
}

bool sendPhotoCommand(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality) {
  if (!slavePaired) {
    Serial.println("[ERROR] Slave not paired, cannot send command");
    return false;
  }

  // Packet layout: type(1) + lux(2) + width(2) + height(2) + quality(1) = 8 bytes
  uint8_t packet[8];
  packet[0] = PKT_PHOTO_CMD;
  packet[1] = (uint8_t)(lux & 0xFF);
  packet[2] = (uint8_t)(lux >> 8);
  packet[3] = (uint8_t)(width & 0xFF);
  packet[4] = (uint8_t)(width >> 8);
  packet[5] = (uint8_t)(height & 0xFF);
  packet[6] = (uint8_t)(height >> 8);
  packet[7] = quality;

  Serial.printf("[ESPNOW] Sending PHOTO_CMD: LUX=%d, %dx%d, Q=%d\n",
                lux, width, height, quality);

  esp_err_t result = esp_now_send(slaveMac, packet, sizeof(packet));
  if (result != ESP_OK) {
    Serial.printf("[ESPNOW] Send error: 0x%x\n", result);
    return false;
  }

  Serial.println("[ESPNOW] PHOTO_CMD sent");
  return true;
}

bool receivePhoto() {
  Serial.println("[PHOTO] Waiting for photo transmission...");

  photoStartReceived = false;
  photoEndReceived = false;
  slaveError = false;
  packetsReceived = 0;
  lastAckedPacket = 0;     // NEW
  transferAborted = false; // NEW
  lastPacketTime = millis();

  uint32_t startTime = millis();
  // v2.0: Dynamic timeout based on packet count (50ms per packet conservative estimate)
  uint32_t NO_DATA_TIMEOUT_MS = (totalPacketsExpected > 0) ? max((totalPacketsExpected * 50U * 2U), 5000U) : 15000U;

  // Phase 1: Wait for PHOTO_START
  Serial.println("[PHOTO] Waiting for PHOTO_START...");
  while (!photoStartReceived && !slaveError) {
    if (millis() - startTime > PHOTO_TIMEOUT_MS) {
      Serial.println("[ERROR] PHOTO_START timeout");
      return false;
    }
    delay(10);
  }

  if (slaveError) {
    Serial.printf("[ERROR] Slave reported error: 0x%02X\n", slaveErrorCode);
    return false;
  }
  if (!photoBuffer) {
    Serial.println("[ERROR] Photo buffer allocation failed");
    return false;
  }

  // Phase 2: Receive PHOTO_DATA + PHOTO_END
  Serial.println("[PHOTO] Receiving data packets...");
  while (!photoEndReceived && !slaveError && !transferAborted) { // NEW: Check abort
    if (millis() - startTime > PHOTO_TIMEOUT_MS) {
      Serial.printf("[ERROR] Photo reception absolute timeout (%ds)\n", PHOTO_TIMEOUT_MS / 1000);
      break;
    }
    if (millis() - lastPacketTime > NO_DATA_TIMEOUT_MS) {
      Serial.printf("[ERROR] No data for %ds, aborting\n", NO_DATA_TIMEOUT_MS / 1000);
      break;
    }
    delay(1);
  }

  // NEW: Check for sequencing errors
  if (transferAborted) {
    Serial.printf("[ERROR] Transfer aborted due to sequencing error at packet %u\n",
                  packetsReceived);
    free(photoBuffer);
    photoBuffer = nullptr;
    return false;
  }

  if (slaveError) {
    Serial.printf("[ERROR] Slave error during transfer: 0x%02X\n", slaveErrorCode);
    free(photoBuffer);
    photoBuffer = nullptr;
    return false;
  }
  if (!photoEndReceived) {
    Serial.printf("[ERROR] Transfer incomplete: %u/%u packets received\n",
                  packetsReceived, totalPacketsExpected);
    free(photoBuffer);
    photoBuffer = nullptr;
    return false;
  }

  // Phase 3: Verify CRC32
  uint32_t elapsed = millis() - startTime;
  float kbps = (elapsed > 0) ? (photoSize * 8.0f / elapsed) : 0;
  Serial.printf("[PHOTO] Transfer complete: %u bytes, %u packets, %ums (%.1f kbps)\n",
                photoSize, packetsReceived, elapsed, kbps);

  uint32_t calculatedCRC = calculateCRC32(photoBuffer, photoSize);
  Serial.printf("[PHOTO] CRC32 - Calculated: %08X, Received: %08X\n",
                calculatedCRC, receivedCRC32);

  // NEW: Phase 4: Check PSRAM health
  uint32_t voltageCheckVal = photoBuffer[photoSize - 1]; // Read last byte
  if (voltageCheckVal == 0xFF) {
    Serial.println("[WARNING] Last byte suspicious (all 1s) — possible brownout during capture");
    Serial.println("[WARNING] Consider: larger capacitor (100-470µF), better power supply");
    brownoutDetected = true;
  }

  bool success = (calculatedCRC == receivedCRC32);
  if (success) {
    Serial.printf("[PHOTO] CRC32 VERIFIED! %u bytes, %u packets\n",
                  photoSize, packetsReceived);

    // Upload photo to Supabase via A7670 modem
    UploadMetadata meta;
    meta.trapId = DEFAULT_TRAP_ID;
    meta.capturedAt = DEFAULT_CAPTURED_AT;
    meta.gpsLat = DEFAULT_GPS_LAT;
    meta.gpsLon = DEFAULT_GPS_LON;
    meta.ldrValue = capturedLuxValue;
    meta.isFallen = capturedIsFallen ? "true" : "false";
    meta.batteryVoltage = DEFAULT_BATTERY_VOLTAGE;

    if (initModem()) {
      // Diagnostic: Check CRC again immediately before upload to detect buffer corruption
      uint32_t preUploadCRC = calculateCRC32(photoBuffer, photoSize);
      if (preUploadCRC == calculatedCRC) {
        Serial.printf("[PHOTO_PRE_UPLOAD] CRC32 Check: %08X → %08X [STABLE]\n",
                      calculatedCRC, preUploadCRC);
      } else {
        Serial.printf("[WARNING] CRC32 changed after packet reception! %08X → %08X [CORRUPTED]\n",
                      calculatedCRC, preUploadCRC);
      }

      int httpCode = uploadPhoto(photoBuffer, photoSize, meta);
      if (httpCode == 200 || httpCode == 201) {
        Serial.println("[UPLOAD] Photo uploaded successfully!");
      } else {
        Serial.printf("[UPLOAD] Upload failed! HTTP %d\n", httpCode);
      }
    } else {
      Serial.println("[UPLOAD] Modem init failed, photo not uploaded");
    }
  } else {
    Serial.printf("[ERROR] CRC32 MISMATCH! Got %08X, expected %08X\n",
                  calculatedCRC, receivedCRC32);
    Serial.printf("[DEBUG] Packets received: %u/%u\n", packetsReceived, totalPacketsExpected);
  }

  free(photoBuffer);
  photoBuffer = nullptr;

  return success;
}
