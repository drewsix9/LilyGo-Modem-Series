/**
 * @file      power_manager.h
 * @brief     Camera power control and deep-sleep management for the LilyGo A7670 master.
 *            GPIO23 controls the ESP32-CAM via a MOSFET with inverted logic
 *            (LOW = camera ON, HIGH = camera OFF).
 *            GPIO32 is the PIR sensor wakeup source for ESP32 deep-sleep.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>
#include <driver/gpio.h>

// ==================== PIN DEFINITIONS ====================
#define PIR_SENSOR_PIN 32 // PIR sensor: wakeup trigger (active HIGH)
#define CAM_PWR_EN_PIN 23 // Camera power enable — inverted: LOW=ON, HIGH=OFF

// ==================== TIMING ====================
#define PIR_SETTLE_TIME_MS 2000 // PIR sensor de-bounce / settle time (ms)

/**
 * @brief Print the ESP32 wakeup reason to Serial.
 */
void print_wakeup_reason();

/**
 * @brief Power ON the camera (GPIO23 LOW — inverted MOSFET logic).
 */
void powerOnCamera();

/**
 * @brief Power OFF the camera (GPIO23 HIGH) and hold GPIO23 state across deep-sleep.
 */
void powerOffCamera();

/**
 * @brief Enter ESP32 deep-sleep, waking on a HIGH edge from the PIR sensor (GPIO32).
 *        Camera power pin is held HIGH (OFF) during sleep.
 *        This function never returns.
 */
void enterDeepSleep();
