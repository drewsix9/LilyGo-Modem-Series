/**
 * @file      ESP32CAM_Slave.ino
 * @author    Modified for UART hybrid camera transfer
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-06
 * @note      ESP32-CAM Slave — captures photo and sends to LilyGo master via UART
 *
 * Hardware:
 *  - AI-Thinker ESP32-CAM board
 *  - Powered by LilyGo master via GPIO23 MOSFET switch
 *
 * Module layout:
 *  uart_protocol.h — Shared UART protocol constants and CRC32
 *  camera.h          — Camera init, capture pipeline, flash LED control
 *  uart_slave.h    — UART parser, READY/SNAP/SIZE/SEND/DONE and photo TX
 */

#include "Arduino.h"
#include "EEPROM.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

#include <WiFi.h>
#include <esp_bt.h>

#include "camera.h"
#include "sdcard.h"
#include "uart_protocol.h"
#include "uart_slave.h"

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
  Serial.println("ESP32-CAM UART Slave");
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

  // Initialise camera at VGA — slave always uses VGA or smaller for UART transfers
  if (!initCamera(FRAMESIZE_VGA, 30)) {
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

  // Power-save requirement: keep radios off for wired UART mode.
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.println("[WIFI/BT] Disabled for low-power UART mode");

  // Initialize UART transport to master
  if (!scanAndPairWithMaster()) {
    Serial.println("[UART] FATAL: Could not initialize UART link, restarting...");
    delay(2000);
    ESP.restart();
  }

  // Send READY twice for reliability (master may miss the first one)
  delay(200);
  sendReadySignal();
  delay(300);
  sendReadySignal();

  Serial.println("[STATUS] Setup complete, waiting for SNAP command...");
  Serial.println("===========================================\n");
}

// ==================== LOOP ====================
void loop() {
  pollSerialCommands();

  if (photoRequested) {
    photoRequested = false;

    Serial.printf("\n[CMD] Processing PHOTO command: LUX=%d, %dx%d, Q=%d\n",
                  cmdLux, cmdWidth, cmdHeight, cmdQuality);

    // Copy volatile values to locals before validation
    uint16_t w = cmdWidth;
    uint16_t h = cmdHeight;
    uint8_t q = cmdQuality;
    uint16_t l = cmdLux;

    if (w < PHOTO_WIDTH_MIN || w > PHOTO_WIDTH_MAX || h < PHOTO_HEIGHT_MIN || h > PHOTO_HEIGHT_MAX) {
      Serial.println("[ERROR] Invalid resolution");
      uint8_t errPkt[2] = {0xF0, ERR_INVALID_PARAMS};
      espnowSendReliable(errPkt, sizeof(errPkt));
      return;
    }
    if (q < PHOTO_QUALITY_MIN || q > PHOTO_QUALITY_MAX) {
      Serial.println("[ERROR] Invalid quality (1-63)");
      uint8_t errPkt[2] = {0xF0, ERR_INVALID_PARAMS};
      espnowSendReliable(errPkt, sizeof(errPkt));
      return;
    }

    captureAndSendPhoto(l, w, h, q);
  }

  delay(10);
}
