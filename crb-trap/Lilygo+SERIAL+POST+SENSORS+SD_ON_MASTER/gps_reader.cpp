/**
 * @file      gps_reader.cpp
 * @brief     GPS position acquisition and caching for metadata.
 *            Runs on cold boot only (not during PIR/deep-sleep wakeup).
 *            Uses best-effort reading with EEPROM fallback.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "gps_reader.h"
#include "gps_storage.h"
#include "http_upload.h"
#include "utilities.h"
#include <TinyGsmClient.h>

// ==================== GLOBAL GPS STATE ====================
// Populated by initGPS() during cold boot setup()
float g_gps_lat = 0.0;
float g_gps_lon = 0.0;
bool g_gps_valid = false;

// ==================== EXTERNAL MODEM STATE ====================
extern TinyGsm modem;

// ==================== IMPLEMENTATION ====================

/**
 * @brief Initialize GPS on cold boot and populate global lat/lon.
 *        Best-effort read with EEPROM fallback chain:
 *        1. Try fresh GPS read (500-1000ms timeout)
 *        2. If fail, load cached position from EEPROM
 *        3. If no cache, use hardcoded defaults
 */
void initGPS() {
  Serial.println("\n[GPS] Initializing GPS on cold boot...");

  float read_lat = 0.0, read_lon = 0.0;
  uint8_t fix_mode = 0;
  int vsat = 0, usat = 0;
  float speed = 0.0, alt = 0.0, accuracy = 0.0;
  int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;

  // ==================== ATTEMPT FRESH GPS READ (BEST-EFFORT) ====================
  // Modem should already be initialized by http_upload.initModem() or earlier.
  // Call modem.getGPS() with short timeout to capture instantaneous satellite state.

  Serial.println("[GPS] Attempting fresh read (best-effort, no blocking)...");

  if (modem.getGPS(
          &fix_mode,
          &read_lat,
          &read_lon,
          &speed,
          &alt,
          &vsat,
          &usat,
          &accuracy,
          &year,
          &month,
          &day,
          &hour,
          &min,
          &sec)) {
    // Success: Valid GPS fix acquired
    g_gps_lat = read_lat;
    g_gps_lon = read_lon;
    g_gps_valid = true;

    Serial.printf("[GPS] Fresh read SUCCESS!\n");
    Serial.printf("[GPS]   Position: %.6f, %.6f\n", read_lat, read_lon);
    Serial.printf("[GPS]   Fix Mode: %d, Visible Sats: %d, Used Sats: %d\n",
                  fix_mode, vsat, usat);
    Serial.printf("[GPS]   Accuracy: %.2f m, Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  accuracy, year, month, day, hour, min, sec);

    // Save to EEPROM for next boot
    saveGPSToEEPROM(read_lat, read_lon);

  } else {
    // Fresh read failed; try EEPROM cache
    Serial.println("[GPS] Fresh read failed, checking EEPROM cache...");

    if (loadGPSFromEEPROM(read_lat, read_lon)) {
      g_gps_lat = read_lat;
      g_gps_lon = read_lon;
      g_gps_valid = true;

      Serial.printf("[GPS] Loaded from EEPROM cache: %.6f, %.6f\n", read_lat, read_lon);

    } else {
      // No cache; use hardcoded defaults
      g_gps_lat = atof(DEFAULT_GPS_LAT); // "9.77"
      g_gps_lon = atof(DEFAULT_GPS_LON); // "124.47"
      g_gps_valid = false;

      Serial.printf("[GPS] No cache available, using hardcoded defaults: %.6f, %.6f\n",
                    g_gps_lat, g_gps_lon);
    }
  }

  Serial.printf("[GPS] Final globals: lat=%.6f, lon=%.6f, valid=%s\n",
                g_gps_lat, g_gps_lon, g_gps_valid ? "true" : "false");
}

/**
 * @brief Get the current GPS position (set by initGPS() during setup).
 * @param latitude   Reference to float to store latitude
 * @param longitude  Reference to float to store longitude
 * @return true if position is valid (fresh or cached), false if using defaults
 */
bool getCurrentGPS(float &latitude, float &longitude) {
  latitude = g_gps_lat;
  longitude = g_gps_lon;
  return g_gps_valid;
}
