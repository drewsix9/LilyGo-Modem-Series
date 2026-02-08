/**
 * @file      Lilygo+CamWakeUpByExt0.ino
 * @author    Modified for camera power control test
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-02-08
 * @note      This example tests camera power control via GPIO23 on LILYGO_T_A7670.
 *            ESP32 wakes from deep sleep using PIR sensor on GPIO32, powers on
 *            the slave ESP32 camera via GPIO23 for 10 seconds, then returns to deep sleep.
 *
 * Hardware Setup:
 *  - LILYGO_T_A7670 board (ESP32 variant)
 *  - PIR sensor output connected to GPIO32 (wakeup trigger)
 *  - GPIO23 connected to slave ESP32 camera power enable (HIGH = ON)
 *
 * Behavior:
 *  1. ESP32 enters deep sleep mode
 *  2. PIR sensor detects motion → GPIO32 goes HIGH → ESP32 wakes up
 *  3. GPIO23 is set HIGH to power ON the camera
 *  4. Camera stays powered for 10 seconds
 *  5. GPIO23 is set LOW to power OFF the camera
 *  6. ESP32 returns to deep sleep and waits for next PIR trigger
 */

#include "utilities.h"
#include <driver/gpio.h>

// Define PIR sensor pin (external wakeup source)
#define PIR_SENSOR_PIN 32

// Define camera power enable pin
#define CAM_PWR_EN_PIN 23

// Camera power on duration (milliseconds)
#define CAMERA_ON_DURATION_MS 10000 // 10 seconds

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
  case ESP_SLEEP_WAKEUP_EXT0:
    Serial.println("Wakeup caused by external signal (PIR Sensor) using RTC_IO");
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    Serial.println("Wakeup caused by external signal using RTC_CNTL");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.println("Wakeup caused by timer");
    break;
  default:
    Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
    break;
  }
}

void setup() {
  Serial.begin(115200); // Set console baud rate
  delay(100);           // Give serial time to initialize

  Serial.println("\n===========================================");
  Serial.println("LILYGO T-A7670 Camera Power Test");
  Serial.println("===========================================");

  // Print why we woke up
  print_wakeup_reason();

  // Initialize camera power pin as output
  pinMode(CAM_PWR_EN_PIN, OUTPUT);

  // Handle PIR sensor trigger
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("\n[EVENT] Motion detected by PIR sensor!");
    Serial.println("[ACTION] Powering ON camera via GPIO23...");

    // Turn ON camera power (HIGH)
    digitalWrite(CAM_PWR_EN_PIN, HIGH);
    Serial.println("[STATUS] Camera powered ON");

    // Keep camera on for specified duration
    Serial.printf("[TIMER] Camera will stay ON for %d seconds\n", CAMERA_ON_DURATION_MS / 1000);

    // Show countdown in serial monitor
    for (int i = CAMERA_ON_DURATION_MS / 1000; i > 0; i--) {
      Serial.printf("  [%d seconds remaining]\n", i);
      delay(1000);
    }

    // Turn OFF camera power (LOW)
    digitalWrite(CAM_PWR_EN_PIN, LOW);
    Serial.println("[STATUS] Camera powered OFF");

  } else {
    // First boot or reset
    Serial.println("\n[BOOT] First boot or reset detected");
    Serial.println("[TEST] Testing camera power control...");

    // Quick test: Turn camera ON briefly then OFF
    Serial.println("[ACTION] Testing GPIO23 HIGH...");
    digitalWrite(CAM_PWR_EN_PIN, HIGH);
    delay(500);
    Serial.println("[ACTION] Testing GPIO23 LOW...");
    digitalWrite(CAM_PWR_EN_PIN, LOW);
    delay(500);

    Serial.println("[TEST] Camera power test completed");
  }

  // Configure PIR sensor pin for wakeup
  // PIR sensor output is typically HIGH when motion is detected
  // So we wake on LOW->HIGH transition (rising edge = 1)
  pinMode(PIR_SENSOR_PIN, INPUT_PULLDOWN);
  gpio_pulldown_en((gpio_num_t)PIR_SENSOR_PIN);

  Serial.println("\n[CONFIG] System configuration:");
  Serial.println("  - Wakeup source: GPIO32 (PIR Sensor)");
  Serial.println("  - Camera power: GPIO23 (HIGH = ON)");
  Serial.printf("  - Camera ON time: %d seconds\n", CAMERA_ON_DURATION_MS / 1000);

  // Wait for PIR sensor to settle
  Serial.println("\n[WAIT] Waiting for PIR sensor to settle (2 seconds)...");
  delay(2000);

  Serial.println("\n[SLEEP] Entering ESP32 deep sleep mode...");
  Serial.println("===========================================");
  Serial.flush();

  // Enable wakeup on GPIO 32 (PIR sensor)
  // Wake when signal goes HIGH (PIR detects motion)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_SENSOR_PIN, 1);

  delay(200);
  esp_deep_sleep_start();

  // This line will never be printed
  Serial.println("This will never be printed");
}

void loop() {
  // Never reached - ESP32 goes directly to deep sleep in setup()
}
