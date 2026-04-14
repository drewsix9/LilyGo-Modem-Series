/**
 * @file      voltage_reader.h
 * @brief     Battery and solar voltage ADC reader with averaging support.
 *            Provides raw mV readings (no percentage mapping) from board ADC pins.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>
#include <cstdint>

// ==================== CONFIGURATION ====================

/**
 * @brief Sampling configuration for averaged voltage reads.
 */
struct VoltageReaderConfig {
  uint8_t avg_samples;           // Number of samples to average (e.g., 20)
  uint16_t sample_delay_ms;      // Delay between samples in milliseconds
  bool apply_divider_correction; // Apply x2 correction for voltage divider
};

// ==================== DATA STRUCTURES ====================

/**
 * @brief Result of a voltage reading operation.
 */
struct VoltageReading {
  uint32_t battery_mv; // Battery voltage in millivolts
  uint32_t solar_mv;   // Solar voltage in millivolts (0 if unavailable)
  bool battery_valid;  // true if battery ADC pin is available and read succeeded
  bool solar_valid;    // true if solar ADC pin is available and read succeeded
};

// ==================== API ====================

/**
 * @brief Initialize voltage reader with ADC configuration.
 *        Call once at startup after creating VoltageReaderConfig.
 * @param cfg Configuration struct with sampling parameters.
 * @return true if initialization succeeded (at least battery pin is available).
 */
bool initVoltageReader(const VoltageReaderConfig &cfg);

/**
 * @brief Read battery voltage using configured averaging.
 * @return Battery voltage in mV (0 if pin unavailable or read failed).
 */
uint32_t readBatteryVoltageMv();

/**
 * @brief Read solar voltage using configured averaging.
 * @return Solar voltage in mV (0 if pin unavailable or read failed).
 */
uint32_t readSolarVoltageMv();

/**
 * @brief Read both battery and solar voltages in a single call.
 * @return VoltageReading struct with both values and validity flags.
 */
VoltageReading readBothVoltagesMv();

/**
 * @brief Check if solar ADC pin is available on this board.
 * @return true if BOARD_SOLAR_ADC_PIN is defined and valid.
 */
bool isSolarAvailable();
