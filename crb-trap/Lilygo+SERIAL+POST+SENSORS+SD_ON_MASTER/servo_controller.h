/**
 * @file      servo_controller.h
 * @brief     Single-servo helper for LilyGo T-A7670 master (ESP32).
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>

namespace ServoController {

// For the standalone Lilygo+Servo sketch, use GPIO13 by default.
// It is exposed on the header and valid for ESP32Servo.
// Do not use SD card or GPS-TX-on-13 at the same time as the servo signal.
constexpr int SERVO_RECOMMENDED_PIN = 13;

constexpr uint16_t SERVO_DEFAULT_MIN_PULSE_US = 500;
constexpr uint16_t SERVO_DEFAULT_MAX_PULSE_US = 2400;
constexpr uint8_t SERVO_DEFAULT_START_ANGLE = 90;

bool begin(int pin = SERVO_RECOMMENDED_PIN,
           uint16_t minPulseUs = SERVO_DEFAULT_MIN_PULSE_US,
           uint16_t maxPulseUs = SERVO_DEFAULT_MAX_PULSE_US,
           uint8_t startAngle = SERVO_DEFAULT_START_ANGLE);

bool writeAngle(uint8_t angle);
bool writePulseUs(uint16_t pulseUs);
uint8_t readAngle();
int pin();
bool attached();
void detach();

} // namespace ServoController
