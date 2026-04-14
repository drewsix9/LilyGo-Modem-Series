/**
 * @file      http_upload.h
 * @brief     HTTPS upload module for posting JPEG photos from RAM to a
 *            Supabase Edge Function via the A7670 LTE modem.
 *            Handles modem power-on, network registration, and binary POST
 *            using the SIMCOM HTTP(S) AT command set.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>

// ==================== SUPABASE CONFIGURATION ====================
// These are injected by PlatformIO build flags from .env

// ==================== CONFIGURABLE METADATA DEFAULTS ====================
// Replace these with real sensor values when hardware is integrated.
#define DEFAULT_TRAP_ID "TRAP-001"
#define DEFAULT_GPS_LAT "9.77"
#define DEFAULT_GPS_LON "124.47"
#define DEFAULT_IS_FALLEN "false"
#define DEFAULT_BATTERY_VOLTAGE "3.91" // Fallback: actual reading from voltage_reader preferred
#define DEFAULT_SOLAR_VOLTAGE ""       // Empty string = omit from query params
#define DEFAULT_CAPTURED_AT "2026-03-08T00:00:00Z"

// ==================== UPLOAD METADATA ====================
/**
 * @brief Capture metadata sent as query parameters with the HTTPS POST.
 *        All string fields are URL-encoded and passed as query params.
 */
struct UploadMetadata {
  const char *trapId;         // trap_id query param
  const char *capturedAt;     // captured_at query param (ISO 8601)
  const char *gpsLat;         // gps_lat query param
  const char *gpsLon;         // gps_lon query param
  uint16_t ldrValue;          // ldr_value query param
  const char *isFallen;       // is_fallen query param ("true" / "false")
  const char *batteryVoltage; // battery_voltage query param (mV as integer string)
  const char *solarVoltage;   // solar_voltage query param (mV as integer string, omitted if empty)
};

// ==================== API ====================

/**
 * @brief  Power on the A7670 modem, wait for SIM/network, activate data.
 *         Safe to call multiple times — subsequent calls return immediately
 *         if the modem is already initialised.
 * @return true if modem is online and data-ready.
 */
bool initModem();

/**
 * @brief  POST a JPEG image (held in RAM) to the Supabase Edge Function.
 * @param  photoData  Pointer to the JPEG bytes in RAM (e.g. photoBuffer).
 * @param  photoSize  Byte count of the JPEG image.
 * @param  meta       Capture metadata forwarded as HTTP headers.
 * @return HTTP status code (200/201 = success), or -1 on transport error.
 */
int uploadPhoto(const uint8_t *photoData, uint32_t photoSize,
                const UploadMetadata &meta);
