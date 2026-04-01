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

// Macro to properly feed the ESP32 task watchdog
#define FEED_WATCHDOG() esp_task_wdt_reset()

// ==================== EEPROM ====================
#define EEPROM_SIZE 1

// ==================== GLOBALS ====================
uint8_t pictureNumber = 1;
static uint32_t lastReadySentTime = 0;   // Track READY transmission for periodic resends
static bool inPhotoTransmission = false; // Flag to silence READY during photo TX

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
  FEED_WATCHDOG();

  // Reset I2C SCCB bus before camera init to clear any lock-up state
  initializeSCCBBus();
  delay(500);
  FEED_WATCHDOG(); // Feed watchdog

  // Initialise camera at VGA — slave always uses VGA or smaller for serial transfers
  if (!initCamera(FRAMESIZE_VGA, 10)) {
    Serial.println("[CAM] FATAL: Camera init failed, restarting...");
    delay(1000);
    ESP.restart();
  }
  delay(500);
  FEED_WATCHDOG(); // Feed watchdog

  // Initialize SD card for photo archival (non-critical; system continues if SD absent)
  initSDCard();
  delay(500);
  FEED_WATCHDOG(); // Feed watchdog

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
  FEED_WATCHDOG(); // Feed watchdog

  // Send ASCII READY packet with metadata
  // Format: "READY:lux:fall:timestamp:width:height\n"
  uint16_t lux = 0; // Will be read by master sensor, not slave
  uint8_t isFallen = 0;
  uint16_t photoWidth = 640; // VGA
  uint16_t photoHeight = 480;
  uint32_t timestamp = millis() / 1000; // System uptime in seconds

  delay(200);
  FEED_WATCHDOG(); // Feed watchdog

  if (!sendReadyPacket(lux, isFallen, photoWidth, photoHeight, timestamp)) {
    Serial.println("[SERIALTRANSFER] ERROR: Failed to send READY packet");
  }

  FEED_WATCHDOG(); // Feed watchdog

  // Wait briefly for master awareness
  delay(300);
  FEED_WATCHDOG(); // Feed watchdog

  Serial.println("[STATUS] Setup complete, waiting for PHOTO command from master...");
  diagnosticUARTStatus();
  Serial.println("===========================================");
  FEED_WATCHDOG(); // Feed watchdog one final time
  Serial.println("[INIT] Task watchdog active (10s timeout) for main loop");
  FEED_WATCHDOG(); // Critical: feed before entering main loop
}

// ==================== LOOP ====================
void loop() {
  // CRITICAL: Feed watchdog timer regularly to prevent resets
  FEED_WATCHDOG();

  // Periodically send READY to master (every 500ms) until we get a command
  // This ensures master discovers us even if it missed the initial READY
  // CRITICAL: Do NOT send READY during photo transmission (would corrupt binary stream)
  static uint32_t lastReadyTime = 0;
  if (!inPhotoTransmission && millis() - lastReadyTime > 500) {
    uint16_t lux = 0;
    uint8_t isFallen = 0;
    uint16_t photoWidth = 640;
    uint16_t photoHeight = 480;
    uint32_t timestamp = millis() / 1000;

    sendReadyPacket(lux, isFallen, photoWidth, photoHeight, timestamp);
    lastReadyTime = millis();
  }

  // Check for incoming ASCII commands from master
  if (commandAvailable()) {
    PhotoCommand cmd;
    if (getCommand(cmd)) {
      Serial.printf("[CMD] Received command: %s\n", cmd.cmdName);

      if (strcmp(cmd.cmdName, CMD_PHOTO) == 0) {
        // Master is requesting a photo capture
        Serial.printf("[CMD] PHOTO command: %ux%u quality=%u\n",
                      cmd.width, cmd.height, cmd.quality);

        // Capture photo at requested resolution and quality
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
          // Note: actual resolution may differ; update metadata accordingly
          uint32_t photoSize = fb->len;
          const uint8_t *photoBuffer = fb->buf;

          Serial.printf("[CAM] Photo captured: %lu bytes\n", photoSize);

          // Send photo via SerialTransfer (binary chunks)
          if (!sendPhotoChunked(photoBuffer, photoSize, cmd.quality)) {
            Serial.println("[CAM] ERROR: Photo transmission failed");
            sendErrorMessage(ERR_CAMERA_FAIL);
          } else {
            Serial.println("[CAM] Photo transmission complete");
          }

          // Return the frame buffer
          esp_camera_fb_return(fb);
        } else {
          Serial.println("[CAM] ERROR: Failed to capture photo");
          sendErrorMessage(ERR_CAMERA_FAIL);
        }

        FEED_WATCHDOG(); // Feed watchdog after heavy operation

      } else if (strcmp(cmd.cmdName, CMD_STAT) == 0) {
        // Master is requesting status
        Serial.println("[CMD] STAT command received");
        sendAckMessage(0); // ACK with status 0 (ok)

      } else if (strcmp(cmd.cmdName, CMD_PING) == 0) {
        // Master is checking connection
        Serial.println("[CMD] PING command received");
        sendAckMessage(0);

      } else if (strcmp(cmd.cmdName, CMD_RESET) == 0) {
        // Master is requesting reset
        Serial.println("[CMD] RESET command received");
        ESP.restart();
      }
    }
  }

  // Check if master is still connected
  if (!isMasterAlive()) {
    static uint32_t lastWarning = 0;
    if (millis() - lastWarning > 1000) { // Warn every 10 seconds
      Serial.println("[WARN] Master disconnected (no commands for >5 seconds)");
      lastWarning = millis();
      FEED_WATCHDOG(); // Feed during long timeout activity
    }
  } else {
    // No command available; avoid busy-looping by yielding a small delay
    delay(10);
  }

  FEED_WATCHDOG(); // Feed watchdog at end of loop iteration
}
