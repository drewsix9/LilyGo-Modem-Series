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

uint8_t *photoBuffer = nullptr;
volatile uint32_t photoSize = 0;
volatile uint16_t totalPacketsExpected = 0;
volatile uint16_t packetsReceived = 0;
volatile uint32_t receivedCRC32 = 0;
volatile uint32_t lastPacketTime = 0;

uint8_t slaveMac[6] = {0};
bool slavePaired = false;

// ==================== INTERNAL CALLBACKS ====================

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
        photoBuffer = (uint8_t *)malloc(photoSize);
        if (photoBuffer) {
          memset(photoBuffer, 0, photoSize);
          packetsReceived = 0;
          photoStartReceived = true;
          Serial.printf("[ESPNOW] Buffer allocated: %u bytes\n", photoSize);
        } else {
          Serial.println("[ESPNOW] ERROR: malloc failed!");
        }
      } else {
        Serial.printf("[ESPNOW] ERROR: Invalid photo size: %u\n", photoSize);
      }
    }
    break;

  case PKT_PHOTO_DATA:
    if (data_len >= 3 && photoBuffer && photoStartReceived) {
      uint16_t packetNum = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
      uint16_t dataLen = (uint16_t)(data_len - 3);
      uint32_t offset = (uint32_t)packetNum * ESPNOW_DATA_SIZE;

      if (offset + dataLen <= photoSize) {
        memcpy(photoBuffer + offset, data + 3, dataLen);
        packetsReceived++;

        if (packetsReceived % 50 == 0) {
          uint32_t bytesRecv = (uint32_t)packetsReceived * ESPNOW_DATA_SIZE;
          if (bytesRecv > photoSize)
            bytesRecv = photoSize;
          Serial.printf("[ESPNOW] Progress: %u/%u packets (%u/%u bytes, %d%%)\n",
                        packetsReceived, totalPacketsExpected,
                        bytesRecv, photoSize,
                        (int)(bytesRecv * 100 / photoSize));
        }
      } else {
        Serial.printf("[ESPNOW] WARNING: Packet %u offset %u + %u exceeds size %u\n",
                      packetNum, offset, dataLen, photoSize);
      }
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
  lastPacketTime = millis();

  uint32_t startTime = millis();
  const uint32_t NO_DATA_TIMEOUT_MS = 15000;

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
  while (!photoEndReceived && !slaveError) {
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

  bool success = (calculatedCRC == receivedCRC32);
  if (success) {
    Serial.printf("[PHOTO] CRC32 VERIFIED! %u bytes, %u packets\n",
                  photoSize, packetsReceived);
    // TODO: Process photo data here (e.g., upload via A7670 modem, save to SD)
    // photoBuffer contains the complete JPEG image of photoSize bytes
  } else {
    Serial.printf("[ERROR] CRC32 MISMATCH! Got %08X, expected %08X\n",
                  calculatedCRC, receivedCRC32);
    Serial.printf("[DEBUG] Packets received: %u/%u\n", packetsReceived, totalPacketsExpected);
  }

  free(photoBuffer);
  photoBuffer = nullptr;

  return success;
}
