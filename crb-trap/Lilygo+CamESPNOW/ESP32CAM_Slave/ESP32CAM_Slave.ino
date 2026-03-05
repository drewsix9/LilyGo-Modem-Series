/**
 * @file      ESP32CAM_Slave.ino
 * @author    Modified for ESP-NOW camera transfer
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-06
 * @note      ESP32-CAM Slave — captures photo and sends to LilyGo master via ESP-NOW
 *
 * Hardware:
 *  - AI-Thinker ESP32-CAM board
 *  - Powered by LilyGo master via GPIO23 MOSFET switch
 *
 * ESP-NOW Protocol:
 *  Packet types (first byte):
 *    0x00 = READY      (slave → master)  payload: "READY" (5 bytes)
 *    0x01 = PHOTO_CMD  (master → slave)  payload: lux(2)+w(2)+h(2)+q(1) = 7 bytes
 *    0x10 = PHOTO_START(slave → master)  payload: totalSize(4)+totalPackets(2) = 6 bytes
 *    0x20 = PHOTO_DATA (slave → master)  payload: packetNum(2)+data(<=240) = max 242 bytes
 *    0x30 = PHOTO_END  (slave → master)  payload: CRC32(4) = 4 bytes
 *    0xF0 = ERROR      (slave → master)  payload: error code(1) = 1 byte
 *
 * Workflow:
 *  1. Master powers ON camera via GPIO23
 *  2. Camera boots, inits WiFi STA + ESP-NOW
 *  3. Scans for master AP "CRBMaster", extracts BSSID, adds as ESP-NOW peer
 *  4. Sends READY packet to master
 *  5. Waits for PHOTO_CMD from master
 *  6. Captures photo, sends via ESP-NOW in 240-byte chunks
 *  7. Sends PHOTO_END with CRC32
 *  8. Master verifies, then cuts power
 */

#include "Arduino.h"
#include "EEPROM.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>

// ==================== PIN DEFINITIONS ====================
// Camera pins for AI-THINKER ESP32-CAM
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// Flash LED
#define FLASH_LED_PIN 4

// ==================== ESP-NOW CONFIGURATION ====================
#define ESPNOW_CHANNEL 1
#define MASTER_AP_SSID "CRBMaster"
#define ESPNOW_DATA_SIZE 240 // Max data bytes per ESP-NOW packet (250 - 10 header margin)
#define MAX_SEND_RETRIES 5   // Retries per packet on send failure
#define SCAN_MAX_RETRIES 10  // Max WiFi scan retries to find master
#define SEND_TIMEOUT_MS 2000 // Timeout waiting for send callback

// ==================== ESP-NOW PACKET TYPES ====================
#define PKT_READY 0x00
#define PKT_PHOTO_CMD 0x01
#define PKT_PHOTO_START 0x10
#define PKT_PHOTO_DATA 0x20
#define PKT_PHOTO_END 0x30
#define PKT_ERROR 0xF0

// ==================== EEPROM CONFIGURATION ====================
#define EEPROM_SIZE 1

// ==================== GLOBAL VARIABLES ====================
uint8_t pictureNumber = 1;
bool cameraInitialized = false;

// ESP-NOW state
uint8_t masterMac[6] = {0};
volatile bool espnowSendDone = false;
volatile bool espnowSendSuccess = false;
volatile bool photoRequested = false;
volatile uint16_t cmdLux = 500;
volatile uint16_t cmdWidth = 640;
volatile uint16_t cmdHeight = 480;
volatile uint8_t cmdQuality = 10;
bool isPaired = false;

// ==================== CRC32 CALCULATION ====================
uint32_t calculateCRC32(uint8_t *data, uint32_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (uint32_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
  }
  return crc ^ 0xFFFFFFFF;
}

// ==================== HELPERS ====================
static framesize_t selectFrameSize(uint16_t width, uint16_t height) {
  if (width >= 1280 || height >= 1024)
    return FRAMESIZE_SXGA;
  if (width >= 1024 || height >= 768)
    return FRAMESIZE_XGA;
  if (width >= 800 || height >= 600)
    return FRAMESIZE_SVGA;
  return FRAMESIZE_VGA;
}

static void initializeSCCBBus() {
  Serial.println("[CAM] Initializing I2C SCCB bus...");
  pinMode(SIOD_GPIO_NUM, OUTPUT);
  pinMode(SIOC_GPIO_NUM, OUTPUT);
  digitalWrite(SIOD_GPIO_NUM, HIGH);
  digitalWrite(SIOC_GPIO_NUM, HIGH);
  delay(10);

  for (int i = 0; i < 9; i++) {
    digitalWrite(SIOC_GPIO_NUM, LOW);
    delay(2);
    digitalWrite(SIOC_GPIO_NUM, HIGH);
    delay(2);
  }

  digitalWrite(SIOD_GPIO_NUM, LOW);
  delay(2);
  digitalWrite(SIOD_GPIO_NUM, HIGH);
  delay(2);

  Serial.println("[CAM] I2C bus reset complete");
}

