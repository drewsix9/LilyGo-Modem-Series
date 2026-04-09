/**
 * @file      http_upload.cpp
 * @brief     HTTPS upload implementation — modem init, network registration,
 *            and binary JPEG POST to Supabase via SIMCOM A7670 AT commands.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "http_upload.h"
#include "utilities.h"
#include <TinyGsmClient.h>

#ifndef SUPABASE_URL
#error "SUPABASE_URL is not defined"
#endif

#ifndef SUPABASE_API_KEY
#error "SUPABASE_API_KEY is not defined"
#endif

// ==================== MODULE-LOCAL MODEM ====================

static TinyGsm modem(SerialAT);
static bool modemInitialized = false;

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

// ==================== MODEM INITIALISATION ====================

bool initModem() {
  if (modemInitialized)
    return true;

  Serial.println("[MODEM] Starting initialization...");

  // Open the modem serial port
  SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  // Supply power to the modem module
#ifdef BOARD_POWERON_PIN
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
#endif

  // Hardware reset
#ifdef MODEM_RESET_PIN
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
  delay(100);
  digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
  delay(2600);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif

  // Keep DTR low so the modem stays awake
#ifdef MODEM_DTR_PIN
  pinMode(MODEM_DTR_PIN, OUTPUT);
  digitalWrite(MODEM_DTR_PIN, LOW);
#endif

  // Power-key pulse
  pinMode(BOARD_PWRKEY_PIN, OUTPUT);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);
  delay(100);
  digitalWrite(BOARD_PWRKEY_PIN, HIGH);
  delay(MODEM_POWERON_PULSE_WIDTH_MS);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);

  // ---- Wait for the modem to accept AT commands ----
  Serial.println("[MODEM] Waiting for AT response...");
  int retry = 0;
  while (!modem.testAT(1000)) {
    Serial.print(".");
    if (retry++ > 30) {
      // Re-toggle PWRKEY
      digitalWrite(BOARD_PWRKEY_PIN, LOW);
      delay(100);
      digitalWrite(BOARD_PWRKEY_PIN, HIGH);
      delay(MODEM_POWERON_PULSE_WIDTH_MS);
      digitalWrite(BOARD_PWRKEY_PIN, LOW);
      retry = 0;
    }
  }
  Serial.println("\n[MODEM] AT OK");

  // ---- SIM card ----
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

  // ---- Network registration ----
  Serial.println("[MODEM] Waiting for network registration...");
  RegStatus status = REG_NO_RESULT;
  retry = 0;
  while (status == REG_NO_RESULT || status == REG_SEARCHING ||
         status == REG_UNREGISTERED) {
    status = modem.getRegistrationStatus();
    if (status == REG_DENIED) {
      Serial.println("[MODEM] Registration denied — check APN / SIM balance");
      return false;
    }
    if (status == REG_OK_HOME || status == REG_OK_ROAMING)
      break;

    int16_t sq = modem.getSignalQuality();
    Serial.printf("[MODEM] Signal: %d  status: %d\n", sq, status);
    if (retry++ > 60) {
      Serial.println("[MODEM] Registration timeout!");
      return false;
    }
    delay(1000);
  }
  Serial.println("[MODEM] Registered on network");

  // ---- Activate PDP / data ----
  retry = 3;
  while (retry--) {
    if (modem.setNetworkActive("", false))
      break;
    Serial.println("[MODEM] Network activation failed, retrying...");
    delay(3000);
  }
  if (retry < 0) {
    Serial.println("[MODEM] Could not activate data!");
    return false;
  }

  delay(3000);
  String ip = modem.getLocalIP();
  Serial.printf("[MODEM] IP: %s\n", ip.c_str());

  modemInitialized = true;
  return true;
}

// ==================== PHOTO UPLOAD ====================

int uploadPhoto(const uint8_t *photoData, uint32_t photoSize,
                const UploadMetadata &meta) {
  if (!photoData || photoSize == 0) {
    Serial.println("[HTTP] No photo data to upload");
    return -1;
  }

  Serial.printf("[HTTP] Uploading %u bytes to Supabase...\n", photoSize);

  uint32_t photoCRC = calculateCRC32(photoData, photoSize);
  char crcHex[9] = {0};
  snprintf(crcHex, sizeof(crcHex), "%08lX", (unsigned long)photoCRC);
  Serial.printf("[HTTP] Photo CRC32: %s\n", crcHex);

  // ---- Initialise HTTPS service (AT+HTTPINIT) ----
  if (!modem.https_begin()) {
    Serial.println("[HTTP] HTTPS init failed");
    return -1;
  }

  // ---- Build URL with query parameters (A7670 doesn't reliably send custom headers) ----
  const char *trapId =
      (meta.trapId && strlen(meta.trapId)) ? meta.trapId : DEFAULT_TRAP_ID;
  String urlWithParams = String(SUPABASE_URL);
  urlWithParams += "?trap_id=" + String(trapId);
  urlWithParams += "&captured_at=" + String(meta.capturedAt);
  urlWithParams += "&gps_lat=" + String(meta.gpsLat);
  urlWithParams += "&gps_lon=" + String(meta.gpsLon);
  urlWithParams += "&ldr_value=" + String(meta.ldrValue);
  urlWithParams += "&is_fallen=" + String(meta.isFallen);
  urlWithParams += "&battery_voltage=" + String(meta.batteryVoltage);
  urlWithParams += "&api_key=" + String(SUPABASE_API_KEY);
  urlWithParams += "&image_crc32=" + String(crcHex);

  // ---- Set endpoint URL with parameters ----
  Serial.printf("[HTTP] Setting URL: %s\n", urlWithParams.c_str());
  if (!modem.https_set_url(urlWithParams)) {
    Serial.println("[HTTP] Failed to set URL");
    modem.https_end();
    return -1;
  }

  // ---- Multipart form-data content type ----
  static const char *kBoundary = "----ESP32_A7670_Boundary";
  String contentType = "multipart/form-data; boundary=";
  contentType += kBoundary;
  if (!modem.https_set_content_type(contentType.c_str())) {
    Serial.println("[HTTP] Failed to set content type");
    modem.https_end();
    return -1;
  }

  Serial.println("[HTTP] Metadata sent via query parameters");

  // ---- Build multipart envelope ----
  String head = "--";
  head += kBoundary;
  head += "\r\n";
  head +=
      "Content-Disposition: form-data; name=\"file\"; filename=\"photo.jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n";
  head += "Content-Transfer-Encoding: binary\r\n\r\n";

  String tail = "\r\n--";
  tail += kBoundary;
  tail += "--\r\n";

  const size_t totalBodySize = (size_t)head.length() + (size_t)photoSize +
                               (size_t)tail.length();
  Serial.printf("[HTTP] Multipart total body size: %u bytes\n",
                (unsigned)totalBodySize);
  Serial.printf("[HTTP] Multipart parts: head=%u image=%u tail=%u\n",
                (unsigned)head.length(), (unsigned)photoSize,
                (unsigned)tail.length());

  // ---- Begin streamed POST body ----
  if (!modem.https_post_begin(totalBodySize)) {
    Serial.println("[HTTP] https_post_begin failed");
    modem.https_end();
    return -1;
  }

  size_t writtenBytes = 0;

  if (!modem.https_post_write((const uint8_t *)head.c_str(),
                              (size_t)head.length())) {
    Serial.println("[HTTP] Failed to write multipart header");
    modem.https_end();
    return -1;
  }
  writtenBytes += (size_t)head.length();

  const size_t photoChunkSize = 512;
  for (size_t offset = 0; offset < photoSize; offset += photoChunkSize) {
    size_t remaining = (size_t)photoSize - offset;
    size_t chunkSize = remaining > photoChunkSize ? photoChunkSize : remaining;
    if (!modem.https_post_write(photoData + offset, chunkSize)) {
      Serial.printf("[HTTP] Failed at offset %u\n", (unsigned)offset);
      modem.https_end();
      return -1;
    }
    writtenBytes += chunkSize;
    yield();
  }

  if (!modem.https_post_write((const uint8_t *)tail.c_str(),
                              (size_t)tail.length())) {
    Serial.println("[HTTP] Failed to write multipart footer");
    modem.https_end();
    return -1;
  }
  writtenBytes += (size_t)tail.length();

  Serial.printf("[HTTP] Multipart bytes written: %u/%u\n", (unsigned)writtenBytes,
                (unsigned)totalBodySize);

  if (writtenBytes != totalBodySize) {
    Serial.printf("[HTTP] Size mismatch before HTTPACTION: wrote=%u expected=%u\n",
                  (unsigned)writtenBytes, (unsigned)totalBodySize);
    modem.https_end();
    return -1;
  }

  int httpCode = modem.https_post_end();

  if (httpCode == 200 || httpCode == 201) {
    Serial.printf("[HTTP] Upload success! HTTP %d\n", httpCode);
  } else {
    Serial.printf("[HTTP] Upload failed! HTTP %d\n", httpCode);
  }

  // ---- Log the response body ----
  String body = modem.https_body();
  if (body.length() > 0) {
    Serial.printf("[HTTP] Response: %s\n", body.c_str());
  }

  // ---- Terminate HTTPS (AT+HTTPTERM) ----
  modem.https_end();

  return httpCode;
}
