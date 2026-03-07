/**
 * @file      sensor.cpp
 * @brief     Light intensity (LUX) sensor implementation.
 *            Replace the stub body with a real sensor read when hardware is ready.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "sensor.h"
#include <Arduino.h>

// Uncomment when BH1750 hardware is connected:
// #include <BH1750.h>
// static BH1750 lightMeter;

uint16_t readLuxSensor() {
  // TODO: Replace with actual light sensor reading.
  // Example for BH1750:
  //   float lux = lightMeter.readLightLevel();
  //   return (uint16_t)constrain(lux, 0, 65535);

  uint16_t simulatedLux = 500;
  return simulatedLux;
}
