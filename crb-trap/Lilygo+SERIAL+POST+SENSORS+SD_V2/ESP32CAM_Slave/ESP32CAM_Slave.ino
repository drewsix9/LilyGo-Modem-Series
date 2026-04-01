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
 * Module layout:
 *  espnow_protocol.h — ESP-NOW packet types, CRC32, shared constants
 *  camera.h          — Camera init, capture pipeline, flash LED control
 *  espnow_slave.h    — WiFi scan, master pairing, reliable send, READY & photo TX
 */

#include "Arduino.h"
#include "EEPROM.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "camera.h"
#include "espnow_protocol.h"
#include "espnow_slave.h"
#include "sdcard.h"

// ==================== EEPROM ====================
#define EEPROM_SIZE 1

// ==================== GLOBALS ====================
uint8_t pictureNumber = 1;

// ==================== SETUP ====================
void setup() {
  // Disable brownout detector to prevent spurious resets during camera power-on
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(500);

  Serial.println("\n\n===========================================");
  Serial.println("ESP32-CAM ESP-NOW Slave");
  Serial.println("===========================================");
  Serial.println("[INIT] Brownout detector disabled");

  // Restore picture counter from EEPROM
  EEPROM.begin(EEPROM_SIZE);
  uint8_t stored = EEPROM.read(0);
  pictureNumber = (stored == 255) ? 1 : (uint8_t)(stored + 1);
  Serial.printf("[EEPROM] Picture number: %d\n", pictureNumber);

  // Reset I2C SCCB bus before camera init to clear any lock-up state
  initializeSCCBBus();
  delay(500);

  // Initialise camera at VGA — slave always uses VGA or smaller for ESP-NOW transfers
  if (!initCamera(FRAMESIZE_VGA, 10)) {
    Serial.println("[CAM] FATAL: Camera init failed, restarting...");
    delay(1000);
    ESP.restart();
  }
  delay(500);

  // Initialize SD card for photo archival (non-critical; system continues if SD absent)
  initSDCard();
  delay(500);

  // Configure flash LED pin
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  // Start WiFi in STA mode and set channel to match master AP
  Serial.println("[WIFI] Initializing WiFi STA mode...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Force 802.11b mode for stable JPEG transfers
  // 802.11n is fast but sensitive to multipath interference; 802.11b is robust for binary data
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);
  esp_wifi_set_ps(WIFI_PS_NONE); // Disable power save for consistent latency
  Serial.println("[WIFI] WiFi protocol forced to 802.11b, power save disabled");

  Serial.printf("[WIFI] STA MAC: %s\n", WiFi.macAddress().c_str());

  // Initialise ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] FATAL: Init failed, restarting...");
    delay(1000);
    ESP.restart();
  }
  Serial.println("[ESPNOW] Initialized");

  // Scan for master AP and register as ESP-NOW peer
  if (!scanAndPairWithMaster()) {
    Serial.println("[ESPNOW] FATAL: Could not pair with master, restarting...");
    delay(2000);
    ESP.restart();
  }

  // Send READY twice for reliability (master may miss the first one)
  delay(200);
  sendReadySignal();
  delay(300);
  sendReadySignal();

  Serial.println("[STATUS] Setup complete, waiting for PHOTO command...");
  Serial.println("===========================================\n");
}

// ==================== LOOP ====================
void loop() {
  if (photoRequested) {
    photoRequested = false;

    Serial.printf("\n[CMD] Processing PHOTO command: LUX=%d, %dx%d, Q=%d\n",
                  cmdLux, cmdWidth, cmdHeight, cmdQuality);

    // Copy volatile values to locals before validation
    uint16_t w = cmdWidth;
    uint16_t h = cmdHeight;
    uint8_t q = cmdQuality;
    uint16_t l = cmdLux;

    if (w < 320 || w > 1600 || h < 240 || h > 1200) {
      Serial.println("[ERROR] Invalid resolution");
      uint8_t errPkt[2] = {PKT_ERROR, ERR_INVALID_PARAMS};
      espnowSendReliable(errPkt, sizeof(errPkt));
      return;
    }
    if (q < 1 || q > 63) {
      Serial.println("[ERROR] Invalid quality (1-63)");
      uint8_t errPkt[2] = {PKT_ERROR, ERR_INVALID_PARAMS};
      espnowSendReliable(errPkt, sizeof(errPkt));
      return;
    }

    captureAndSendPhoto(l, w, h, q);
  }

  delay(10);
}
