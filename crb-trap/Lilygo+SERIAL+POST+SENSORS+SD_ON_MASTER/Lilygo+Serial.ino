/**
 * @file      Lilygo+CamESPNOW.ino
 * @author    Modified for UART hybrid camera control
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-06
 * @note      LilyGo A7670 Master — Controls ESP32CAM via UART (ASCII + SerialTransfer)
 *
 * Hardware Setup:
 *  - LILYGO_T_A7670 board (ESP32 Master)
 *  - PIR sensor output -> GPIO32 (wakeup trigger)
 *  - GPIO23 -> ESP32CAM power enable (LOW = ON, HIGH = OFF - inverted MOSFET logic)
 *  - Light sensor (e.g., BH1750) on I2C bus for LUX reading
 *
 * Module layout:
 *  utilities.h       — Board pin definitions (LILYGO_T_A7670)
 *  uart_protocol.h — Shared UART protocol constants and CRC32
 *  power_manager.h   — Camera GPIO control and deep-sleep entry
 *  uart_master.h   — UART init, ASCII command flow, binary photo RX
 *  sensor.h          — Ambient light (LUX) reading
 */

#include "ESP32CAM_Slave/sdcard.h"
#include "gps_reader.h"
#include "gps_storage.h"
#include "http_upload.h"
#include "power_manager.h"
#include "sensor.h"
#include "servo_controller.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "uart_master.h"
#include "uart_protocol.h"
#include "utilities.h"

#include <EEPROM.h>
#include <Wire.h>
#include <driver/gpio.h>

// ==================== PHOTO PARAMETERS ====================
#define PHOTO_WIDTH 640  // Requested capture width  (pixels) — native VGA
#define PHOTO_HEIGHT 480 // Requested capture height (pixels) — native VGA
#define PHOTO_QUALITY 30 // JPEG quality: 1–63 (lower = better quality)

// ==================== TIMING ====================
#define CAM_BOOT_TIME_MS 2500 // Camera power-on to WiFi-ready delay (ms)

// ==================== SERVO CONFIGURATION ====================
#define SERVO_PIN 13 // GPIO13 for servo control (configurable)
#define SERVO_MIN_PULSE_US 500
#define SERVO_MAX_PULSE_US 2400

// ==================== SERVO HELPER FUNCTION ====================
void executeServoAction(const char *servo_action, uint8_t servo_angle) {
  if (!ServoController::attached()) {
    Serial.println("[SERVO] Servo not initialized, skipping action");
    return;
  }

  if (strcmp(servo_action, "servo_male") == 0) {
    Serial.printf("[SERVO] Executing MALE action: moving to 45°\n");
    ServoController::writeAngle(45);
  } else if (strcmp(servo_action, "servo_female") == 0) {
    Serial.printf("[SERVO] Executing FEMALE action: moving to 135°\n");
    ServoController::writeAngle(135);
  } else if (strcmp(servo_action, "servo_neutral") == 0) {

    Serial.printf("[SERVO] Executing NEUTRAL action: moving to 90°\n");
    ServoController::writeAngle(90);
  } else if (strcmp(servo_action, "no_action") == 0) {
    Serial.println("[SERVO] No beetles detected, servo remains at current position");
  } else {
    Serial.printf("[SERVO] Unknown action: %s, moving to provided angle: %d°\n",
                  servo_action, servo_angle);
    ServoController::writeAngle(servo_angle);
  }

  delay(500); // Allow servo time to reach target position
}

// ==================== GLOBALS FOR API RESPONSE DATA ====================
// These are populated by http_upload.h after receiving API response
char g_servo_action[32] = "no_action";
uint8_t g_servo_angle = 90;
uint16_t luxValue;
Bmi160Reading bmiSample;
bool bmiReadOk;
bool isFallen;

