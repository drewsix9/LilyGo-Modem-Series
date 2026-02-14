/**
 * @file      Lilygo+CamUART.ino
 * @author    Modified for UART camera control with LUX sensor
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-02-10
 * @note      LilyGo A7670 Master - Controls ESP32CAM via UART with light-adaptive flash
 *
 * Hardware Setup:
 *  - LILYGO_T_A7670 board (ESP32 Master)
 *  - PIR sensor output → GPIO32 (wakeup trigger)
 *  - GPIO23 → ESP32CAM power enable (HIGH = ON)
 *  - GPIO18 (TX) → ESP32CAM GPIO16 (RX)
 *  - GPIO19 (RX) → ESP32CAM GPIO13 (TX)
 *  - Light sensor (e.g., BH1750) on I2C bus for LUX reading
 *
 * Communication Protocol:
 *  Command: "PHOTO:lux:width:height:quality\n"
 *  Example: "PHOTO:500:1600:1200:10\n"
 *  Responses: "READY\n", "OK:filename.jpg\n", "ERROR:msg\n"
 *
 * CRITICAL NOTE:
 *  GPIO1/GPIO3 are used by Serial (USB) and CANNOT be used for HardwareSerial!
 *  Using UART2 (GPIO18/19) instead to avoid boot loops.
 *
 * Workflow:
 *  1. PIR detects motion → ESP32 wakes from deep sleep
 *  2. Powers ON ESP32CAM via GPIO23
 *  3. Reads LUX value from light sensor
 *  4. Waits for "READY" from camera
 *  5. Sends PHOTO command with parameters
 *  6. Waits for "OK" confirmation
 *  7. Powers OFF camera
 *  8. Returns to deep sleep
 */

#include "utilities.h"
#include <Wire.h>
#include <driver/gpio.h>

// ==================== PIN DEFINITIONS ====================
#define PIR_SENSOR_PIN 32 // PIR sensor wakeup source
#define CAM_PWR_EN_PIN 23 // Camera power enable (HIGH = ON)
#define UART_TX_PIN 18    // Free GPIO for TX to camera (avoid GPIO1,16,17)
#define UART_RX_PIN 19    // Free GPIO for RX from camera (avoid GPIO3,16,17)

// ==================== I2C LIGHT SENSOR ====================
// If using BH1750 or similar, uncomment and configure:
// #include <BH1750.h>
// BH1750 lightMeter;
#define I2C_SDA 21 // Default I2C SDA
#define I2C_SCL 22 // Default I2C SCL

// ==================== UART CONFIGURATION ====================
#define UART_BAUD_RATE 115200
#define UART_TIMEOUT_MS 10000 // Timeout waiting for camera response (increased for boot time)
#define CAM_BOOT_TIME_MS 500  // Initial power-on delay (reduced, camera sends status as it boots)

// ==================== PHOTO PARAMETERS ====================
#define PHOTO_WIDTH 1600  // Image width in pixels
#define PHOTO_HEIGHT 1200 // Image height in pixels
#define PHOTO_QUALITY 10  // JPEG quality (1-63, lower = higher quality)

// ==================== TIMING ====================
#define CAMERA_ON_DURATION_MS 15000 // Max time camera stays on (15 seconds)
#define PIR_SETTLE_TIME_MS 2000     // PIR sensor settle time

// UART Serial for camera communication
HardwareSerial CameraSerial(2); // Use UART2 (GPIO16/17)

