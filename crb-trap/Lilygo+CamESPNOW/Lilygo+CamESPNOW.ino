/**
 * @file      Lilygo+CamESPNOW.ino
 * @author    Modified for ESP-NOW camera control
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-06
 * @note      LilyGo A7670 Master - Controls ESP32CAM via ESP-NOW (wireless, no UART wires)
 *
 * Hardware Setup:
 *  - LILYGO_T_A7670 board (ESP32 Master)
 *  - PIR sensor output -> GPIO32 (wakeup trigger)
 *  - GPIO23 -> ESP32CAM power enable (LOW = ON, HIGH = OFF - inverted logic for MOSFET driver)
 *  - Light sensor (e.g., BH1750) on I2C bus for LUX reading
 *  - NO UART wires needed between master and slave
 *
 * ESP-NOW Protocol:
 *  Packet types (first byte):
 *    0x00 = READY      (slave -> master)  payload: "READY" (5 bytes)
 *    0x01 = PHOTO_CMD  (master -> slave)  payload: lux(2)+w(2)+h(2)+q(1) = 7 bytes
 *    0x10 = PHOTO_START(slave -> master)  payload: totalSize(4)+totalPackets(2) = 6 bytes
 *    0x20 = PHOTO_DATA (slave -> master)  payload: packetNum(2)+data(<=240) = max 242 bytes
 *    0x30 = PHOTO_END  (slave -> master)  payload: CRC32(4) = 4 bytes
 *    0xF0 = ERROR      (slave -> master)  payload: error code(1) = 1 byte
 *
 * Workflow:
 *  1. PIR detects motion -> ESP32 wakes from deep sleep
 *  2. Starts WiFi AP "CRBMaster" for slave discovery
 *  3. Powers ON ESP32CAM via GPIO23
 *  4. Reads LUX value from light sensor
 *  5. Waits for READY from slave (via ESP-NOW)
 *  6. Sends PHOTO_CMD with lux/resolution/quality
 *  7. Receives photo data packets, assembles in RAM
 *  8. Verifies CRC32
 *  9. Powers OFF camera, stops WiFi
 *  10. Returns to deep sleep
 */

#include "utilities.h"
#include <Wire.h>
#include <driver/gpio.h>

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ==================== PIN DEFINITIONS ====================
#define PIR_SENSOR_PIN 32 // PIR sensor wakeup source
#define CAM_PWR_EN_PIN 23 // Camera power enable (LOW = ON, HIGH = OFF - inverted logic)

// ==================== I2C LIGHT SENSOR ====================
// If using BH1750 or similar, uncomment and configure:
// #include <BH1750.h>
// BH1750 lightMeter;
#define I2C_SDA 21 // Default I2C SDA
#define I2C_SCL 22 // Default I2C SCL

// ==================== ESP-NOW CONFIGURATION ====================
#define ESPNOW_CHANNEL 1
#define MASTER_AP_SSID "CRBMaster"
#define MASTER_AP_PASS "12345678"
#define ESPNOW_DATA_SIZE 240 // Must match slave

// ==================== ESP-NOW PACKET TYPES ====================
#define PKT_READY 0x00
#define PKT_PHOTO_CMD 0x01
#define PKT_PHOTO_START 0x10
#define PKT_PHOTO_DATA 0x20
#define PKT_PHOTO_END 0x30
#define PKT_ERROR 0xF0

// ==================== PHOTO PARAMETERS ====================
#define PHOTO_WIDTH 640  // Image width in pixels
#define PHOTO_HEIGHT 480 // Image height in pixels
#define PHOTO_QUALITY 10 // JPEG quality (1-63, lower = higher quality)

// ==================== TIMING ====================
#define CAM_BOOT_TIME_MS 2500   // Camera power-on to WiFi ready
#define READY_TIMEOUT_MS 15000  // Timeout waiting for READY signal
#define PHOTO_TIMEOUT_MS 60000  // Timeout waiting for entire photo transfer
#define PIR_SETTLE_TIME_MS 2000 // PIR sensor settle time
#define PHOTO_MAX_RETRIES 3     // Retry photo on failure

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

// ==================== ESP-NOW STATE ====================
// Volatile flags set by callbacks, polled by main code
volatile bool camReady = false;
volatile bool photoStartReceived = false;
volatile bool photoEndReceived = false;
volatile bool slaveError = false;
volatile uint8_t slaveErrorCode = 0;