// ==================== SETUP ====================
void setup() {

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  // delay(100);

  Serial.println("\n===========================================");
  Serial.println("LILYGO A7670 - Camera UART Master");
  Serial.println("===========================================");
  Serial.println("[PINS] GPIO23->CAM_POWER (LOW=ON), GPIO32->PIR_WAKEUP");
  Serial.println("[COMM] UART hybrid (ASCII command + binary chunks)");

  print_wakeup_reason();

  // Release GPIO hold left over from previous deep-sleep cycle
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)CAM_PWR_EN_PIN);

  // Configure camera power pin with maximum drive strength
  pinMode(CAM_PWR_EN_PIN, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)CAM_PWR_EN_PIN, GPIO_DRIVE_CAP_3);
  digitalWrite(CAM_PWR_EN_PIN, HIGH); // Camera OFF initially
  // Serial.println("[GPIO] GPIO23 configured with maximum drive strength");

  // Log initial memory status
  // Serial.printf("[MEMORY] Initial: Free PSRAM: %u bytes, Free heap: %u bytes\n",
  //               ESP.getFreePsram(), ESP.getFreeHeap());

  // Start I2C for light sensor
  Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);
  bool sensorsReady = initSensors();
  // Serial.printf("[SENSOR] Sensor init status: %s\n", sensorsReady ? "OK" : "PARTIAL/FAILED");

  // SD module uses EEPROM counter bytes [0..1] for rolling filenames.
  // GPS storage uses bytes [2..10] for cached position + validity flag.
  EEPROM.begin(16);

  // Initialize SD card on master for archiving CRC-verified received photos.
  // bool sdReady = initSDCard();
  // Serial.printf("[SD] Master SD archive status: %s\n", sdReady ? "READY" : "UNAVAILABLE");

  // Initialize servo for sex-based separation
  bool servoReady = ServoController::begin(SERVO_PIN, SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US, 90);
  Serial.printf("[SERVO] Servo init status: %s (pin %d, start angle 90°)\n",
                servoReady ? "OK" : "FAILED", SERVO_PIN);

  // ==================== INITIALIZE GPS ON COLD BOOT ONLY ====================
  // If this is cold boot (not PIR deep-sleep wakeup), read GPS and cache it.
  // GPS stays OFF during PIR wakeup cycles to save power.
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) { // Not PIR wakeup, so likely cold boot or manual reset
    Serial.println("\n[BOOT] Cold boot detected, initializing GPS for metadata...");
    // Initialize modem first (required for GPS reads)
    if (initModem()) {
      // initGPS(); // Populates g_gps_lat, g_gps_lon, g_gps_valid with 60s timeout
      Serial.printf("[GPS] Populated globals: lat=%.6f, lon=%.6f, valid=%s\n",
                    g_gps_lat, g_gps_lon, g_gps_valid ? "true" : "false");
    } else {
      Serial.println("[GPS] Modem init failed, using EEPROM cache or defaults");
      // Fallback: Try EEPROM cache or use defaults
      float lat, lon;
      if (!loadGPSFromEEPROM(lat, lon)) {
        lat = atof(DEFAULT_GPS_LAT);
        lon = atof(DEFAULT_GPS_LON);
        g_gps_valid = false;
      } else {
        g_gps_valid = true;
      }
      g_gps_lat = lat;
      g_gps_lon = lon;
      Serial.printf("[GPS] Fallback globals: lat=%.6f, lon=%.6f, valid=%s\n",
                    g_gps_lat, g_gps_lon, g_gps_valid ? "true" : "false");
    }

    // Put modem to sleep before entering deep sleep
    sleepModem();

    // ==================== CONFIGURE AND ENTER DEEP SLEEP ====================
    // Configure PIR sensor as INPUT (no pullup/pulldown — PIR drives signal strongly)
    pinMode(PIR_SENSOR_PIN, INPUT);
    Serial.printf("[SETUP] PIR sensor GPIO%d configured as INPUT for EXT0 wakeup\n", PIR_SENSOR_PIN);
    Serial.println("[SETUP] PIR wakeup will trigger on HIGH signal (motion detected)");

    // Reset servo to neutral (90°) before sleep
    if (ServoController::attached()) {
      Serial.println("[SERVO] Resetting servo to neutral (90°) before sleep...");
      ServoController::writeAngle(90);
      delay(500);
      Serial.println("[SERVO] Servo reset to neutral complete");
    }

    // Serial.println("\n[CONFIG] System configuration:");
    // Serial.println("  - Wakeup source: GPIO32 (PIR Sensor)");
    // Serial.println("  - Camera power:  GPIO23 (LOW=ON, HIGH=OFF - inverted logic)");
    // Serial.println("  - Communication: UART (GPIO18 TX -> GPIO16 RX, GPIO19 RX <- GPIO4 TX)");
    // Serial.printf("  - Photo size:    %dx%d, Quality: %d\n",
    //               PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY);
    // Serial.printf("[MEMORY] Before sleep: Free PSRAM: %u bytes, Free heap: %u bytes\n",
    //               ESP.getFreePsram(), ESP.getFreeHeap());

    enterDeepSleep();
    Serial.println("FINISHED BOOT. Awaiting PIR trigger...");
  }

  // ==================== HANDLE WAKEUP EVENT ====================
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("\n[BOOT] PIR wakeup detected, waking modem...");
    // if (!wakeupModem()) {
    //   goto deepsleep;
    // }

    Serial.println("\n[EVENT] Motion detected by PIR sensor!");

    luxValue = readLuxSensor();
    bmiSample = {0, 0, 0, 0, 0, 0};
    bmiReadOk = readBmi160Raw(bmiSample);
    isFallen = false;
    if (bmiReadOk) {
      isFallen = detectFallFromBmi160(bmiSample);
    }

    Serial.printf("[SENSOR] Current light intensity: %d LUX\n", luxValue);
    Serial.print("[SENSOR] Fall status: ");
    Serial.println(isFallen ? "FALLEN" : "NOT FALLEN");

    // Log memory before camera power on
    Serial.printf("[MEMORY] Before camera: Free PSRAM: %u bytes, Free heap: %u bytes\n",
                  ESP.getFreePsram(), ESP.getFreeHeap());

    // Initialize UART transport before powering camera
    initUART();

    powerOnCamera();

    Serial.printf("[WAIT] Waiting %d ms for camera power-on...\n", CAM_BOOT_TIME_MS);
    delay(CAM_BOOT_TIME_MS);

    // Pass LUX value so the upload module can include it in metadata
    capturedLuxValue = luxValue;
    capturedIsFallen = isFallen;

    Serial.println("[UART] Waiting for camera READY signal...");
    if (waitForCameraReady()) {
      Serial.println("[UART] Camera is READY");

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
          Serial.printf("[MEMORY] Before photo RX: Free PSRAM: %u bytes, Free heap: %u bytes\n",
                        ESP.getFreePsram(), ESP.getFreeHeap());
          Serial.println("[SUCCESS] Photo command sent, receiving photo...");
          photoSuccess = receivePhoto();
          Serial.printf("[MEMORY] After photo RX: Free PSRAM: %u bytes, Free heap: %u bytes\n",
                        ESP.getFreePsram(), ESP.getFreeHeap());
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
      } else {
        // Execute servo action based on API response (populated by http_upload module)
        Serial.printf("[SERVO] Executing action from API: '%s' (angle: %d°)\n",
                      g_servo_action, g_servo_angle);
        executeServoAction(g_servo_action, g_servo_angle);
      }

    } else {
      Serial.println("[ERROR] Camera READY timeout!");
    }

    delay(1000);
    powerOffCamera();
  deepsleep:
    cleanupUART();

    // Put modem back to sleep before entering deep sleep
    sleepModem();
  } else {
    // // First boot or manual reset — run a quick power-cycle smoke test
    // Serial.println("\n[BOOT] First boot or reset detected");
    // Serial.println("[TEST] Testing camera power control...");
    // powerOnCamera();
    // delay(1000);
    // powerOffCamera();
    // Serial.println("[TEST] Power control test completed");
  }

  // ==================== CONFIGURE AND ENTER DEEP SLEEP ====================
  // Configure PIR sensor as INPUT (no pullup/pulldown — PIR drives signal strongly)
  pinMode(PIR_SENSOR_PIN, INPUT);
  Serial.printf("[SLEEP] PIR sensor GPIO%d configured as INPUT for EXT0 wakeup\n", PIR_SENSOR_PIN);
  Serial.println("[SLEEP] PIR wakeup will trigger on HIGH signal (motion detected)");

  // Reset servo to neutral (90°) before sleep
  if (ServoController::attached()) {
    Serial.println("[SERVO] Resetting servo to neutral (90°) before sleep...");
    ServoController::writeAngle(90);
    delay(500);
    Serial.println("[SERVO] Servo reset to neutral complete");
  }

  // Serial.println("\n[CONFIG] System configuration:");
  // Serial.println("  - Wakeup source: GPIO32 (PIR Sensor)");
  // Serial.println("  - Camera power:  GPIO23 (LOW=ON, HIGH=OFF - inverted logic)");
  // Serial.println("  - Communication: UART (GPIO18 TX -> GPIO16 RX, GPIO19 RX <- GPIO4 TX)");
  // Serial.printf("  - Photo size:    %dx%d, Quality: %d\n",
  //               PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY);
  // Serial.printf("[MEMORY] Before sleep: Free PSRAM: %u bytes, Free heap: %u bytes\n",
  //               ESP.getFreePsram(), ESP.getFreeHeap());

  enterDeepSleep();
}

// ==================== LOOP ====================
void loop() {
  // Never reached — device enters deep sleep in setup()
}
