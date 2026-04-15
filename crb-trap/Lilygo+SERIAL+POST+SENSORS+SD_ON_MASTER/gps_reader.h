/**
 * @file      gps_reader.h
 * @brief     GPS initialization and position retrieval API.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>

// ==================== GLOBAL GPS STATE ====================
// Populated by initGPS() during cold boot
extern float g_gps_lat;
extern float g_gps_lon;
extern bool g_gps_valid;

// ==================== API ====================

/**
 * @brief Initialize GPS on cold boot.
 *        Attempts fresh read → EEPROM cache → hardcoded defaults.
 *        Populates g_gps_lat, g_gps_lon, g_gps_valid globals.
 */
void initGPS();

/**
 * @brief Retrieve the current GPS position (populated by initGPS).
 * @param latitude   Reference to float to store latitude
 * @param longitude  Reference to float to store longitude
 * @return true if position is valid (fresh or cached), false if using defaults
 */
bool getCurrentGPS(float &latitude, float &longitude);