// Photo reception buffers
uint8_t *photoBuffer = nullptr;
volatile uint32_t photoSize = 0;
volatile uint16_t totalPacketsExpected = 0;
volatile uint16_t packetsReceived = 0;
volatile uint32_t receivedCRC32 = 0;
volatile uint32_t lastPacketTime = 0;

// Slave MAC address (learned from READY packet)
uint8_t slaveMac[6] = {0};
bool slavePaired = false;

// ==================== FUNCTION PROTOTYPES ====================
void print_wakeup_reason();
uint16_t readLuxSensor();
bool waitForCameraReady();
bool sendPhotoCommand(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality);
bool receivePhoto();
void initESPNOW();
void cleanupESPNOW();
void powerOnCamera();
void powerOffCamera();
void enterDeepSleep();

// ==================== ESP-NOW CALLBACKS ====================
// Version-aware callbacks: ESP-IDF v5.4+ changed the callback signatures.

// Internal handler for received data (common logic)
static void handleRecvData(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  if (data_len < 1)
    return;

  lastPacketTime = millis();
  uint8_t pktType = data[0];

  switch (pktType) {

  case PKT_READY:
    // Store slave MAC for future sends
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
      // Parse: type(1) + totalSize(4) + totalPackets(2)
      photoSize = (uint32_t)data[1] |
                  ((uint32_t)data[2] << 8) |
                  ((uint32_t)data[3] << 16) |
                  ((uint32_t)data[4] << 24);
      totalPacketsExpected = (uint16_t)data[5] | ((uint16_t)data[6] << 8);

      Serial.printf("[ESPNOW] PHOTO_START: %u bytes, %u packets expected\n",
                    photoSize, totalPacketsExpected);

      // Allocate buffer for photo
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
      // Parse: type(1) + packetNum(2) + data(remaining)
      uint16_t packetNum = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
      uint16_t dataLen = data_len - 3;
      uint32_t offset = (uint32_t)packetNum * ESPNOW_DATA_SIZE;

      if (offset + dataLen <= photoSize) {
        memcpy(photoBuffer + offset, data + 3, dataLen);
        packetsReceived++;

        // Progress logging every 50 packets
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

// Send callback (old ESP-IDF API — master only compiles with PlatformIO)
static void espnowSendCb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("[ESPNOW] Send delivery failed");
  }
}

// Receive callback wrapper
static void espnowRecvCb(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  handleRecvData(mac_addr, data, data_len);
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n===========================================");
  Serial.println("LILYGO A7670 - Camera ESP-NOW Master");
  Serial.println("===========================================");
  Serial.println("[PINS] GPIO23->CAM_POWER (LOW=ON), GPIO32->PIR_WAKEUP");
  Serial.println("[COMM] ESP-NOW wireless (no UART wires)");

  // Print wakeup reason
  print_wakeup_reason();

  // Release GPIO hold from deep sleep
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)CAM_PWR_EN_PIN);

  // Initialize camera power pin
  pinMode(CAM_PWR_EN_PIN, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)CAM_PWR_EN_PIN, GPIO_DRIVE_CAP_3);
  digitalWrite(CAM_PWR_EN_PIN, HIGH); // Camera OFF initially

  Serial.println("[GPIO] GPIO23 configured with maximum drive strength");

  // Initialize I2C for light sensor
  Wire.begin(I2C_SDA, I2C_SCL);

  // ==================== HANDLE WAKEUP EVENT ====================
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("\n[EVENT] Motion detected by PIR sensor!");

    // Read light intensity
    uint16_t luxValue = readLuxSensor();
    Serial.printf("[SENSOR] Current light intensity: %d LUX\n", luxValue);

    // Initialize WiFi AP + ESP-NOW BEFORE powering on camera
    // Master AP must be running so slave can scan and find it
    initESPNOW();

    // Power ON camera
    powerOnCamera();

    // Wait for camera to boot
    Serial.printf("[WAIT] Waiting %d ms for camera power-on...\n", CAM_BOOT_TIME_MS);
    delay(CAM_BOOT_TIME_MS);

    // Wait for camera READY signal (via ESP-NOW)
    Serial.println("[ESPNOW] Waiting for camera READY signal...");
    if (waitForCameraReady()) {
      Serial.println("[ESPNOW] Camera is READY");

      // Attempt photo capture with retry on failure
      bool photoSuccess = false;
      for (int attempt = 1; attempt <= PHOTO_MAX_RETRIES && !photoSuccess; attempt++) {
        if (attempt > 1) {
          Serial.printf("\n[RETRY] Attempt %d/%d\n", attempt, PHOTO_MAX_RETRIES);
          // Reset state for retry
          photoStartReceived = false;
          photoEndReceived = false;
          slaveError = false;
          packetsReceived = 0;
          if (photoBuffer) {
            free(photoBuffer);
            photoBuffer = nullptr;
          }
          delay(500);
        }

        bool cmdSent = sendPhotoCommand(luxValue, PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY);
        if (cmdSent) {
          Serial.println("[SUCCESS] Photo command sent, receiving photo...");
          photoSuccess = receivePhoto();
          if (photoSuccess) {
            Serial.printf("[SUCCESS] Photo received and verified! (attempt %d/%d)\n",
                          attempt, PHOTO_MAX_RETRIES);
          } else {
            Serial.printf("[ERROR] Photo reception failed on attempt %d/%d\n",
                          attempt, PHOTO_MAX_RETRIES);
          }
        } else {
          Serial.println("[ERROR] Photo command send failed!");
        }
      }

      if (!photoSuccess) {
        Serial.printf("[FAIL] All %d photo attempts failed!\n", PHOTO_MAX_RETRIES);
      }

    } else {
      Serial.println("[ERROR] Camera READY timeout!");
    }

    // Brief delay for final operations
    delay(1000);

    // Power OFF camera
    powerOffCamera();

    // Cleanup WiFi/ESP-NOW
    cleanupESPNOW();

  } else {
    // First boot or reset
    Serial.println("\n[BOOT] First boot or reset detected");
    Serial.println("[TEST] Testing camera power control...");

    powerOnCamera();
    delay(1000);
    powerOffCamera();

    Serial.println("[TEST] Power control test completed");
  }

  // ==================== PREPARE FOR DEEP SLEEP ====================
  pinMode(PIR_SENSOR_PIN, INPUT_PULLDOWN);
  gpio_pulldown_en((gpio_num_t)PIR_SENSOR_PIN);

  Serial.println("\n[CONFIG] System configuration:");
  Serial.println("  - Wakeup source: GPIO32 (PIR Sensor)");
  Serial.println("  - Camera power: GPIO23 (LOW = ON, HIGH = OFF - inverted logic)");
  Serial.println("  - Communication: ESP-NOW (wireless, no UART wires)");
  Serial.printf("  - Master AP: %s (channel %d)\n", MASTER_AP_SSID, ESPNOW_CHANNEL);
  Serial.printf("  - Photo size: %dx%d, Quality: %d\n",
                PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY);

  // Enter deep sleep
  enterDeepSleep();
}