static uint8_t calculateFlashBrightness(uint16_t lux) {
  if (lux < 50)
    return 255;
  if (lux < 150)
    return 192;
  if (lux < 300)
    return 128;
  if (lux < 500)
    return 64;
  return 0;
}

static void setFlashBrightness(uint8_t brightness) {
  if (brightness > 0) {
    if (FLASH_LED_PIN >= 0) {
      digitalWrite(FLASH_LED_PIN, HIGH);
      Serial.printf("[LED] Flash ON (level %d/255)\n", brightness);
    }
  } else {
    if (FLASH_LED_PIN >= 0) {
      digitalWrite(FLASH_LED_PIN, LOW);
    }
    Serial.println("[LED] Flash OFF");
  }
}

// ==================== CAMERA INIT ====================
bool initCamera(framesize_t frameSize, uint8_t jpegQuality) {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = frameSize;
    config.jpeg_quality = jpegQuality;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }

  config.grab_mode = CAMERA_GRAB_LATEST;

  Serial.printf("[CAM] Initializing camera (size=%d, quality=%d, xclk=20MHz)...\n",
                frameSize, jpegQuality);
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] ERROR: Camera init failed 0x%x\n", err);
    cameraInitialized = false;
    return false;
  }
  Serial.println("[CAM] Camera initialized successfully");
  cameraInitialized = true;
  return true;
}

// ==================== ESP-NOW CALLBACKS ====================
// Version-aware callbacks: ESP-IDF v5.4+ changed the callback signatures.

// Internal handlers (common logic)
static void handleSendResult(esp_now_send_status_t status) {
  espnowSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  espnowSendDone = true;
}

static void handleRecvData(const uint8_t *mac_addr, const uint8_t *data, int data_len);

// Register the correct callback signature based on ESP-IDF version
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
  // New API: send cb uses wifi_tx_info_t*, recv cb uses esp_now_recv_info_t*
  static void espnowSendCb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    handleSendResult(status);
  }
  static void espnowRecvCb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len) {
    handleRecvData(recv_info->src_addr, data, data_len);
  }
#else
  // Old API: send cb uses const uint8_t*, recv cb uses const uint8_t*
  static void espnowSendCb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    handleSendResult(status);
  }
  static void espnowRecvCb(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    handleRecvData(mac_addr, data, data_len);
  }
#endif

static void handleRecvData(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  if (data_len < 1)
    return;

  uint8_t pktType = data[0];

  switch (pktType) {
  case PKT_PHOTO_CMD:
    if (data_len >= 8) {
      // Parse: type(1) + lux(2) + width(2) + height(2) + quality(1)
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

  default:
    Serial.printf("[ESPNOW] Unknown packet type: 0x%02X (len=%d)\n", pktType, data_len);
    break;
  }
}

// ==================== ESP-NOW SEND WITH RETRY ====================

/**
 * @brief Send data via ESP-NOW with blocking wait and retry
 * @return true if delivered successfully
 */
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

    // Wait for send callback
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

// ==================== SCAN AND PAIR WITH MASTER ====================

/**
 * @brief Scan WiFi for master AP "CRBMaster" and add as ESP-NOW peer
 * @return true if paired successfully
 */
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

        // Extract MAC address from BSSID
        String bssidStr = WiFi.BSSIDstr(i);
        int mac[6];
        if (sscanf(bssidStr.c_str(), "%x:%x:%x:%x:%x:%x",
                   &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
          for (int j = 0; j < 6; j++) {
            masterMac[j] = (uint8_t)mac[j];
          }
          found = true;
          break;
        }
      }
    }

    WiFi.scanDelete();

    if (!found) {
      Serial.println("[ESPNOW] Master AP not found in scan results");
      continue;
    }

    // Add master as ESP-NOW peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, masterMac, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;

    // Remove existing peer if any
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

// ==================== SEND READY SIGNAL ====================

/**
 * @brief Send READY packet to master
 */
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

// ==================== SEND PHOTO VIA ESP-NOW ====================

