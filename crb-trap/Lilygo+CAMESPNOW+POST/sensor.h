/**
 * @file      sensor.h
 * @brief     Light intensity (LUX) sensor interface for the LilyGo A7670 master.
 *            Currently provides a stub; replace the body of readLuxSensor()
 *            with a real BH1750 (or similar) reading when hardware is available.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>

// ==================== I2C BUS PINS ====================
// These match the default I2C pins used by Wire.begin() on the LILYGO_T_A7670.
#define SENSOR_I2C_SDA 21
#define SENSOR_I2C_SCL 22

/**
 * @brief Read the current ambient light level.
 * @return Lux value (0–65535).
 *         Returns a simulated value (500) until a real sensor is wired up.
 */
uint16_t readLuxSensor();
