/**
 * @file      power_manager.cpp
 * @brief     Camera power control and deep-sleep implementation.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "power_manager.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

// ==================== WAKEUP REASON ====================

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

// ==================== CAMERA POWER ====================

void powerOnCamera() {
  Serial.println("[POWER] Turning camera ON (GPIO23 -> LOW - inverted logic)");
  digitalWrite(CAM_PWR_EN_PIN, LOW); // Inverted: LOW = camera ON
  delay(100);
  Serial.println("[STATUS] Camera powered ON");
}

void powerOffCamera() {
  Serial.println("[POWER] Turning camera OFF (GPIO23 -> HIGH - inverted logic)");
  digitalWrite(CAM_PWR_EN_PIN, HIGH); // Inverted: HIGH = camera OFF
  delay(100);

  // Hold GPIO23 state during deep sleep so the camera stays off
  gpio_hold_en((gpio_num_t)CAM_PWR_EN_PIN);
  gpio_deep_sleep_hold_en();

  Serial.println("[STATUS] Camera powered OFF and isolated");
}

// ==================== DEEP SLEEP ====================

void enterDeepSleep() {
  Serial.println("\n[WAIT] PIR sensor settling...");
  delay(PIR_SETTLE_TIME_MS);

  // Ensure camera stays OFF during sleep
  digitalWrite(CAM_PWR_EN_PIN, HIGH);
  gpio_hold_en((gpio_num_t)CAM_PWR_EN_PIN);
  gpio_deep_sleep_hold_en();

  Serial.println("[GPIO] Camera power pin held HIGH (OFF) for deep sleep");
  Serial.println("[SLEEP] Entering deep sleep mode...");
  Serial.println("         Waiting for PIR motion detection...");
  Serial.println("===========================================\n");
  Serial.flush();

  // Wake on HIGH level from PIR sensor
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_SENSOR_PIN, 1);

  delay(200);
  esp_deep_sleep_start();
  // Never returns
}
