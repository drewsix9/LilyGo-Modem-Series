/**
 * @file      Lilygo+CamESPNOW.ino
 * @author    Modified for SerialTransfer camera control
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-21
 * @note      LilyGo A7670 Master — Controls ESP32-CAM via UART + SerialTransfer (wired, reliable)
 *
 * Hardware Setup:
 *  - LILYGO_T_A7670 board (ESP32 Master)
 *  - PIR sensor output -> GPIO32 (wakeup trigger)
 *  - GPIO23 -> ESP32CAM power enable (LOW = ON, HIGH = OFF - inverted MOSFET logic)
 *  - Serial1 UART: GPIO18 (TX) → Slave RX, GPIO19 (RX) ← Slave TX
 *  - Light sensor (e.g., BH1750) on I2C bus for LUX reading
 *
 * Module layout:
 *  utilities.h           — Board pin definitions (LILYGO_T_A7670)
 *  serialtransfer_protocol.h — SerialTransfer packet types, metadata structs
 *  power_manager.h       — Camera GPIO control and deep-sleep entry
 *  serialtransfer_master.h   — UART init, slave discovery, photo RX
 *  sensor.h              — Ambient light (LUX) reading
 */

#include "http_upload.h"
#include "power_manager.h"
#include "sensor.h"
#include "serialtransfer_master.h"
#include "serialtransfer_protocol.h"
#include "utilities.h"

#include <Wire.h>
#include <driver/gpio.h>

// ==================== PHOTO PARAMETERS ====================
#define PHOTO_WIDTH 640  // Requested capture width  (pixels) — native VGA
#define PHOTO_HEIGHT 480 // Requested capture height (pixels) — native VGA
#define PHOTO_QUALITY 30 // JPEG quality: 1–63 (lower = better quality)

// ==================== TIMING ====================
#define CAM_BOOT_TIME_MS 2500    // Camera power-on to UART-ready delay (ms)
#define UART_RX_TIMEOUT_MS 10000 // Timeout for photo reception from slave

// ==================== UART TEST MODE ====================
#define UART_TEST_ENABLED false
#define UART_TEST_REPEAT_MODE true
#define UART_TEST_REPEAT_COUNT 10
#define UART_TEST_GAP_MS 1000

