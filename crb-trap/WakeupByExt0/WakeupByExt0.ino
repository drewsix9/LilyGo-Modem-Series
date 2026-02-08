/**
 * @file      WakeupByExt0.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-02-03
 * @note      This example wakes up ESP32 from deep sleep using an external signal
 *            on GPIO 32 (PIR sensor output). It combines deep sleep power management
 *            with external wakeup functionality.
 */

#include <driver/gpio.h>

// Define PIR sensor pin (external wakeup source)
#define PIR_SENSOR_PIN 32

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

  // Print why we woke up
  print_wakeup_reason();

  // Handle PIR sensor trigger
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Motion detected by PIR sensor!");

    // Here you can add code to handle the PIR sensor trigger
    // For example: send SMS, upload data, take photo, etc.
    Serial.println("PIR sensor triggered! Handling event...");

    delay(2000);
  } else {
    // First boot
    Serial.println("First boot or reset. Initializing...");
    delay(2000);
  }

  // Configure PIR sensor pin for wakeup
  // PIR sensor output is typically HIGH when motion is detected
  // So we wake on LOW->HIGH transition (rising edge = 1)
  pinMode(PIR_SENSOR_PIN, INPUT_PULLDOWN);
  gpio_pulldown_en((gpio_num_t)PIR_SENSOR_PIN);

  Serial.println("Waiting for PIR sensor to settle...");
  delay(2000);

  Serial.println("System configured to wake up on PIR sensor trigger (GPIO 32)");
  Serial.println("Enter ESP32 deep sleep mode!");
  Serial.flush();

  // Enable wakeup on GPIO 32 (PIR sensor)
  // Wake when signal goes HIGH (PIR detects motion)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_SENSOR_PIN, 1);

  delay(200);
  esp_deep_sleep_start();
  Serial.println("This will never be printed");
}

void loop() {
  // Never reached - ESP32 goes directly to deep sleep in setup()
}
