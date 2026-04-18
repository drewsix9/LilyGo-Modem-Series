/**
 * @file      sensor.cpp
 * @brief     BH1750 + BMI160 sensor implementation.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "sensor.h"
#include <Arduino.h>
#include <DFRobot_BMI160.h>
#include <ErriezBH1750.h>
#include <Wire.h>
#include <math.h>

// static BH1750 lightSensor(LOW); // 0x23 (ADDR low/open) — LIGHT SENSOR DISABLED
static DFRobot_BMI160 bmi160;

// static bool bh1750Ready = false; — LIGHT SENSOR DISABLED
static bool bmi160Ready = false;
static uint16_t lastLuxValue = 0;
static const int8_t BMI160_DEVICE_ADDR = 0x68;
static const float FALL_ANGLE_THRESHOLD_DEG = 60.0f;
static const float FALL_ANGLE_UPPER_BOUND_DEG = 90.0f;

bool initSensors() {
  // BH1750 light sensor disabled
  // Serial.println("[SENSOR] Initializing BH1750...");
  // lightSensor.begin(ModeContinuous, ResolutionLow);
  // lightSensor.startConversion();
  // bh1750Ready = true;
  // Serial.println("[SENSOR] BH1750 initialized");

  Serial.println("[SENSOR] Initializing BMI160...");
  if (bmi160.softReset() != BMI160_OK) {
    Serial.println("[SENSOR] BMI160 soft reset failed");
    bmi160Ready = false;
  } else if (bmi160.I2cInit(BMI160_DEVICE_ADDR) != BMI160_OK) {
    Serial.println("[SENSOR] BMI160 I2C init failed");
    bmi160Ready = false;
  } else {
    Serial.println("[SENSOR] BMI160 initialized");
    bmi160Ready = true;
  }

  return bmi160Ready; // Only BMI160 active (BH1750 disabled)
}

uint16_t readLuxSensor() {
  // BH1750 light sensor disabled — returning 0
  return 0;
}

bool readBmi160Raw(Bmi160Reading &out) {
  if (!bmi160Ready) {
    Serial.println("[SENSOR] BMI160 not initialized");
    return false;
  }

  int16_t accelGyro[6] = {0};
  int result = bmi160.getAccelGyroData(accelGyro);
  if (result != BMI160_OK) {
    Serial.print("[SENSOR] BMI160 read failed: ");
    Serial.println(result);
    return false;
  }

  out.gx = accelGyro[0] / 131.0f;
  out.gy = accelGyro[1] / 131.0f;
  out.gz = accelGyro[2] / 131.0f;
  out.ax = accelGyro[3] / 16384.0f;
  out.ay = accelGyro[4] / 16384.0f;
  out.az = accelGyro[5] / 16384.0f;
  return true;
}

bool detectFallFromBmi160(const Bmi160Reading &sample) {
  const float pitch = atan2f(sample.ay, sample.az) * 180.0f / 3.14159265f;
  const float roll = atan2f(-sample.ax, sqrtf(sample.ay * sample.ay + sample.az * sample.az)) * 180.0f / 3.14159265f;

  // Return false if roll is between FALL_ANGLE_THRESHOLD_DEG and FALL_ANGLE_UPPER_BOUND_DEG (not a fall)
  return !(fabs(roll) >= FALL_ANGLE_THRESHOLD_DEG && fabs(roll) <= FALL_ANGLE_UPPER_BOUND_DEG);
}
