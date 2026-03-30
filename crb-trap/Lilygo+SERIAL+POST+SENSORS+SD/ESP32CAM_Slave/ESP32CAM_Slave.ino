/**
 * @file      ESP32CAM_Slave.ino
 * @author    Modified for SerialTransfer camera transmission
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-21
 * @note      ESP32-CAM Slave — captures photo and sends to LilyGo master via UART + SerialTransfer
 *
 * Hardware:
 *  - AI-Thinker ESP32-CAM board
 *  - Powered by LilyGo master via GPIO23 MOSFET switch
 *  - Serial2 UART: GPIO12 (TX) → Master RX, GPIO16 (RX) ← Master TX
 *
 * Module layout:
 *  serialtransfer_protocol.h — SerialTransfer packet types, metadata structs
 *  camera.h                  — Camera init, capture pipeline, flash LED control
 *  serialtransfer_slave.h    — UART init, READY signal, photo chunking, error handling
 *  sdcard.h                  — SD card photo archival
 */

#include "Arduino.h"
#include "EEPROM.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

#include "camera.h"
#include "sdcard.h"
#include "serialtransfer_protocol.h"
#include "serialtransfer_slave.h"

// ==================== EEPROM ====================
#define EEPROM_SIZE 1

// ==================== GLOBALS ====================
uint8_t pictureNumber = 1;

// ==================== SETUP ====================
void setup() {
  // Initialize watchdog with a 10-second timeout FIRST, before ANY hardware init
  // This allows slow components (camera, SD card) time to initialize
  esp_task_wdt_config_t twdt_config = {
      .timeout_ms = 10000,  // 10 seconds in milliseconds
      .idle_core_mask = 0,  // Apply to both cores
      .trigger_panic = true // Panic on timeout
  };
  esp_task_wdt_init(&twdt_config);
  esp_task_wdt_add(NULL); // Add current task to watchdog

  // Disable brownout detector to prevent spurious resets during camera power-on
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(500);

  Serial.println("\n\n===========================================");
  Serial.println("ESP32-CAM SerialTransfer Slave");
  Serial.println("===========================================");
  Serial.println("[INIT] Watchdog initialized (10s timeout)");
  Serial.println("[INIT] Brownout detector disabled");

  // Restore picture counter from EEPROM
  EEPROM.begin(EEPROM_SIZE);
  uint8_t stored = EEPROM.read(0);
  pictureNumber = (stored == 255) ? 1 : (uint8_t)(stored + 1);
  Serial.printf("[EEPROM] Picture number: %d\n", pictureNumber);
  yield();

  // Reset I2C SCCB bus before camera init to clear any lock-up state
  initializeSCCBBus();
  delay(500);
  yield(); // Feed watchdog

  // Initialise camera at VGA — slave always uses VGA or smaller for serial transfers
  if (!initCamera(FRAMESIZE_VGA, 10)) {
    Serial.println("[CAM] FATAL: Camera init failed, restarting...");
    delay(1000);
    ESP.restart();
  }
  delay(500);
  yield(); // Feed watchdog

  // Initialize SD card for photo archival (non-critical; system continues if SD absent)
  initSDCard();
  delay(500);
  yield(); // Feed watchdog

  // Configure flash LED pin
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  // Initialize UART and SerialTransfer for communication with master
  Serial.println("[UART] Initializing UART communication with master...");
  if (!initSerialTransfer()) {
    Serial.println("[UART] FATAL: SerialTransfer init failed, restarting...");
    delay(1000);
    ESP.restart();
  }
  delay(500);
  yield(); // Feed watchdog

  // Send READY packet with metadata
  PhotoMetadata metadata;
  metadata.luxValue = 0; // Will be updated by sensor on master before photo capture
  metadata.isFallen = 0;
  metadata.photoWidth = 640; // VGA
  metadata.photoHeight = 480;
  metadata.timestamp = millis() / 1000; // System uptime in seconds

  delay(200);
  yield(); // Feed watchdog

  if (!sendReadyPacket(metadata)) {
    Serial.println("[SERIALTRANSFER] ERROR: Failed to send READY packet");
  }

  yield(); // Feed watchdog

  // Wait briefly for master awareness
  delay(300);
  yield(); // Feed watchdog

  Serial.println("[STATUS] Setup complete, waiting for photo command from master...");
  diagnosticUARTStatus();
  Serial.println("===========================================");
  yield(); // Feed watchdog one final time
  Serial.println("[INIT] Task watchdog active (10s timeout) for main loop");
}

// ==================== LOOP ====================
void loop() {
  // Feed watchdog timer to prevent resets
  yield();

  // Process incoming packets from master
  processIncomingPackets();

  // Check if master is still connected
  if (!isMasterAlive()) {
    static uint32_t lastWarning = 0;
    if (millis() - lastWarning > 10000) { // Warn every 10 seconds
      Serial.println("[WARN] Master disconnected (no packets for >5 seconds)");
      lastWarning = millis();
    }
  }

  delay(10);
}