void loop() {
  // Never reached - ESP32 goes to deep sleep in setup()
}

// ==================== FUNCTION IMPLEMENTATIONS ====================

/**
 * @brief Print the reason for ESP32 wakeup
 */
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
  case ESP_SLEEP_WAKEUP_EXT0:
    Serial.println("[WAKEUP] External signal (PIR Sensor) via RTC_IO");
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    Serial.println("[WAKEUP] External signal via RTC_CNTL");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.println("[WAKEUP] Timer");
    break;
  default:
    Serial.printf("[WAKEUP] Not from deep sleep (reason: %d)\n", wakeup_reason);
    break;
  }
}

/**
 * @brief Read light intensity from LUX sensor
 * @return LUX value (0-65535)
 */
uint16_t readLuxSensor() {
  // TODO: Replace with actual light sensor reading
  // Example for BH1750:
  // float lux = lightMeter.readLightLevel();
  // return (uint16_t)lux;
  uint16_t simulatedLux = 500;
  return simulatedLux;
}

/**
 * @brief Initialize WiFi AP + ESP-NOW for slave communication
 */
void initESPNOW() {
  Serial.println("[WIFI] Starting AP mode for slave discovery...");

  // Start as AP so slave can find us via WiFi scan
  WiFi.mode(WIFI_AP);
  WiFi.softAP(MASTER_AP_SSID, MASTER_AP_PASS, ESPNOW_CHANNEL);
  delay(100);

  Serial.printf("[WIFI] AP started: SSID=%s, Channel=%d\n", MASTER_AP_SSID, ESPNOW_CHANNEL);
  Serial.printf("[WIFI] AP MAC: %s\n", WiFi.softAPmacAddress().c_str());

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] FATAL: Init failed!");
    return;
  }
  Serial.println("[ESPNOW] Initialized");

  // Register callbacks
  esp_now_register_recv_cb(espnowRecvCb);
  esp_now_register_send_cb(espnowSendCb);

  // Reset state
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