// ==================== FUNCTION PROTOTYPES ====================
void print_wakeup_reason();
uint16_t readLuxSensor();
bool waitForCameraReady();
bool sendPhotoCommand(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality);
String readCameraResponse(uint32_t timeout_ms);
void powerOnCamera();
void powerOffCamera();
void enterDeepSleep();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200); // Debug console
  delay(100);

  Serial.println("\n===========================================");
  Serial.println("LILYGO A7670 - Camera UART Master");
  Serial.println("===========================================");
  Serial.println("[PINS] GPIO18(TX)->CAM_RX, GPIO19(RX)->CAM_TX");
  Serial.println("[PINS] GPIO23->CAM_POWER, GPIO32->PIR_WAKEUP");

  // Print wakeup reason
  print_wakeup_reason();

  // Initialize camera power pin
  pinMode(CAM_PWR_EN_PIN, OUTPUT);
  digitalWrite(CAM_PWR_EN_PIN, LOW); // Start with camera OFF

  // Initialize I2C for light sensor
  Wire.begin(I2C_SDA, I2C_SCL);

  // Uncomment if using BH1750 or similar I2C light sensor:
  // if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
  //   Serial.println("[I2C] Light sensor initialized");
  // } else {
  //   Serial.println("[I2C] ERROR: Light sensor not found!");
  // }

  // Initialize UART to camera
  CameraSerial.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.printf("[UART] Initialized on TX:%d RX:%d @ %d baud\n",
                UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);

  // ==================== HANDLE WAKEUP EVENT ====================
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("\n[EVENT] Motion detected by PIR sensor!");

    // Read light intensity
    uint16_t luxValue = readLuxSensor();
    Serial.printf("[SENSOR] Current light intensity: %d LUX\n", luxValue);

    // Power ON camera
    powerOnCamera();

    // Wait for camera to start booting (reduced delay, UART sends status during init)
    Serial.printf("[WAIT] Waiting %d ms for camera power-on...\n", CAM_BOOT_TIME_MS);
    delay(CAM_BOOT_TIME_MS);

    // Wait for camera READY signal (camera needs ~3-4 seconds to init SD+Camera)
    Serial.println("[UART] Waiting for camera READY signal...");
    if (waitForCameraReady()) {
      Serial.println("[UART] Camera is READY");

      // Send photo command with parameters
      bool success = sendPhotoCommand(luxValue, PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY);

      if (success) {
        Serial.println("[SUCCESS] Photo captured successfully!");
      } else {
        Serial.println("[ERROR] Photo capture failed!");
      }

    } else {
      Serial.println("[ERROR] Camera READY timeout!");
    }

    // Keep camera on a bit longer for any final operations
    delay(1000);

    // Power OFF camera
    powerOffCamera();

  } else {
    // First boot or reset - just test the hardware
    Serial.println("\n[BOOT] First boot or reset detected");
    Serial.println("[TEST] Testing camera power control...");

    // Quick power test
    powerOnCamera();
    delay(1000);
    powerOffCamera();

    Serial.println("[TEST] Power control test completed");
  }

  // ==================== PREPARE FOR DEEP SLEEP ====================
  // Configure PIR sensor pin for wakeup
  pinMode(PIR_SENSOR_PIN, INPUT_PULLDOWN);
  gpio_pulldown_en((gpio_num_t)PIR_SENSOR_PIN);

  Serial.println("\n[CONFIG] System configuration:");
  Serial.println("  - Wakeup source: GPIO32 (PIR Sensor)");
  Serial.println("  - Camera power: GPIO23 (HIGH = ON)");
  Serial.println("  - UART: GPIO18(TX) → CAM_GPIO16(RX), GPIO19(RX) ← CAM_GPIO13(TX)");
  Serial.printf("  - Photo size: %dx%d, Quality: %d\n",
                PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY);
  Serial.printf("  - UART timeout: %d ms (allows ~%ds for camera init)\n",
                UART_TIMEOUT_MS, UART_TIMEOUT_MS / 1000);

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
 * @note Replace with actual sensor reading code
 */
uint16_t readLuxSensor() {
  // TODO: Replace with actual light sensor reading
  // Example for BH1750:
  // float lux = lightMeter.readLightLevel();
  // return (uint16_t)lux;

  // For testing, return a simulated value based on time
  // In production, replace this with real sensor reading
  uint16_t simulatedLux = 500; // Medium brightness

  // Uncomment when BH1750 is connected:
  // if (lightMeter.measurementReady()) {
  //   simulatedLux = (uint16_t)lightMeter.readLightLevel();
  // }

  return simulatedLux;
}

