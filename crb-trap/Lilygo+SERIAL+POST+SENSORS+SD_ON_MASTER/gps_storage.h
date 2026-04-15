/**
 * @file      gps_storage.h
 * @brief     GPS position storage and retrieval via EEPROM.
 *            Caches last-known GPS position for fallback and inter-boot persistence.
 *            Current EEPROM layout:
 *            [0..1]   — SD card rolling filename counter (maintained by sdcard.h)
 *            [2..9]   — float latitude (4 bytes) + float longitude (4 bytes)
 *            [10]     — GPS validity flag (0xAA = valid cache, 0x00 = invalid)
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>
#include <EEPROM.h>

// ==================== EEPROM LAYOUT ====================
#define GPS_LAT_ADDR 2         // Byte offset for latitude (float, 4 bytes)
#define GPS_LON_ADDR 6         // Byte offset for longitude (float, 4 bytes)
#define GPS_VALID_FLAG_ADDR 10 // Byte offset for validity marker (uint8_t, 1 byte)
#define GPS_VALID_MARKER 0xAA  // Marker to indicate cached GPS is valid

// ==================== API ====================

/**
 * @brief Save GPS position to EEPROM cache.
 * @param latitude   GPS latitude as float (decimal degrees)
 * @param longitude  GPS longitude as float (decimal degrees)
 */
inline void saveGPSToEEPROM(float latitude, float longitude) {
  uint8_t *lat_ptr = (uint8_t *)&latitude;
  uint8_t *lon_ptr = (uint8_t *)&longitude;

  // Write latitude bytes
  for (int i = 0; i < 4; i++) {
    EEPROM.write(GPS_LAT_ADDR + i, lat_ptr[i]);
  }

  // Write longitude bytes
  for (int i = 0; i < 4; i++) {
    EEPROM.write(GPS_LON_ADDR + i, lon_ptr[i]);
  }

  // Write validity marker
  EEPROM.write(GPS_VALID_FLAG_ADDR, GPS_VALID_MARKER);
  EEPROM.commit();

  Serial.printf("[GPS_STORAGE] Saved to EEPROM: lat=%.6f, lon=%.6f\n", latitude, longitude);
}

/**
 * @brief Load GPS position from EEPROM cache.
 * @param latitude   Reference to float to store latitude
 * @param longitude  Reference to float to store longitude
 * @return true if cache is valid (marker present), false if cache is empty/invalid
 */
inline bool loadGPSFromEEPROM(float &latitude, float &longitude) {
  // Check validity flag first
  uint8_t valid_flag = EEPROM.read(GPS_VALID_FLAG_ADDR);

  if (valid_flag != GPS_VALID_MARKER) {
    Serial.println("[GPS_STORAGE] EEPROM cache is empty/invalid");
    return false;
  }

  // Read latitude bytes
  uint8_t lat_bytes[4];
  for (int i = 0; i < 4; i++) {
    lat_bytes[i] = EEPROM.read(GPS_LAT_ADDR + i);
  }

  // Read longitude bytes
  uint8_t lon_bytes[4];
  for (int i = 0; i < 4; i++) {
    lon_bytes[i] = EEPROM.read(GPS_LON_ADDR + i);
  }

  // Reconstruct float values
  latitude = *(float *)lat_bytes;
  longitude = *(float *)lon_bytes;

  Serial.printf("[GPS_STORAGE] Loaded from EEPROM: lat=%.6f, lon=%.6f\n", latitude, longitude);
  return true;
}

/**
 * @brief Clear GPS cache by invalidating the marker.
 */
inline void clearGPSCache() {
  EEPROM.write(GPS_VALID_FLAG_ADDR, 0x00);
  EEPROM.commit();
  Serial.println("[GPS_STORAGE] GPS cache cleared");
}