/**
 * @brief Send captured photo to master via ESP-NOW in chunks
 * @param fb Camera frame buffer containing JPEG data
 * @return true if all packets sent successfully
 */
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

  // === Send PHOTO_START packet ===
  // Format: type(1) + totalSize(4) + totalPackets(2) = 7 bytes
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

  // Small delay for master to allocate buffer
  delay(50);

  // === Send PHOTO_DATA packets ===
  uint32_t bytesSent = 0;
  uint16_t packetNum = 0;
  uint32_t sendStartTime = millis();
  uint32_t lastProgressLog = 0;

  while (bytesSent < totalSize) {
    uint32_t remaining = totalSize - bytesSent;
    uint16_t chunkSize = (remaining > ESPNOW_DATA_SIZE) ? ESPNOW_DATA_SIZE : (uint16_t)remaining;

    // Build data packet: type(1) + packetNum(2) + data(chunkSize)
    uint8_t dataPacket[3 + ESPNOW_DATA_SIZE];
    dataPacket[0] = PKT_PHOTO_DATA;
    dataPacket[1] = (uint8_t)(packetNum & 0xFF);
    dataPacket[2] = (uint8_t)((packetNum >> 8) & 0xFF);
    memcpy(dataPacket + 3, fb->buf + bytesSent, chunkSize);

    if (!espnowSendReliable(dataPacket, 3 + chunkSize)) {
      Serial.printf("[ESPNOW] FATAL: Failed to send packet %u at offset %u\n", packetNum, bytesSent);
      return false;
    }

    bytesSent += chunkSize;
    packetNum++;

    // Progress logging every 10KB
    if (bytesSent / 10000 > lastProgressLog) {
      lastProgressLog = bytesSent / 10000;
      uint32_t elapsed = millis() - sendStartTime;
      float kbps = (elapsed > 0) ? (bytesSent * 8.0f / elapsed) : 0;
      Serial.printf("[ESPNOW] Progress: %u/%u bytes (%d%%, %.1f kbps)\n",
                    bytesSent, totalSize,
                    (int)(bytesSent * 100 / totalSize), kbps);
    }

    // Brief yield to prevent WDT reset
    yield();
    delay(2);
  }

  uint32_t transferTime = millis() - sendStartTime;
  float avgKbps = (transferTime > 0) ? (totalSize * 8.0f / transferTime) : 0;
  Serial.printf("[ESPNOW] All %u data packets sent (%u bytes, %ums, %.1f kbps)\n",
                packetNum, bytesSent, transferTime, avgKbps);

  // === Send PHOTO_END with CRC32 ===
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

// ==================== PHOTO CAPTURE ====================

/**
 * @brief Capture a photo and send via ESP-NOW
 */
bool captureAndSendPhoto(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality) {
  if (!cameraInitialized) {
    Serial.println("[ERROR] Camera not initialized");
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (!s) {
    Serial.println("[CAM] ERROR: Failed to get sensor");
    return false;
  }

  // Configure sensor for requested resolution and quality
  framesize_t targetSize = selectFrameSize(width, height);
  s->set_framesize(s, targetSize);
  s->set_quality(s, quality);
  Serial.printf("[CAM] Settings: Size=%dx%d, Quality=%d\n", width, height, quality);

  delay(500);

  // Flash based on LUX
  uint8_t flashBrightness = calculateFlashBrightness(lux);
  if (flashBrightness > 0) {
    setFlashBrightness(flashBrightness);
    delay(200);
  }

  // Capture with warm-up frame strategy (same as UART version)
  Serial.println("[CAM] Capturing image...");
  camera_fb_t *fb = nullptr;
  const int MAX_ATTEMPTS = 5;
  bool warmupDone = false;

  for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
    camera_fb_t *temp = esp_camera_fb_get();
    if (!temp) {
      Serial.printf("[CAM] Capture returned null, attempt %d/%d\n", attempt + 1, MAX_ATTEMPTS);
      delay(1000);
      continue;
    }

    // Diagnostic: dump first 16 bytes
    Serial.printf("[CAM] Frame %d: %u bytes, first 16: ", attempt + 1, temp->len);
    for (int i = 0; i < 16 && i < (int)temp->len; i++) {
      Serial.printf("%02X ", temp->buf[i]);
    }
    Serial.println();

    // Check for JPEG SOI (FF D8 FF) at offset 0
    bool isValidJpeg = (temp->len >= 3 &&
                        temp->buf[0] == 0xFF &&
                        temp->buf[1] == 0xD8 &&
                        temp->buf[2] == 0xFF);

    // Scan entire buffer for SOI if not at offset 0
    uint32_t soiOffset = 0;
    if (!isValidJpeg && temp->len > 3) {
      uint32_t scanLimit = temp->len - 2;
      for (uint32_t i = 1; i < scanLimit; i++) {
        if (temp->buf[i] == 0xFF && temp->buf[i + 1] == 0xD8 && temp->buf[i + 2] == 0xFF) {
          soiOffset = i;
          isValidJpeg = true;
          Serial.printf("[CAM] JPEG SOI found at offset %u\n", soiOffset);
          break;
        }
      }
    }

    if (!isValidJpeg) {
      Serial.printf("[CAM] Invalid frame on attempt %d (%u bytes, no JPEG SOI)\n",
                    attempt + 1, temp->len);
      esp_camera_fb_return(temp);
      delay(1000);
      continue;
    }

    // First valid frame = warm-up (auto-exposure settling)
    if (!warmupDone) {
      warmupDone = true;
      Serial.printf("[CAM] Warm-up frame: %u bytes (discarded for auto-exposure)\n", temp->len);
      esp_camera_fb_return(temp);
      delay(1000);
      continue;
    }

    // Second valid frame — use it
    if (soiOffset > 0) {
      temp->buf += soiOffset;
      temp->len -= soiOffset;
      Serial.printf("[CAM] Adjusted buffer: skipped %u bytes of DMA padding\n", soiOffset);
    }
    fb = temp;
    Serial.printf("[CAM] Valid JPEG captured: %u bytes (attempt %d)\n",
                  fb->len, attempt + 1);
    break;
  }

  // Turn off flash
  if (flashBrightness > 0) {
    setFlashBrightness(0);
  }

  if (!fb) {
    Serial.println("[CAM] ERROR: Capture failed after all attempts");
    // Send error to master
    uint8_t errPkt[2] = {PKT_ERROR, 0x01}; // 0x01 = capture failed
    espnowSendReliable(errPkt, sizeof(errPkt));
    return false;
  }

  Serial.printf("[CAM] Image captured: %u bytes\n", fb->len);

  // Send via ESP-NOW
  bool success = sendPhotoViaESPNOW(fb);
  esp_camera_fb_return(fb);

  if (success) {
    Serial.println("[ESPNOW] Photo transmission complete");
  } else {
    Serial.println("[ESPNOW] Photo transmission failed");
    uint8_t errPkt[2] = {PKT_ERROR, 0x02}; // 0x02 = send failed
    espnowSendReliable(errPkt, sizeof(errPkt));
  }

  return success;
}

