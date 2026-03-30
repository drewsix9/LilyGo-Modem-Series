/**
 * @file      sensor.h
 * @brief     Sensor interface for LilyGo A7670 master.
 *            Provides BH1750 ambient light and BMI160 IMU helpers over I2C.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>

// ==================== I2C BUS PINS ====================
// These match the default I2C pins used by Wire.begin() on the LILYGO_T_A7670.
#define SENSOR_I2C_SDA 21
#define SENSOR_I2C_SCL 22

struct Bmi160Reading {
  float gx;
  float gy;
  float gz;
  float ax;
  float ay;
  float az;
};

/**
 * @brief Initialize BH1750 and BMI160 on the already-started I2C bus.
 * @return true when both sensors are initialized.
 */
bool initSensors();

/**
 * @brief Read the current ambient light level.
 * @return Lux value (0-65535). Returns the latest cached value on transient read miss.
 */
uint16_t readLuxSensor();

/**
 * @brief Read raw gyro and accel values from BMI160.
 * @param out Sensor sample output.
 * @return true on successful BMI160 read.
 */
bool readBmi160Raw(Bmi160Reading &out);

/**
 * @brief Estimate "fallen" status from accelerometer-based orientation.
 * @param sample Raw BMI160 sample.
 * @return true when pitch or roll exceeds threshold.
 */
bool detectFallFromBmi160(const Bmi160Reading &sample);
