/**
 * @file      http_upload.cpp
 * @brief     FTP upload implementation — modem init, network registration,
 *            and binary JPEG STOR via SIMCOM A7670 data channel.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "http_upload.h"
#include "utilities.h"
#include <TinyGsmClient.h>
#include <ctype.h>

// ==================== MODULE-LOCAL MODEM ====================

static TinyGsm modem(SerialAT);
static bool modemInitialized = false;

static bool ftpReadResponse(TinyGsmClient &client, uint32_t timeoutMs,
                            int &outCode, String &outResponse) {
  outCode = -1;
  outResponse = "";

  uint32_t start = millis();
  int firstCode = -1;
  bool multiline = false;
  String currentLine = "";

  while ((millis() - start) < timeoutMs) {
    while (client.available()) {
      char c = (char)client.read();
      outResponse += c;

      if (c == '\r') {
        continue;
      }

      if (c != '\n') {
        currentLine += c;
        continue;
      }

      if (currentLine.length() >= 3 && isdigit((int)currentLine[0]) &&
          isdigit((int)currentLine[1]) && isdigit((int)currentLine[2])) {
        int lineCode = currentLine.substring(0, 3).toInt();
        char sep = (currentLine.length() > 3) ? currentLine[3] : ' ';

        if (firstCode < 0) {
          firstCode = lineCode;
          multiline = (sep == '-');
          if (!multiline) {
            outCode = lineCode;
            return true;
          }
        } else if (multiline && lineCode == firstCode && sep == ' ') {
          outCode = lineCode;
          return true;
        }
      }

      currentLine = "";
    }
    delay(5);
    yield();
  }

  return false;
}

static bool ftpSendCommand(TinyGsmClient &client, const String &command,
                           uint32_t timeoutMs, int &outCode,
                           String &outResponse) {
  client.print(command);
  client.print("\r\n");
  Serial.printf("[FTP] >> %s\n", command.c_str());

  if (!ftpReadResponse(client, timeoutMs, outCode, outResponse)) {
    Serial.println("[FTP] << (timeout)");
    return false;
  }

  Serial.printf("[FTP] << %s", outResponse.c_str());
  return true;
}

static bool ftpSendCommandExpect(TinyGsmClient &client, const String &command,
                                 int expectedCode, uint32_t timeoutMs,
                                 int &outCode, String &outResponse) {
  if (!ftpSendCommand(client, command, timeoutMs, outCode, outResponse)) {
    return false;
  }
  return outCode == expectedCode;
}

static bool parsePasv(const String &response, String &outIp, uint16_t &outPort) {
  int open = response.indexOf('(');
  int close = response.indexOf(')', open + 1);
  if (open < 0 || close < 0) {
    return false;
  }

  String nums = response.substring(open + 1, close);
  int values[6] = {0};
  int idx = 0;
  int start = 0;

  while (idx < 6) {
    int comma = nums.indexOf(',', start);
    String part = (comma >= 0) ? nums.substring(start, comma) : nums.substring(start);
    if (part.length() == 0) {
      return false;
    }
    values[idx++] = part.toInt();
    if (comma < 0) {
      break;
    }
    start = comma + 1;
  }

  if (idx != 6) {
    return false;
  }

  for (int i = 0; i < 6; i++) {
    if (values[i] < 0 || values[i] > 255) {
      return false;
    }
  }

  outIp = String(values[0]) + "." + String(values[1]) + "." + String(values[2]) +
          "." + String(values[3]);
  outPort = (uint16_t)((values[4] << 8) | values[5]);
  return true;
}

static bool ftpUploadBinary(const uint8_t *photoData, uint32_t photoSize,
                            const char *remoteFilename) {
  TinyGsmClient controlClient(modem);
  TinyGsmClient dataClient(modem);

  Serial.printf("[FTP] Connecting to %s:%d\n", FTP_HOST, (int)FTP_PORT);
  if (!controlClient.connect(FTP_HOST, FTP_PORT)) {
    Serial.println("[FTP] Control channel connect failed");
    return false;
  }

  int code = -1;
  String response;
  if (!ftpReadResponse(controlClient, 15000, code, response) || code != 220) {
    Serial.printf("[FTP] Greeting failed (code=%d)\n", code);
    controlClient.stop();
    return false;
  }
  Serial.printf("[FTP] << %s", response.c_str());

  if (!ftpSendCommand(controlClient, String("USER ") + FTP_USER, 10000, code, response)) {
    controlClient.stop();
    return false;
  }
  if (code == 331) {
    if (!ftpSendCommandExpect(controlClient, String("PASS ") + FTP_PASS, 230, 10000,
                              code, response)) {
      Serial.println("[FTP] PASS rejected");
      controlClient.stop();
      return false;
    }
  } else if (code != 230) {
    Serial.printf("[FTP] USER rejected (code=%d)\n", code);
    controlClient.stop();
    return false;
  }

  if (!ftpSendCommandExpect(controlClient, "TYPE I", 200, 10000, code, response)) {
    Serial.println("[FTP] Failed to set binary mode");
    controlClient.stop();
    return false;
  }

  String remoteDir = String(FTP_REMOTE_DIR);
  if (remoteDir.length() > 0 && remoteDir != "/") {
    if (!ftpSendCommandExpect(controlClient, String("CWD ") + remoteDir, 250, 10000,
                              code, response)) {
      Serial.printf("[FTP] Failed to CWD %s\n", remoteDir.c_str());
      controlClient.stop();
      return false;
    }
  }

  if (!ftpSendCommand(controlClient, "PASV", 10000, code, response) || code != 227) {
    Serial.println("[FTP] PASV failed");
    controlClient.stop();
    return false;
  }

  String pasvIp;
  uint16_t pasvPort = 0;
  if (!parsePasv(response, pasvIp, pasvPort)) {
    Serial.println("[FTP] Could not parse PASV response");
    controlClient.stop();
    return false;
  }

  Serial.printf("[FTP] Data endpoint: %s:%u\n", pasvIp.c_str(), (unsigned)pasvPort);
  if (!dataClient.connect(pasvIp.c_str(), pasvPort)) {
    Serial.println("[FTP] Data channel connect failed");
    controlClient.stop();
    return false;
  }

  if (!ftpSendCommand(controlClient, String("STOR ") + remoteFilename, 10000, code,
                      response) ||
      (code != 125 && code != 150)) {
    Serial.printf("[FTP] STOR rejected (code=%d)\n", code);
    dataClient.stop();
    controlClient.stop();
    return false;
  }

  size_t writtenBytes = 0;
  const size_t chunkSize = 512;
  for (size_t offset = 0; offset < photoSize; offset += chunkSize) {
    size_t remaining = (size_t)photoSize - offset;
    size_t thisChunk = remaining > chunkSize ? chunkSize : remaining;
    size_t sent = dataClient.write(photoData + offset, thisChunk);
    if (sent != thisChunk) {
      Serial.printf("[FTP] Data write failed at offset %u\n", (unsigned)offset);
      dataClient.stop();
      controlClient.stop();
      return false;
    }
    writtenBytes += sent;
    if ((writtenBytes % (16 * 1024)) == 0 || writtenBytes == photoSize) {
      Serial.printf("[FTP] Upload progress: %u/%u\n", (unsigned)writtenBytes,
                    (unsigned)photoSize);
    }
    yield();
  }
  dataClient.stop();

  if (!ftpReadResponse(controlClient, 20000, code, response) || code != 226) {
    Serial.printf("[FTP] Transfer finalize failed (code=%d)\n", code);
    controlClient.stop();
    return false;
  }
  Serial.printf("[FTP] << %s", response.c_str());

  ftpSendCommand(controlClient, "QUIT", 5000, code, response);
  controlClient.stop();
  return writtenBytes == photoSize;
}

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
    Serial.println("[FTP] No photo data to upload");
    return -1;
  }

  const char *fallbackName = "IMG_0000.jpg";
  const char *remoteFilename =
      (meta.photoFilename && strlen(meta.photoFilename)) ? meta.photoFilename : fallbackName;

  Serial.printf("[FTP] Uploading %u bytes as %s\n", photoSize, remoteFilename);

  uint32_t photoCRC = calculateCRC32(photoData, photoSize);
  char crcHex[9] = {0};
  snprintf(crcHex, sizeof(crcHex), "%08lX", (unsigned long)photoCRC);
  Serial.printf("[FTP] Photo CRC32: %s\n", crcHex);

  if (ftpUploadBinary(photoData, photoSize, remoteFilename)) {
    Serial.println("[FTP] Upload success");
    return 201;
  }

  Serial.println("[FTP] Upload failed");
  return -1;
}
