/**
 * @file      Lilygo+CamESPNOW.ino
 * @author    Modified for ESP-NOW camera control
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-06
 * @note      LilyGo A7670 Master — Controls ESP32CAM via ESP-NOW (wireless, no UART wires)
 *
 * Hardware Setup:
 *  - LILYGO_T_A7670 board (ESP32 Master)
 *  - PIR sensor output -> GPIO32 (wakeup trigger)
 *  - GPIO23 -> ESP32CAM power enable (LOW = ON, HIGH = OFF - inverted MOSFET logic)
 *  - Light sensor (e.g., BH1750) on I2C bus for LUX reading
 *
 * Module layout:
 *  utilities.h       — Board pin definitions (LILYGO_T_A7670)
 *  espnow_protocol.h — ESP-NOW packet types, CRC32, shared constants
 *  power_manager.h   — Camera GPIO control and deep-sleep entry
 *  espnow_master.h   — WiFi AP, ESP-NOW init, slave pairing, photo RX
 *  sensor.h          — Ambient light (LUX) reading
 */

#include "espnow_master.h"
#include "espnow_protocol.h"
#include "http_upload.h"
#include "power_manager.h"
#include "sensor.h"
#include "utilities.h"

#include <Wire.h>
#include <driver/gpio.h>

// ==================== PHOTO PARAMETERS ====================
#define PHOTO_WIDTH 640  // Requested capture width  (pixels)
#define PHOTO_HEIGHT 480 // Requested capture height (pixels)
#define PHOTO_QUALITY 10 // JPEG quality: 1–63 (lower = better quality)

// ==================== TIMING ====================
#define CAM_BOOT_TIME_MS 2500 // Camera power-on to WiFi-ready delay (ms)

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n===========================================");
  Serial.println("LILYGO A7670 - Camera ESP-NOW Master");
  Serial.println("===========================================");
  Serial.println("[PINS] GPIO23->CAM_POWER (LOW=ON), GPIO32->PIR_WAKEUP");
  Serial.println("[COMM] ESP-NOW wireless (no UART wires)");

  print_wakeup_reason();

  // Release GPIO hold left over from previous deep-sleep cycle
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)CAM_PWR_EN_PIN);

  // Configure camera power pin with maximum drive strength
  pinMode(CAM_PWR_EN_PIN, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)CAM_PWR_EN_PIN, GPIO_DRIVE_CAP_3);
  digitalWrite(CAM_PWR_EN_PIN, HIGH); // Camera OFF initially
  Serial.println("[GPIO] GPIO23 configured with maximum drive strength");

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

    // Start WiFi AP + ESP-NOW before powering camera so slave can find us
    initESPNOW();

    powerOnCamera();

    Serial.printf("[WAIT] Waiting %d ms for camera power-on...\n", CAM_BOOT_TIME_MS);
    delay(CAM_BOOT_TIME_MS);

    // Pass LUX value so the upload module can include it in metadata
    capturedLuxValue = luxValue;
    capturedIsFallen = isFallen;

    Serial.println("[ESPNOW] Waiting for camera READY signal...");
    if (waitForCameraReady()) {
      Serial.println("[ESPNOW] Camera is READY");

      bool photoSuccess = false;
      for (int attempt = 1; attempt <= PHOTO_MAX_RETRIES && !photoSuccess; attempt++) {
        if (attempt > 1) {
          Serial.printf("\n[RETRY] Attempt %d/%d\n", attempt, PHOTO_MAX_RETRIES);
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

        if (sendPhotoCommand(luxValue, PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY)) {
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

    delay(1000);
    powerOffCamera();
    cleanupESPNOW();

  } else {
    // First boot or manual reset — run a quick power-cycle smoke test
    Serial.println("\n[BOOT] First boot or reset detected");
    Serial.println("[TEST] Testing camera power control...");
    powerOnCamera();
    delay(1000);
    powerOffCamera();
    Serial.println("[TEST] Power control test completed");
  }

  // ==================== CONFIGURE AND ENTER DEEP SLEEP ====================
  pinMode(PIR_SENSOR_PIN, INPUT_PULLDOWN);
  gpio_pulldown_en((gpio_num_t)PIR_SENSOR_PIN);

  Serial.println("\n[CONFIG] System configuration:");
  Serial.println("  - Wakeup source: GPIO32 (PIR Sensor)");
  Serial.println("  - Camera power:  GPIO23 (LOW=ON, HIGH=OFF - inverted logic)");
  Serial.println("  - Communication: ESP-NOW (wireless, no UART wires)");
  Serial.printf("  - Master AP:     %s (channel %d)\n", MASTER_AP_SSID, ESPNOW_CHANNEL);
  Serial.printf("  - Photo size:    %dx%d, Quality: %d\n",
                PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY);

  enterDeepSleep();
}

// ==================== LOOP ====================
void loop() {
  // Never reached — device enters deep sleep in setup()
}