// ==================== SETUP ====================
void setup() {
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(500);

  Serial.println("\n\n===========================================");
  Serial.println("ESP32-CAM ESP-NOW Slave");
  Serial.println("===========================================");
  Serial.println("[INIT] Brownout detector disabled");

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);
  uint8_t stored = EEPROM.read(0);
  pictureNumber = (stored == 255) ? 1 : (uint8_t)(stored + 1);
  Serial.printf("[EEPROM] Picture number: %d\n", pictureNumber);

  // Reset I2C bus BEFORE camera init
  initializeSCCBBus();
  delay(500);

  // Initial camera init at VGA (target resolution for ESP-NOW transfer)
  if (!initCamera(FRAMESIZE_VGA, 10)) {
    Serial.println("[CAM] FATAL: Camera init failed, restarting...");
    delay(1000);
    ESP.restart();
  }

  delay(500);

  // Flash LED
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  // === Initialize WiFi + ESP-NOW ===
  Serial.println("[WIFI] Initializing WiFi STA mode...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Set WiFi channel to match master's AP
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.printf("[WIFI] STA MAC: %s\n", WiFi.macAddress().c_str());

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] FATAL: Init failed, restarting...");
    delay(1000);
    ESP.restart();
  }
  Serial.println("[ESPNOW] Initialized");

  // Register callbacks (version-aware wrappers)
  esp_now_register_send_cb(espnowSendCb);
  esp_now_register_recv_cb(espnowRecvCb);

  // Scan for master and pair
  if (!scanAndPairWithMaster()) {
    Serial.println("[ESPNOW] FATAL: Could not pair with master, restarting...");
    delay(2000);
    ESP.restart();
  }

  // Send READY signal to master (send twice for reliability)
  delay(200);
  sendReadySignal();
  delay(300);
  sendReadySignal();

  Serial.println("[STATUS] Setup complete, waiting for PHOTO command...");
  Serial.println("===========================================\n");
}

// ==================== LOOP ====================
void loop() {
  // Check if photo was requested by master
  if (photoRequested) {
    photoRequested = false;

    Serial.printf("\n[CMD] Processing PHOTO command: LUX=%d, %dx%d, Q=%d\n",
                  cmdLux, cmdWidth, cmdHeight, cmdQuality);

    // Validate parameters
    uint16_t w = cmdWidth;
    uint16_t h = cmdHeight;
    uint8_t q = cmdQuality;
    uint16_t l = cmdLux;

    if (w < 320 || w > 1600 || h < 240 || h > 1200) {
      Serial.println("[ERROR] Invalid resolution");
      uint8_t errPkt[2] = {PKT_ERROR, 0x03}; // 0x03 = invalid params
      espnowSendReliable(errPkt, sizeof(errPkt));
      return;
    }
    if (q < 1 || q > 63) {
      Serial.println("[ERROR] Invalid quality (1-63)");
      uint8_t errPkt[2] = {PKT_ERROR, 0x03};
      espnowSendReliable(errPkt, sizeof(errPkt));
      return;
    }

    captureAndSendPhoto(l, w, h, q);
  }

  delay(10);
}