// ==================== QUICK UART TEST ====================
static void runUartLinkTest() {
  Serial.println("\n[TEST] Starting UART link test (GPIO18 TX, GPIO19 RX)...");

  uint16_t totalRuns = UART_TEST_REPEAT_MODE ? UART_TEST_REPEAT_COUNT : 1;
  uint16_t passCount = 0;

  for (uint16_t i = 0; i < totalRuns; i++) {
    Serial.printf("[TEST] Run %u/%u\n", i + 1, totalRuns);

    if (!initMasterSerialTransfer(18, 19)) {
      Serial.println("[TEST] FAIL: UART init failed");
      continue;
    }

    powerOnCamera();
    delay(CAM_BOOT_TIME_MS);

    bool ready = waitForSlaveReady(20000);
    if (ready) {
      passCount++;
      Serial.println("[TEST] PASS: Slave READY received over UART");
    } else {
      Serial.println("[TEST] FAIL: No READY from slave (check TX/RX and GND)");
      diagnosticMasterUARTStatus();
    }

    powerOffCamera();

    if (i + 1 < totalRuns) {
      delay(UART_TEST_GAP_MS);
    }
  }

  Serial.printf("[TEST] Summary: %u/%u PASS\n", passCount, totalRuns);
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n===========================================");
  Serial.println("LILYGO A7670 - Camera SerialTransfer Master");
  Serial.println("===========================================");
  Serial.println("[PINS] GPIO23->CAM_POWER (LOW=ON), GPIO32->PIR_WAKEUP");
  Serial.println("[COMM] SerialTransfer UART (wired: GPIO18 TX, GPIO19 RX)");

  print_wakeup_reason();

  // Release GPIO hold left over from previous deep-sleep cycle
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)CAM_PWR_EN_PIN);

  // Configure camera power pin with maximum drive strength
  pinMode(CAM_PWR_EN_PIN, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)CAM_PWR_EN_PIN, GPIO_DRIVE_CAP_3);
  digitalWrite(CAM_PWR_EN_PIN, HIGH); // Camera OFF initially
  Serial.println("[GPIO] GPIO23 configured with maximum drive strength");

  // Log initial memory status
  Serial.printf("[MEMORY] Initial: Free PSRAM: %u bytes, Free heap: %u bytes\n",
                ESP.getFreePsram(), ESP.getFreeHeap());

  // Start I2C for light sensor
  Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);
  bool sensorsReady = initSensors();
  Serial.printf("[SENSOR] Sensor init status: %s\n", sensorsReady ? "OK" : "PARTIAL/FAILED");

  // ==================== HANDLE WAKEUP EVENT ====================
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("\n[EVENT] Motion detected by PIR sensor!");

    uint16_t luxValue = readLuxSensor();
    Bmi160Reading bmiSample = {0, 0, 0, 0, 0, 0};
    bool bmiReadOk = readBmi160Raw(bmiSample);
    bool isFallen = false;
    if (bmiReadOk) {
      isFallen = detectFallFromBmi160(bmiSample);
    }

    Serial.printf("[SENSOR] Current light intensity: %d LUX\n", luxValue);
    Serial.print("[SENSOR] Fall status: ");
    Serial.println(isFallen ? "FALLEN" : "NOT FALLEN");

    // Log memory before camera power on
    Serial.printf("[MEMORY] Before camera: Free PSRAM: %u bytes, Free heap: %u bytes\n",
                  ESP.getFreePsram(), ESP.getFreeHeap());

    // Initialize UART and SerialTransfer communication with slave
    Serial.println("[UART] Initializing UART communication with camera slave...");
    if (!initMasterSerialTransfer(18, 19)) { // GPIO18 TX, GPIO19 RX
      Serial.println("[UART] ERROR: Failed to init master UART, restarting...");
      delay(1000);
      ESP.restart();
    }
    delay(500);

    powerOnCamera();

    Serial.printf("[WAIT] Waiting %d ms for camera power-on...\n", CAM_BOOT_TIME_MS);
    delay(CAM_BOOT_TIME_MS);

    Serial.println("[UART] Waiting for slave READY signal (ASCII)...");
    if (!waitForSlaveReady(UART_RX_TIMEOUT_MS)) {
      Serial.println("[UART] ERROR: Slave READY timeout!");
      powerOffCamera();
      enterDeepSleep();
    }

    Serial.println("[UART] Slave is READY");

    // Send ASCII PHOTO command to slave
    // Format: "PHOTO:width:height:quality\n"
    Serial.printf("[UART] Sending PHOTO command: %dx%d quality=%d\n",
                  PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY);
    if (!sendPhotoCommand(PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY)) {
      Serial.println("[UART] ERROR: Failed to send PHOTO command");
      powerOffCamera();
      enterDeepSleep();
    }

    // Give slave immediate time to start capture and transmission
    // Don't wait 500ms - the slave will send PHOTO_HEADER as soon as it's ready
    // SerialTransfer.tick() handles any timing jitter properly
    delay(100);

    Serial.println("[UART] Receiving photo chunks from slave...");
    Serial.printf("[MEMORY] Before photo RX: Free PSRAM: %u bytes, Free heap: %u bytes\n",
                  ESP.getFreePsram(), ESP.getFreeHeap());

    uint8_t *photo = nullptr;
    uint32_t photoSize = 0;

    if (!receivePhotoChunked(photo, photoSize)) {
      Serial.println("[UART] ERROR: Photo reception failed");
      powerOffCamera();
      freePhotoBuffer();
      enterDeepSleep();
    }

    Serial.printf("[MEMORY] After photo RX: Free PSRAM: %u bytes, Free heap: %u bytes\n",
                  ESP.getFreePsram(), ESP.getFreeHeap());

    Serial.printf("[SUCCESS] Photo received: %lu bytes\n", photoSize);

    // Process photo and upload (photo buffer is already allocated)

    powerOffCamera();
    freePhotoBuffer();

  } else {
    // First boot or manual reset — run a quick power-cycle smoke test
    Serial.println("\n[BOOT] First boot or reset detected");
    Serial.println("[TEST] Testing camera power control...");
    powerOnCamera();
    delay(1000);
    powerOffCamera();
    Serial.println("[TEST] Power control test completed");

    // UART wiring/handshake check is compile-time toggleable.
    if (UART_TEST_ENABLED) {
      runUartLinkTest();
    } else {
      Serial.println("[TEST] UART link test disabled (UART_TEST_ENABLED=false)");
    }
  }

  // ==================== CONFIGURE AND ENTER DEEP SLEEP ====================
  pinMode(PIR_SENSOR_PIN, INPUT_PULLDOWN);
  gpio_pulldown_en((gpio_num_t)PIR_SENSOR_PIN);

  Serial.println("\n[CONFIG] System configuration:");
  Serial.println("  - Wakeup source: GPIO32 (PIR Sensor)");
  Serial.println("  - Camera power:  GPIO23 (LOW=ON, HIGH=OFF - inverted logic)");
  Serial.println("  - Communication: SerialTransfer UART (wired, GPIO18/GPIO19)");
  Serial.printf("  - Photo size:    %dx%d\n", PHOTO_WIDTH, PHOTO_HEIGHT);
  Serial.printf("[MEMORY] Before sleep: Free PSRAM: %u bytes, Free heap: %u bytes\n",
                ESP.getFreePsram(), ESP.getFreeHeap());

  diagnosticMasterUARTStatus();
  enterDeepSleep();
}

// ==================== LOOP ====================
void loop() {
  // Never reached — device enters deep sleep in setup()
}
