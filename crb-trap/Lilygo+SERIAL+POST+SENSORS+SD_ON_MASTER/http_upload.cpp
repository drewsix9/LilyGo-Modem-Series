/**
 * @file      http_upload.cpp
 * @brief     Modem init plus raw binary POST upload to /echo-test.
 *            Optimized to keep modem / data session warm when possible.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "http_upload.h"
#include "utilities.h"
#include <TinyGsmClient.h>
#include <ctype.h>

#ifndef UPLOAD_TRAP_IMAGE_BASE_URL
#ifdef ECHO_TEST_BASE_URL
#define UPLOAD_TRAP_IMAGE_BASE_URL ECHO_TEST_BASE_URL
#else
#define UPLOAD_TRAP_IMAGE_BASE_URL "https://backend.crbtrap.me/upload-trap-image"
#endif
#endif

// ==================== MODULE-LOCAL MODEM ====================

static TinyGsm modem(SerialAT);
static bool modemInitialized = false;

// Reuse window bookkeeping only for logging / behavior hints.
// Even if the ESP32 resets and this is lost, warm detection still works
// because we probe the modem with AT first.
static unsigned long lastModemUseMs = 0;

// ==================== TUNABLES ====================
// How long we consider the modem "recently used" for log purposes.
static const unsigned long MODEM_WARM_HINT_MS = 5UL * 60UL * 1000UL;

// Retry counts / timeouts
static const int AT_PROBE_TRIES = 3;
static const int NETWORK_REG_MAX_RETRIES = 60;
static const int PDP_ACTIVATION_RETRIES = 3;

// ==================== HELPERS ====================

static uint32_t calculateCRC32(const uint8_t *data, uint32_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (uint32_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
    }
  }
  return crc ^ 0xFFFFFFFF;
}

static String urlEncode(const char *value) {
  if (!value) {
    return String("");
  }

  String encoded;
  const char *hex = "0123456789ABCDEF";

  for (size_t i = 0; value[i] != '\0'; ++i) {
    const uint8_t c = (uint8_t)value[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += (char)c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}

static String buildUploadFilename(const char *trapId, const char *crcHex) {
  String filename;
  filename += (trapId && strlen(trapId)) ? trapId : DEFAULT_TRAP_ID;
  filename += "-";
  filename += (crcHex && strlen(crcHex)) ? crcHex : "00000000";
  filename += ".jpg";
  return filename;
}

static String buildUploadTrapImageUrl(const UploadMetadata &meta,
                                      const char *crcHex,
                                      const String &filename) {
  const char *trapId =
      (meta.trapId && strlen(meta.trapId)) ? meta.trapId : DEFAULT_TRAP_ID;
  const char *capturedAt = (meta.capturedAt && strlen(meta.capturedAt))
                               ? meta.capturedAt
                               : DEFAULT_CAPTURED_AT;
  const char *gpsLat =
      (meta.gpsLat && strlen(meta.gpsLat)) ? meta.gpsLat : DEFAULT_GPS_LAT;
  const char *gpsLon =
      (meta.gpsLon && strlen(meta.gpsLon)) ? meta.gpsLon : DEFAULT_GPS_LON;
  const char *isFallen = (meta.isFallen && strlen(meta.isFallen))
                             ? meta.isFallen
                             : DEFAULT_IS_FALLEN;
  const char *batteryVoltage =
      (meta.batteryVoltage && strlen(meta.batteryVoltage))
          ? meta.batteryVoltage
          : DEFAULT_BATTERY_VOLTAGE;
  const char *solarVoltage =
      (meta.solarVoltage && strlen(meta.solarVoltage))
          ? meta.solarVoltage
          : DEFAULT_SOLAR_VOLTAGE;

  String url = String(UPLOAD_TRAP_IMAGE_BASE_URL);
  if (url.endsWith("/upload-trap-image")) {
    // already complete
  } else if (url.endsWith("/")) {
    url += "upload-trap-image";
  } else {
    url += "/upload-trap-image";
  }

  url += "?trap_id=";
  url += urlEncode(trapId);
  url += "&captured_at=";
  url += urlEncode(capturedAt);
  url += "&image_crc32=";
  url += urlEncode((crcHex && strlen(crcHex)) ? crcHex : "00000000");
  url += "&filename=";
  url += urlEncode(filename.c_str());
  url += "&gps_lat=";
  url += urlEncode(gpsLat);
  url += "&gps_lon=";
  url += urlEncode(gpsLon);
  url += "&ldr_value=";
  url += String((unsigned)meta.ldrValue);
  url += "&is_fallen=";
  url += urlEncode(isFallen);
  url += "&battery_voltage=";
  url += urlEncode(batteryVoltage);

  // Append solar voltage only if provided (non-empty)
  if (solarVoltage && strlen(solarVoltage) > 0) {
    url += "&solar_voltage=";
    url += urlEncode(solarVoltage);
  }

  return url;
}

static void beginModemSerial() {
  SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  delay(100);
}

static void configureModemPinsForAwakeState() {
#ifdef BOARD_POWERON_PIN
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
#endif

#ifdef MODEM_DTR_PIN
  pinMode(MODEM_DTR_PIN, OUTPUT);
  // Keep DTR low so the modem stays awake
  digitalWrite(MODEM_DTR_PIN, LOW);
#endif

  pinMode(BOARD_PWRKEY_PIN, OUTPUT);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);

#ifdef MODEM_RESET_PIN
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif
}

static bool modemRespondsToAT() {
  for (int i = 0; i < AT_PROBE_TRIES; i++) {
    if (modem.testAT(1000)) {
      return true;
    }
    delay(150);
  }
  return false;
}

static void hardResetAndPowerOnModem() {
  Serial.println("[MODEM] Performing cold modem bring-up...");

#ifdef MODEM_RESET_PIN
  Serial.println("[MODEM] Hardware reset pulse");
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
  delay(100);
  digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
  delay(2600);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif

  Serial.println("[MODEM] PWRKEY pulse");
  digitalWrite(BOARD_PWRKEY_PIN, LOW);
  delay(100);
  digitalWrite(BOARD_PWRKEY_PIN, HIGH);
  delay(MODEM_POWERON_PULSE_WIDTH_MS);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);
}

static bool waitForATAfterBoot() {
  Serial.println("[MODEM] Waiting for AT response...");
  int retry = 0;

  while (!modem.testAT(1000)) {
    Serial.print(".");
    if (retry++ > 30) {
      Serial.println("\n[MODEM] Retrying PWRKEY pulse...");
      digitalWrite(BOARD_PWRKEY_PIN, LOW);
      delay(100);
      digitalWrite(BOARD_PWRKEY_PIN, HIGH);
      delay(MODEM_POWERON_PULSE_WIDTH_MS);
      digitalWrite(BOARD_PWRKEY_PIN, LOW);
      retry = 0;
    }
  }

  Serial.println("\n[MODEM] AT OK");
  return true;
}

static bool ensureSimReady() {
  Serial.println("[MODEM] Checking SIM status...");
  SimStatus sim = SIM_ERROR;

  while (sim != SIM_READY) {
    sim = modem.getSimStatus();

    if (sim == SIM_LOCKED) {
      Serial.println("[MODEM] SIM card is locked!");
      return false;
    }

    delay(1000);
  }

  Serial.println("[MODEM] SIM ready");
  return true;
}

static bool ensureRegistered() {
  Serial.println("[MODEM] Checking network registration...");
  RegStatus status = modem.getRegistrationStatus();

  if (status == REG_OK_HOME || status == REG_OK_ROAMING) {
    Serial.println("[MODEM] Already registered");
    return true;
  }

  Serial.println("[MODEM] Waiting for network registration...");
  int retry = 0;

  while (status == REG_NO_RESULT || status == REG_SEARCHING ||
         status == REG_UNREGISTERED) {
    status = modem.getRegistrationStatus();

    if (status == REG_DENIED) {
      Serial.println("[MODEM] Registration denied — check APN / SIM balance");
      return false;
    }

    if (status == REG_OK_HOME || status == REG_OK_ROAMING) {
      Serial.println("[MODEM] Registered on network");
      return true;
    }

    int16_t sq = modem.getSignalQuality();
    Serial.printf("[MODEM] Signal: %d  status: %d\n", sq, status);

    if (retry++ > NETWORK_REG_MAX_RETRIES) {
      Serial.println("[MODEM] Registration timeout!");
      return false;
    }

    delay(1000);
  }

  if (status == REG_OK_HOME || status == REG_OK_ROAMING) {
    Serial.println("[MODEM] Registered on network");
    return true;
  }

  Serial.printf("[MODEM] Unexpected registration state: %d\n", status);
  return false;
}

static bool ensureDataSession() {
  String ip = modem.getLocalIP();
  if (ip.length() > 0 && ip != "0.0.0.0") {
    Serial.printf("[MODEM] Data session already active, IP: %s\n", ip.c_str());
    return true;
  }

  Serial.println("[MODEM] Activating PDP / data session...");
  int retry = PDP_ACTIVATION_RETRIES;

  while (retry--) {
    if (modem.setNetworkActive("", false)) {
      delay(3000);

      ip = modem.getLocalIP();
      Serial.printf("[MODEM] IP: %s\n", ip.c_str());

      if (ip.length() > 0 && ip != "0.0.0.0") {
        return true;
      }
    }

    Serial.println("[MODEM] Network activation failed, retrying...");
    delay(3000);
  }

  Serial.println("[MODEM] Could not activate data!");
  return false;
}

static bool tryWarmModemRecovery() {
  beginModemSerial();
  configureModemPinsForAwakeState();

  if (!modemRespondsToAT()) {
    return false;
  }

  Serial.println("[MODEM] Warm modem detected — skipping reset / re-init");

  if (!ensureSimReady()) {
    return false;
  }

  if (!ensureRegistered()) {
    return false;
  }

  if (!ensureDataSession()) {
    return false;
  }

  modemInitialized = true;
  lastModemUseMs = millis();
  return true;
}

static bool coldBootModemAndAttach() {
  beginModemSerial();
  configureModemPinsForAwakeState();
  hardResetAndPowerOnModem();

  if (!waitForATAfterBoot()) {
    return false;
  }

  if (!ensureSimReady()) {
    return false;
  }

  if (!ensureRegistered()) {
    return false;
  }

  if (!ensureDataSession()) {
    return false;
  }

  modemInitialized = true;
  lastModemUseMs = millis();
  return true;
}

// ==================== MODEM INITIALISATION ====================

bool initModem() {
  unsigned long now = millis();

  if (modemInitialized) {
    Serial.println("[MODEM] Cached modem state present, verifying...");
    if (tryWarmModemRecovery()) {
      if ((now - lastModemUseMs) <= MODEM_WARM_HINT_MS) {
        Serial.println("[MODEM] Reusing warm modem/data session");
      }
      return true;
    }

    Serial.println("[MODEM] Cached state stale; falling back to cold init");
    modemInitialized = false;
  }

  // Important optimization:
  // Even if the ESP32 restarted and lost modemInitialized, the modem may still
  // be powered and registered. Probe first before forcing a reset.
  Serial.println("[MODEM] Probing for already-alive modem...");
  if (tryWarmModemRecovery()) {
    Serial.println("[MODEM] Warm modem/data session recovered");
    return true;
  }

  Serial.println("[MODEM] No warm session found, doing full initialization...");
  return coldBootModemAndAttach();
}

// ==================== PHOTO UPLOAD ====================

int uploadPhoto(const uint8_t *photoData, uint32_t photoSize,
                const UploadMetadata &meta) {
  if (!photoData || photoSize == 0) {
    Serial.println("[HTTP] No photo data to upload");
    return -1;
  }

  if (!initModem()) {
    Serial.println("[HTTP] Modem not ready for /upload-trap-image");
    return -1;
  }

  uint32_t photoCRC = calculateCRC32(photoData, photoSize);
  char crcHex[9] = {0};
  snprintf(crcHex, sizeof(crcHex), "%08lX", (unsigned long)photoCRC);

  const char *trapId =
      (meta.trapId && strlen(meta.trapId)) ? meta.trapId : DEFAULT_TRAP_ID;
  const String filename = buildUploadFilename(trapId, crcHex);
  const String uploadUrl = buildUploadTrapImageUrl(meta, crcHex, filename);

  Serial.printf("[HTTP] Uploading %u bytes to /upload-trap-image\n", photoSize);
  Serial.printf("[HTTP] trap_id=%s crc32=%s\n", trapId, crcHex);
  Serial.printf("[HTTP] filename=%s\n", filename.c_str());
  Serial.printf("[HTTP] URL: %s\n", uploadUrl.c_str());

  // Even when the modem/data session is kept warm, we still start and end
  // the HTTPS transaction per request. This is much safer with SIMCOM HTTP(S)
  // AT flows while still avoiding the biggest delay sources:
  // modem boot, network registration, and PDP activation.
  if (!modem.https_begin()) {
    Serial.println("[HTTP] HTTPS init failed");
    modemInitialized = false;
    return -1;
  }

  if (!modem.https_set_url(uploadUrl)) {
    Serial.println("[HTTP] Failed to set /upload-trap-image URL");
    modem.https_end();
    return -1;
  }

  if (!modem.https_set_content_type("application/octet-stream")) {
    Serial.println("[HTTP] Failed to set content type");
    modem.https_end();
    return -1;
  }

  if (!modem.https_post_begin(photoSize)) {
    Serial.println("[HTTP] https_post_begin failed");
    modem.https_end();
    return -1;
  }

  size_t writtenBytes = 0;
  const size_t chunkSize = 512;

  for (size_t offset = 0; offset < photoSize; offset += chunkSize) {
    size_t remaining = (size_t)photoSize - offset;
    size_t thisChunk = remaining > chunkSize ? chunkSize : remaining;

    if (!modem.https_post_write(photoData + offset, thisChunk)) {
      Serial.printf("[HTTP] Failed to write body chunk at offset %u\n",
                    (unsigned)offset);
      modem.https_end();
      return -1;
    }

    writtenBytes += thisChunk;
    yield();
  }

  if (writtenBytes != photoSize) {
    Serial.printf("[HTTP] Body write mismatch: wrote=%u expected=%u\n",
                  (unsigned)writtenBytes, (unsigned)photoSize);
    modem.https_end();
    return -1;
  }

  int httpCode = modem.https_post_end();
  Serial.printf("[HTTP] /upload-trap-image status: %d\n", httpCode);

  String body = modem.https_body();
  if (body.length() > 0) {
    Serial.printf("[HTTP] /upload-trap-image response: %s\n", body.c_str());
  } else {
    Serial.println("[HTTP] /upload-trap-image response body is empty");
  }

  modem.https_end();

  // Mark the modem as recently usable for the next cycle.
  modemInitialized = true;
  lastModemUseMs = millis();

  return httpCode;
}