/**
 * @brief Clean up WiFi and ESP-NOW
 */
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

/**
 * @brief Power ON the camera
 */
void powerOnCamera() {
  Serial.println("[POWER] Turning camera ON (GPIO23 -> LOW - inverted logic)");
  digitalWrite(CAM_PWR_EN_PIN, LOW); // Inverted logic: LOW = ON
  delay(100);
  Serial.println("[STATUS] Camera powered ON");
}

/**
 * @brief Power OFF the camera
 */
void powerOffCamera() {
  Serial.println("[POWER] Turning camera OFF (GPIO23 -> HIGH - inverted logic)");
  digitalWrite(CAM_PWR_EN_PIN, HIGH); // Inverted logic: HIGH = OFF
  delay(100);

  // Hold GPIO23 state during deep sleep
  gpio_hold_en((gpio_num_t)CAM_PWR_EN_PIN);
  gpio_deep_sleep_hold_en();

  Serial.println("[STATUS] Camera powered OFF and isolated");
}

/**
 * @brief Wait for READY signal from camera slave
 * @return true if READY received, false on timeout
 */
bool waitForCameraReady() {
  camReady = false;
  uint32_t startTime = millis();

  Serial.println("[ESPNOW] Waiting for READY (polling flag)...");

  while (millis() - startTime < READY_TIMEOUT_MS) {
    if (camReady) {
      Serial.printf("[SUCCESS] READY received after %ldms\n", millis() - startTime);

      // Add slave as peer for sending commands
      if (!slavePaired) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, slaveMac, 6);
        peerInfo.channel = ESPNOW_CHANNEL;
        peerInfo.ifidx = WIFI_IF_AP; // Master runs as AP, must use AP interface
        peerInfo.encrypt = false;

        // Remove existing peer if any
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

/**
 * @brief Send PHOTO command to slave
 * @return true if send succeeded
 */
bool sendPhotoCommand(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality) {
  if (!slavePaired) {
    Serial.println("[ERROR] Slave not paired, cannot send command");
    return false;
  }

  // Build command packet: type(1) + lux(2) + width(2) + height(2) + quality(1) = 8 bytes
  uint8_t packet[8];
  packet[0] = PKT_PHOTO_CMD;
  packet[1] = (uint8_t)(lux & 0xFF);
  packet[2] = (uint8_t)((lux >> 8) & 0xFF);
  packet[3] = (uint8_t)(width & 0xFF);
  packet[4] = (uint8_t)((width >> 8) & 0xFF);
  packet[5] = (uint8_t)(height & 0xFF);
  packet[6] = (uint8_t)((height >> 8) & 0xFF);
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

/**
 * @brief Receive photo from slave via ESP-NOW and verify CRC32
 * @return true if photo received and CRC32 matches
 */
bool receivePhoto() {
  Serial.println("[PHOTO] Waiting for photo transmission...");

  // Reset state
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

  // Phase 2: Wait for all PHOTO_DATA + PHOTO_END
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
    if (photoBuffer) {
      free(photoBuffer);
      photoBuffer = nullptr;
    }
    return false;
  }

  if (!photoEndReceived) {
    Serial.printf("[ERROR] Transfer incomplete: %u/%u packets received\n",
                  packetsReceived, totalPacketsExpected);
    if (photoBuffer) {
      free(photoBuffer);
      photoBuffer = nullptr;
    }
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

  // Free buffer
  free(photoBuffer);
  photoBuffer = nullptr;

  return success;
}

/**
 * @brief Enter ESP32 deep sleep mode and wait for PIR trigger
 */
void enterDeepSleep() {
  Serial.println("\n[WAIT] PIR sensor settling...");
  delay(PIR_SETTLE_TIME_MS);

  // Ensure camera stays OFF during deep sleep
  digitalWrite(CAM_PWR_EN_PIN, HIGH);
  gpio_hold_en((gpio_num_t)CAM_PWR_EN_PIN);
  gpio_deep_sleep_hold_en();

  Serial.println("[GPIO] Camera power pin held HIGH (OFF) for deep sleep");

  Serial.println("[SLEEP] Entering deep sleep mode...");
  Serial.println("         Waiting for PIR motion detection...");
  Serial.println("===========================================\n");
  Serial.flush();

  // Enable wakeup on GPIO32 (PIR sensor HIGH)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_SENSOR_PIN, 1);

  delay(200);
  esp_deep_sleep_start();
}
