/**
 * @file      voltage_reader.cpp
 * @brief     Battery and solar voltage ADC reader implementation.
 *            Uses 12-bit ADC, 11dB attenuation, averaging, and voltage divider correction.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "voltage_reader.h"
#include "utilities.h"
#include <algorithm>
#include <numeric>
#include <vector>

// ==================== MODULE STATE ====================

static VoltageReaderConfig gConfig = {20, 30, true};
static bool gBatteryAvailable = false;
static bool gSolarAvailable = false;
static bool gInitialized = false;

// ==================== HELPERS ====================

/**
 * @brief Internal helper to read and average a single ADC pin.
 * @param pin GPIO pin number connected to ADC.
 * @return Averaged mV reading (already includes divider correction if enabled).
 */
static uint32_t readAveragedMv(uint8_t pin) {
  if (pin == 255) {
    return 0; // Invalid pin
  }

  std::vector<uint32_t> samples;
  for (uint8_t i = 0; i < gConfig.avg_samples; i++) {
    uint32_t raw_mv = analogReadMilliVolts(pin);
    samples.push_back(raw_mv);

    if (i < gConfig.avg_samples - 1) {
      delay(gConfig.sample_delay_ms);
    }
  }

  // Remove min and max outliers for robustness
  if (samples.size() > 2) {
    std::sort(samples.begin(), samples.end());
    samples.erase(samples.begin()); // Remove min
    samples.pop_back();             // Remove max
  }

  // Calculate average
  uint32_t sum = std::accumulate(samples.begin(), samples.end(), 0U);
  uint32_t avg = sum / samples.size();

  // Apply voltage divider correction if enabled
  if (gConfig.apply_divider_correction) {
    avg *= 2;
  }

  return avg;
}

// ==================== API IMPLEMENTATION ====================

bool initVoltageReader(const VoltageReaderConfig &cfg) {
  gConfig = cfg;

  // Configure ADC for 0-3.6V full range
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);

#if CONFIG_IDF_TARGET_ESP32
  analogSetWidth(12); // ESP32-specific call
#endif

  // Check battery pin availability
#ifdef BOARD_BAT_ADC_PIN
  gBatteryAvailable = true;
  analogSetPinAttenuation(BOARD_BAT_ADC_PIN, ADC_11db);
  Serial.println("[VOLT] Battery ADC pin configured (GPIO " + String(BOARD_BAT_ADC_PIN) + ")");
#else
  Serial.println("[VOLT] WARNING: BOARD_BAT_ADC_PIN not defined");
  gBatteryAvailable = false;
#endif

  // Check solar pin availability
#ifdef BOARD_SOLAR_ADC_PIN
  gSolarAvailable = true;
  analogSetPinAttenuation(BOARD_SOLAR_ADC_PIN, ADC_11db);
  Serial.println("[VOLT] Solar ADC pin configured (GPIO " + String(BOARD_SOLAR_ADC_PIN) + ")");
#else
  gSolarAvailable = false;
#endif

  gInitialized = true;
  return gBatteryAvailable; // Success if at least battery is available
}

uint32_t readBatteryVoltageMv() {
  if (!gInitialized || !gBatteryAvailable) {
    Serial.println("[VOLT] Battery reader not initialized or unavailable");
    return 0;
  }

#ifdef BOARD_BAT_ADC_PIN
  return readAveragedMv(BOARD_BAT_ADC_PIN);
#else
  return 0;
#endif
}

uint32_t readSolarVoltageMv() {
  if (!gInitialized) {
    Serial.println("[VOLT] Solar reader not initialized");
    return 0;
  }

#ifdef BOARD_SOLAR_ADC_PIN
  if (!gSolarAvailable) {
    return 0;
  }
  return readAveragedMv(BOARD_SOLAR_ADC_PIN);
#else
  return 0;
#endif
}

VoltageReading readBothVoltagesMv() {
  VoltageReading result = {0, 0, false, false};

  if (!gInitialized) {
    Serial.println("[VOLT] Reader not initialized");
    return result;
  }

  // Read battery
  if (gBatteryAvailable) {
#ifdef BOARD_BAT_ADC_PIN
    result.battery_mv = readAveragedMv(BOARD_BAT_ADC_PIN);
    result.battery_valid = true;
#endif
  }

  // Read solar
  if (gSolarAvailable) {
#ifdef BOARD_SOLAR_ADC_PIN
    result.solar_mv = readAveragedMv(BOARD_SOLAR_ADC_PIN);
    result.solar_valid = true;
#endif
  }

  return result;
}

bool isSolarAvailable() {
  return gSolarAvailable;
}
