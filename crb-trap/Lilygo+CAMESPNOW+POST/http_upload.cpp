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

// ==================== MODULE-LOCAL MODEM ====================

static TinyGsm modem(SerialAT);
static bool modemInitialized = false;

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

  // ---- Initialise HTTPS service (AT+HTTPINIT) ----
  if (!modem.https_begin()) {
    Serial.println("[HTTP] HTTPS init failed");
    return -1;
  }

  // ---- Set endpoint URL ----
  if (!modem.https_set_url(SUPABASE_URL)) {
    Serial.println("[HTTP] Failed to set URL");
    modem.https_end();
    return -1;
  }

  // ---- Content type ----
  modem.https_set_content_type("image/jpeg");

  // ---- Custom headers (match Edge Function contract) ----
  modem.https_add_header("x-api-key", SUPABASE_API_KEY);
  modem.https_add_header("x-trap-id", meta.trapId);
  modem.https_add_header("x-captured-at", meta.capturedAt);
  modem.https_add_header("x-gps-lat", meta.gpsLat);
  modem.https_add_header("x-gps-lon", meta.gpsLon);
  modem.https_add_header("x-ldr-value", String(meta.ldrValue).c_str());
  modem.https_add_header("x-is-fallen", meta.isFallen);
  modem.https_add_header("x-battery-voltage", meta.batteryVoltage);

  // ---- POST the binary JPEG body ----
  // TinyGsmHttpsComm (A7670) has no body-size cap.
  // Internally: AT+HTTPDATA=<size>,10000 → DOWNLOAD → stream.write() → OK
  //             AT+HTTPACTION=1 → +HTTPACTION: 1,<status>,<len>
  // The 10 s HTTPDATA timeout is sufficient for images ≤ ~100 KB at 115200 baud.
  int httpCode = modem.https_post((const char *)photoData, (size_t)photoSize);

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