/**
 * @brief Power ON the camera
 */
void powerOnCamera() {
  Serial.println("[POWER] Turning camera ON (GPIO23 → HIGH)");
  digitalWrite(CAM_PWR_EN_PIN, HIGH);
  Serial.println("[STATUS] Camera powered ON");
}

/**
 * @brief Power OFF the camera
 */
void powerOffCamera() {
  Serial.println("[POWER] Turning camera OFF (GPIO23 → LOW)");
  digitalWrite(CAM_PWR_EN_PIN, LOW);
  Serial.println("[STATUS] Camera powered OFF");
}

/**
 * @brief Wait for "READY" message from camera
 * @return true if READY received, false on timeout
 */
bool waitForCameraReady() {
  uint32_t startTime = millis();
  String response = "";

  while (millis() - startTime < UART_TIMEOUT_MS) {
    if (CameraSerial.available()) {
      char c = CameraSerial.read();
      response += c;

      if (response.endsWith("READY\n")) {
        return true;
      }

      // Clear buffer if it gets too long
      if (response.length() > 100) {
        response = response.substring(response.length() - 50);
      }
    }
    delay(10);
  }

  Serial.printf("[TIMEOUT] No READY received (got: %s)\n", response.c_str());
  return false;
}

/**
 * @brief Send photo command to camera with parameters
 * @param lux Light intensity value for flash adjustment
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param quality JPEG quality (1-63, lower = better)
 * @return true if OK received, false otherwise
 */
bool sendPhotoCommand(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality) {
  // Build command string
  char command[64];
  snprintf(command, sizeof(command), "PHOTO:%d:%d:%d:%d\n",
           lux, width, height, quality);

  Serial.printf("[UART] Sending command: %s", command);
  CameraSerial.print(command);
  CameraSerial.flush();

  // Wait for response
  Serial.println("[UART] Waiting for confirmation...");
  String response = readCameraResponse(UART_TIMEOUT_MS);

  if (response.startsWith("OK:")) {
    String filename = response.substring(3);
    filename.trim();
    Serial.printf("[SUCCESS] Photo saved as: %s\n", filename.c_str());
    return true;
  } else if (response.startsWith("ERROR:")) {
    String error = response.substring(6);
    error.trim();
    Serial.printf("[ERROR] Camera error: %s\n", error.c_str());
    return false;
  } else {
    Serial.printf("[ERROR] Unexpected response: %s\n", response.c_str());
    return false;
  }
}

/**
 * @brief Read response from camera with timeout
 * @param timeout_ms Timeout in milliseconds
 * @return Response string (empty if timeout)
 */
String readCameraResponse(uint32_t timeout_ms) {
  uint32_t startTime = millis();
  String response = "";

  while (millis() - startTime < timeout_ms) {
    if (CameraSerial.available()) {
      char c = CameraSerial.read();
      response += c;

      // Check for complete response (ends with newline)
      if (c == '\n') {
        return response;
      }

      // Prevent overflow
      if (response.length() > 200) {
        break;
      }
    }
    delay(10);
  }

  return response;
}

/**
 * @brief Enter ESP32 deep sleep mode and wait for PIR trigger
 */
void enterDeepSleep() {
  Serial.println("\n[WAIT] PIR sensor settling...");
  delay(PIR_SETTLE_TIME_MS);

  Serial.println("[SLEEP] Entering deep sleep mode...");
  Serial.println("         Waiting for PIR motion detection...");
  Serial.println("===========================================\n");
  Serial.flush();

  // Enable wakeup on GPIO32 (PIR sensor HIGH)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_SENSOR_PIN, 1);

  delay(200);
  esp_deep_sleep_start();
}
