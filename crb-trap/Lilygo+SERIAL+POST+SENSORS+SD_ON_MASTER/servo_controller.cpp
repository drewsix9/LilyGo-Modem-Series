/**
 * @file      servo_controller.cpp
 * @brief     Single-servo helper implementation for LilyGo T-A7670 master.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "servo_controller.h"

namespace ServoController {

static Servo g_servo;
static int g_pin = -1;
static uint16_t g_minPulseUs = SERVO_DEFAULT_MIN_PULSE_US;
static uint16_t g_maxPulseUs = SERVO_DEFAULT_MAX_PULSE_US;

void handleMale() {
  ServoController::attached();
  Serial.println("[ACTION] Handling male input...");

  for (int i = 100; i >= 45; i -= 5) {
    Serial.println("[TEST] Moving to " + String(i) + "°...");
    ServoController::writeAngle((uint8_t)i);
    delay(20);
  }

  Serial.println("GOING BACK TO 90...");
  delay(2000);
  for (int i = 45; i <= 100; i += 5) {
    Serial.println("[TEST] Moving to " + String(i) + "°...");
    ServoController::writeAngle((uint8_t)i);
    delay(20);
  }
  Serial.println("DONE WITH MALE TEST");
  ServoController::detach();
}

void handleFemale() {
  ServoController::attached();
  Serial.println("[ACTION] Handling female input...");
  for (int i = 85; i <= 135; i += 5) {
    Serial.println("[TEST] Moving to " + String(i) + "°...");
    delay(20);
    ServoController::writeAngle((uint8_t)i);
  }

  Serial.println("GOING BACK TO 90...");
  delay(2000);
  for (int i = 135; i >= 85; i -= 5) {
    Serial.println("[TEST] Moving to " + String(i) + "°...");
    delay(20);
    ServoController::writeAngle((uint8_t)i);
  }
  Serial.println("DONE WITH FEMALE TEST");
  ServoController::detach();
}

bool begin(int pin, uint16_t minPulseUs, uint16_t maxPulseUs, uint8_t startAngle) {
  if (minPulseUs < SERVO_DEFAULT_MIN_PULSE_US) {
    minPulseUs = SERVO_DEFAULT_MIN_PULSE_US;
  }
  if (maxPulseUs > 2500) {
    maxPulseUs = 2500;
  }
  if (maxPulseUs <= minPulseUs) {
    maxPulseUs = minPulseUs + 1;
  }

  if (g_servo.attached()) {
    g_servo.detach();
  }

  g_servo.setPeriodHertz(50);
  g_servo.attach(pin, (int)minPulseUs, (int)maxPulseUs);

  if (!g_servo.attached()) {
    g_pin = -1;
    return false;
  }

  g_pin = pin;
  g_minPulseUs = minPulseUs;
  g_maxPulseUs = maxPulseUs;

  g_servo.write((int)constrain((int)startAngle, 0, 180));
  return true;
}

bool writeAngle(uint8_t angle) {
  // Attach servo temporarily
  if (!g_servo.attached()) {
    g_servo.setPeriodHertz(50);
    g_servo.attach(g_pin, (int)g_minPulseUs, (int)g_maxPulseUs);
  }

  if (!g_servo.attached()) {
    return false;
  }

  // Write angle and move servo
  g_servo.write((int)constrain((int)angle, 0, 180));

  return true;
}

bool writePulseUs(uint16_t pulseUs) {
  if (!g_servo.attached()) {
    return false;
  }

  uint16_t clamped = pulseUs;
  if (clamped < g_minPulseUs) {
    clamped = g_minPulseUs;
  }
  if (clamped > g_maxPulseUs) {
    clamped = g_maxPulseUs;
  }

  g_servo.writeMicroseconds((int)clamped);
  return true;
}

uint8_t readAngle() {
  int value = g_servo.read();
  return (uint8_t)constrain(value, 0, 180);
}

int pin() {
  return g_servo.attached() ? g_pin : -1;
}

bool attached() {
  return g_servo.attached();
}

void detach() {
  if (g_servo.attached()) {
    g_servo.detach();
  }
  // Keep g_pin so writeAngle() can re-attach later
}

} // namespace ServoController
