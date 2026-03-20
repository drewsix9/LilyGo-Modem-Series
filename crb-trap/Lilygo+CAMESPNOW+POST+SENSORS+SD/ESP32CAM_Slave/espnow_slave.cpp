/**
 * @file      espnow_slave.cpp
 * @brief     ESP-NOW slave-side communication implementation.
 *            Handles WiFi scan, master pairing, reliable packet delivery,
 *            READY signal, and full photo streaming with CRC32.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "espnow_slave.h"
#include "espnow_protocol.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ==================== STATE VARIABLES ====================

uint8_t masterMac[6] = {0};
bool isPaired = false;

volatile bool espnowSendDone = false;
volatile bool espnowSendSuccess = false;
volatile bool photoRequested = false;
volatile uint16_t cmdLux = 500;
volatile uint16_t cmdWidth = 640;
volatile uint16_t cmdHeight = 480;
volatile uint8_t cmdQuality = 10;
volatile bool espnowNextReceived = false;        // NEW: PKT_NEXT received from master
volatile uint16_t lastAckCounter = 0;            // NEW: Ack counter from PKT_NEXT
volatile uint16_t expectedAckPacketNum = 0xFFFF; // NEW: Expected packet number for strict ACK validation

// ==================== INTERNAL CALLBACKS ====================

static void handleSendResult(esp_now_send_status_t status) {
  espnowSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  espnowSendDone = true;
}

static void handleRecvData(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  if (data_len < 1)
    return;

  uint8_t pktType = data[0];

  switch (pktType) {
  case PKT_PHOTO_CMD:
    if (data_len >= 8) {
      cmdLux = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
      cmdWidth = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
      cmdHeight = (uint16_t)data[5] | ((uint16_t)data[6] << 8);
      cmdQuality = data[7];
      photoRequested = true;
      Serial.printf("[ESPNOW] PHOTO_CMD received: LUX=%d, %dx%d, Q=%d\n",
                    cmdLux, cmdWidth, cmdHeight, cmdQuality);
    } else {
      Serial.printf("[ESPNOW] Invalid PHOTO_CMD length: %d\n", data_len);
    }
    break;

  case PKT_NEXT: // NEW: Master acknowledging a data packet
    if (data_len >= 3) {
      lastAckCounter = (uint16_t)data[1] | ((uint16_t)data[2] << 8);

      // STRICT: Only accept PKT_NEXT if it matches the packet we're waiting for
      if (lastAckCounter == expectedAckPacketNum) {
        espnowNextReceived = true;
        Serial.printf("[ESPNOW] << PKT_NEXT[%u] received from master (MATCH)\n", lastAckCounter);
      } else {
        // Discard out-of-order or stale ACK
        Serial.printf("[ESPNOW] << PKT_NEXT[%u] DISCARDED (expected %u)\n",
                      lastAckCounter, expectedAckPacketNum);
        // Do NOT set espnowNextReceived = true
        // This forces waiter to timeout and retry/resync
      }
    }
    break;

  default:
    Serial.printf("[ESPNOW] Unknown packet type: 0x%02X (len=%d)\n", pktType, data_len);
    break;
  }
}

// Version-aware callback registration (ESP-IDF v5.4+ changed signatures)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
static void espnowSendCb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  handleSendResult(status);
}
static void espnowRecvCb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len) {
  handleRecvData(recv_info->src_addr, data, data_len);
}
#else
static void espnowSendCb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  handleSendResult(status);
}
static void espnowRecvCb(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  handleRecvData(mac_addr, data, data_len);
}
#endif

// ==================== PUBLIC API ====================

bool espnowSendReliable(const uint8_t *data, size_t len) {
  for (int retry = 0; retry < MAX_SEND_RETRIES; retry++) {
    if (retry > 0) {
      Serial.printf("[ESPNOW] Send retry %d/%d\n", retry + 1, MAX_SEND_RETRIES);
      delay(50 * retry); // Increasing backoff
    }

    espnowSendDone = false;
    espnowSendSuccess = false;

    esp_err_t result = esp_now_send(masterMac, data, len);
    if (result != ESP_OK) {
      Serial.printf("[ESPNOW] esp_now_send error: 0x%x\n", result);
      continue;
    }

    // Block until send callback fires (or timeout)
    uint32_t waitStart = millis();
    while (!espnowSendDone && (millis() - waitStart < SEND_TIMEOUT_MS)) {
      delay(1);
    }

    if (espnowSendDone && espnowSendSuccess) {
      return true;
    }

    if (!espnowSendDone) {
      Serial.println("[ESPNOW] Send callback timeout");
    } else {
      Serial.println("[ESPNOW] Send delivery failed");
    }
  }

  Serial.println("[ESPNOW] FATAL: Send failed after all retries");
  return false;
}

bool scanAndPairWithMaster() {
  Serial.println("[ESPNOW] Scanning for master AP...");

  for (int attempt = 0; attempt < SCAN_MAX_RETRIES; attempt++) {
    if (attempt > 0) {
      Serial.printf("[ESPNOW] Scan attempt %d/%d\n", attempt + 1, SCAN_MAX_RETRIES);
      delay(500);
    }

    int16_t scanResults = WiFi.scanNetworks();
    if (scanResults <= 0) {
      Serial.println("[ESPNOW] No WiFi networks found");
      WiFi.scanDelete();
      continue;
    }

    Serial.printf("[ESPNOW] Found %d networks\n", scanResults);

    bool found = false;
    for (int i = 0; i < scanResults; i++) {
      String ssid = WiFi.SSID(i);
      Serial.printf("  %d: %s (%d dBm)\n", i + 1, ssid.c_str(), WiFi.RSSI(i));

      if (ssid == MASTER_AP_SSID) {
        Serial.printf("[ESPNOW] Master found: %s [%s] (%d dBm)\n",
                      ssid.c_str(), WiFi.BSSIDstr(i).c_str(), WiFi.RSSI(i));

        String bssidStr = WiFi.BSSIDstr(i);
        int mac[6];
        if (sscanf(bssidStr.c_str(), "%x:%x:%x:%x:%x:%x",
                   &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
          for (int j = 0; j < 6; j++) {
            masterMac[j] = (uint8_t)mac[j];
          }
          found = true;
        }
        break;
      }
    }
    WiFi.scanDelete();

    if (!found) {
      Serial.println("[ESPNOW] Master AP not found in scan results");
      continue;
    }

    // Register ESP-NOW callbacks now that we know the channel
    esp_now_register_send_cb(espnowSendCb);
    esp_now_register_recv_cb(espnowRecvCb);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, masterMac, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;

    if (esp_now_is_peer_exist(masterMac)) {
      esp_now_del_peer(masterMac);
    }

    esp_err_t addStatus = esp_now_add_peer(&peerInfo);
    if (addStatus == ESP_OK) {
      Serial.printf("[ESPNOW] Paired with master: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    masterMac[0], masterMac[1], masterMac[2],
                    masterMac[3], masterMac[4], masterMac[5]);
      isPaired = true;
      return true;
    } else {
      Serial.printf("[ESPNOW] Failed to add peer: 0x%x\n", addStatus);
    }
  }

  Serial.println("[ESPNOW] FATAL: Could not find or pair with master");
  return false;
}

bool sendReadySignal() {
  uint8_t packet[6];
  packet[0] = PKT_READY;
  packet[1] = 'R';
  packet[2] = 'E';
  packet[3] = 'A';
  packet[4] = 'D';
  packet[5] = 'Y';

  Serial.println("[ESPNOW] Sending READY signal to master...");
  bool ok = espnowSendReliable(packet, sizeof(packet));
  if (ok) {
    Serial.println("[ESPNOW] READY signal delivered");
  } else {
    Serial.println("[ESPNOW] READY signal delivery failed");
  }
  return ok;
}

// NEW: Helper function to wait for PKT_NEXT handshake signal from master
// Implements timeout with single retry to handle transient radio issues
static bool waitForNextSignal(uint16_t expectedPacketNum) {
#define WAIT_NEXT_MAX_ATTEMPTS 2 // First wait + one retry

  for (uint8_t attempt = 0; attempt < WAIT_NEXT_MAX_ATTEMPTS; attempt++) {
    espnowNextReceived = false;
    lastAckCounter = 0xFFFF;
    expectedAckPacketNum = expectedPacketNum; // Set expected for handler to validate against

    uint32_t waitStart = millis();
    while (!espnowNextReceived && (millis() - waitStart < NEXT_SIGNAL_TIMEOUT_MS)) {
      delay(1);
    }

    if (espnowNextReceived) {
      Serial.printf("[ESPNOW] << PKT_NEXT[%u] confirmed from master\n", lastAckCounter);
      return true;
    }

    if (attempt < WAIT_NEXT_MAX_ATTEMPTS - 1) {
      Serial.printf("[ESPNOW] TIMEOUT on PKT_NEXT[%u] (attempt %u), retrying...\n",
                    expectedPacketNum, attempt + 1);
      delay(50); // Brief pause before retry
    }
  }

  Serial.printf("[ESPNOW] FATAL: TIMEOUT waiting for PKT_NEXT[%u] after %u attempts\n",
                expectedPacketNum, WAIT_NEXT_MAX_ATTEMPTS);
  return false;
}

bool sendPhotoViaESPNOW(camera_fb_t *fb) {
  if (!fb || fb->len == 0) {
    Serial.println("[ESPNOW] ERROR: No frame buffer");
    return false;
  }
  if (!isPaired) {
    Serial.println("[ESPNOW] ERROR: Not paired with master");
    return false;
  }

  uint32_t totalSize = fb->len;
  uint16_t totalPackets = (uint16_t)((totalSize + ESPNOW_DATA_SIZE - 1) / ESPNOW_DATA_SIZE);

  Serial.printf("[ESPNOW] Sending photo: %u bytes in %u packets\n", totalSize, totalPackets);

  // --- PHOTO_START: type(1) + totalSize(4) + totalPackets(2) = 7 bytes ---
  uint8_t startPacket[7];
  startPacket[0] = PKT_PHOTO_START;
  startPacket[1] = (uint8_t)(totalSize & 0xFF);
  startPacket[2] = (uint8_t)((totalSize >> 8) & 0xFF);
  startPacket[3] = (uint8_t)((totalSize >> 16) & 0xFF);
  startPacket[4] = (uint8_t)((totalSize >> 24) & 0xFF);
  startPacket[5] = (uint8_t)(totalPackets & 0xFF);
  startPacket[6] = (uint8_t)((totalPackets >> 8) & 0xFF);

  if (!espnowSendReliable(startPacket, sizeof(startPacket))) {
    Serial.println("[ESPNOW] Failed to send PHOTO_START");
    return false;
  }
  Serial.println("[ESPNOW] PHOTO_START sent");

  // NEW: Wait for frame buffer to stabilize (camera DMA completion)
  // OV2640 may still be writing via DMA even after fb->len is set
  delay(100); // Increased from 50ms to ensure DMA complete

  // Optional: Verify frame not empty (basic sanity check)
  if (fb->buf[0] == 0 && fb->buf[1] == 0 && fb->buf[2] == 0) {
    Serial.println("[WARNING] Frame buffer appears uninitialized or empty");
  }

  // --- PHOTO_DATA packets with handshake: type(1) + packetNum(2) + data(chunkSize) ---
  uint32_t bytesSent = 0;
  uint16_t packetNum = 0;
  uint32_t sendStartTime = millis();
  uint32_t lastProgressLog = 0;

  while (bytesSent < totalSize) {
    uint32_t remaining = totalSize - bytesSent;
    uint16_t chunkSize = (remaining > ESPNOW_DATA_SIZE) ? ESPNOW_DATA_SIZE : (uint16_t)remaining;

    uint8_t dataPacket[3 + ESPNOW_DATA_SIZE];
    dataPacket[0] = PKT_PHOTO_DATA;
    dataPacket[1] = (uint8_t)(packetNum & 0xFF);
    dataPacket[2] = (uint8_t)((packetNum >> 8) & 0xFF);
    memcpy(dataPacket + 3, fb->buf + bytesSent, chunkSize);

    if (!espnowSendReliable(dataPacket, 3 + chunkSize)) {
      Serial.printf("[ESPNOW] FATAL: Failed to send packet %u\n", packetNum);
      // Send error packet to master
      uint8_t errPacket[2];
      errPacket[0] = PKT_ERROR;
      errPacket[1] = ERR_SEND_FAILED;
      esp_now_send(masterMac, errPacket, sizeof(errPacket));
      return false;
    }

    // NEW: Wait for handshake acknowledgement from master
    if (!waitForNextSignal(packetNum)) {
      Serial.printf("[ESPNOW] FATAL: Handshake timeout after packet %u\n", packetNum);
      // Send error packet to master
      uint8_t errPacket[2];
      errPacket[0] = PKT_ERROR;
      errPacket[1] = ERR_SEND_FAILED;
      esp_now_send(masterMac, errPacket, sizeof(errPacket));
      return false;
    }

    bytesSent += chunkSize;
    packetNum++;

    // v2.0: Progress log every 100 KB (reduced from 10KB due to 6x faster throughput)
    if (bytesSent / 100000 > lastProgressLog) {
      lastProgressLog = bytesSent / 100000;
      float pct = (totalSize > 0) ? (bytesSent * 100.0f / totalSize) : 0;
      Serial.printf("[ESPNOW] Sent %u/%u bytes (%.0f%%, pkt %u/~%u)\n",
                    bytesSent, totalSize, pct, packetNum, totalPackets);
    }

    yield();
    // NEW: Reduced delay since we're now waiting for handshake
    delay(1);
  }

  uint32_t transferTime = millis() - sendStartTime;
  float avgKbps = (transferTime > 0) ? (totalSize * 8.0f / transferTime) : 0;
  Serial.printf("[ESPNOW] All %u data packets sent (%u bytes, %ums, %.1f kbps)\n",
                packetNum, bytesSent, transferTime, avgKbps);

  // --- PHOTO_END: type(1) + CRC32(4) = 5 bytes ---
  uint32_t crc32 = calculateCRC32(fb->buf, fb->len);
  Serial.printf("[ESPNOW] CRC32: %08X\n", crc32);

  uint8_t endPacket[5];
  endPacket[0] = PKT_PHOTO_END;
  endPacket[1] = (uint8_t)(crc32 & 0xFF);
  endPacket[2] = (uint8_t)((crc32 >> 8) & 0xFF);
  endPacket[3] = (uint8_t)((crc32 >> 16) & 0xFF);
  endPacket[4] = (uint8_t)((crc32 >> 24) & 0xFF);

  if (!espnowSendReliable(endPacket, sizeof(endPacket))) {
    Serial.println("[ESPNOW] Failed to send PHOTO_END");
    return false;
  }

  Serial.printf("[ESPNOW] Photo transmission complete: %u bytes, CRC32=%08X\n",
                totalSize, crc32);
  return true;
}
