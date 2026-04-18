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
 *        Follows GPS_BuiltInEx.ino pattern:
 *        1. Enable GPS hardware
 *        2. Try multiple GNSS modes with 60s total timeout (15s delays per retry)
 *        3. If fail, load cached position from EEPROM
 *        4. If no cache, use hardcoded defaults
 */
void initGPS() {
  Serial.println("\n[GPS] Initializing GPS on cold boot...");

  GPSInfo info;
  float read_lat = 0.0, read_lon = 0.0;
  bool gps_success = false;

  // ==================== ENABLE GPS HARDWARE ====================
  Serial.println("[GPS] Enabling GPS/GNSS/GLONASS hardware...");
  int enable_retry = 0;
  while (!modem.enableGPS(MODEM_GPS_ENABLE_GPIO, MODEM_GPS_ENABLE_LEVEL)) {
    Serial.print(".");
    enable_retry++;
    if (enable_retry > 10) {
      Serial.println("[GPS] ERROR: Failed to enable GPS hardware!");
      break;
    }
  }
  Serial.println();
  Serial.println("[GPS] GPS hardware enabled");

  // Set GPS Baud to 115200
  modem.setGPSBaud(115200);
  delay(500);

  // ==================== ATTEMPT FRESH GPS READ WITH MULTIPLE MODES ====================
  // Try different GNSS modes to maximize acquisition chance
  // A7670X modes: 1=GPS L1+SBAS+QZSS, 3=GPS+GLONASS+GALILEO+SBAS+QZSS, 4=GPS+BDS+GALILEO+SBAS+QZSS
  // Mode 3 & 4 are most comprehensive (4+ constellations), Mode 1 is fallback
  uint8_t gnss_modes[] = {3, 4, 1}; // Mode 3 & 4 have best satellite coverage, Mode 1 is fallback
  uint8_t num_modes = sizeof(gnss_modes) / sizeof(gnss_modes[0]);
  const unsigned long MODE_TIMEOUT_MS = 60000; // 60 second timeout per mode

  Serial.println("[GPS] Attempting fresh read with 60s timeout per GNSS mode...");

  for (uint8_t mode_idx = 0; mode_idx < num_modes && !gps_success; mode_idx++) {
    uint8_t gnss_mode = gnss_modes[mode_idx];
    Serial.printf("[GPS] Trying GNSS mode %d\n", gnss_mode);
    modem.setGPSMode(gnss_mode);
    delay(500);

    // Try this mode with its own 60-second window
    unsigned long mode_start_time = millis();
    while (millis() - mode_start_time < MODE_TIMEOUT_MS && !gps_success) {
      if (modem.getGPS_Ex(info)) {
        // Success: Valid GPS fix acquired
        g_gps_lat = info.latitude;
        g_gps_lon = info.longitude;
        g_gps_valid = true;
        gps_success = true;

        Serial.printf("\n[GPS] Fresh read SUCCESS (mode %d)!\n", gnss_mode);
        Serial.printf("[GPS]   Position: %.6f, %.6f\n", info.latitude, info.longitude);
        Serial.printf("[GPS]   Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                      info.year, info.month, info.day, info.hour, info.minute, info.second);

        // Save to EEPROM for next boot
        saveGPSToEEPROM(info.latitude, info.longitude);
        break;

      } else {
        Serial.print(".");
        delay(15000L); // Retry every 15 seconds (like GPS_BuiltInEx.ino)
      }
    }
  }

  if (!gps_success) {
    // Fresh read failed; try EEPROM cache
    Serial.println("\n[GPS] Fresh read failed (60s timeout), checking EEPROM cache...");

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
