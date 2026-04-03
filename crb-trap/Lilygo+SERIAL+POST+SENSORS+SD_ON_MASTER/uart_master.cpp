/**
 * @file      uart_master.cpp
 * @brief     UART hybrid master-side communication implementation.
 *            ASCII control plane + SerialTransfer binary JPEG chunks.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "uart_master.h"
#include "ESP32CAM_Slave/sdcard.h"
#include "http_upload.h"
#include "uart_protocol.h"
#include <Arduino.h>
#include <SerialTransfer.h>
#include <WiFi.h>
#include <esp_bt.h>

// ==================== STATE VARIABLES ====================

volatile bool camReady = false;
volatile bool photoStartReceived = false;
volatile bool photoEndReceived = false;
volatile bool slaveError = false;
volatile uint8_t slaveErrorCode = 0;
volatile bool transferAborted = false;
volatile uint16_t lastAckedPacket = 0;

uint8_t *photoBuffer = nullptr;
volatile uint32_t photoSize = 0;
volatile uint16_t totalPacketsExpected = 0;
volatile uint16_t packetsReceived = 0;
volatile uint32_t receivedCRC32 = 0;
volatile uint32_t lastPacketTime = 0;

uint16_t capturedLuxValue = 0;
bool capturedIsFallen = false;

static HardwareSerial camUart(2); // Serial2 on GPIO18/19
static SerialTransfer camTransfer;
static bool transferInitialized = false;

static char asciiRx[96] = {0};
static uint8_t asciiRxIdx = 0;

// ==================== INTERNAL HELPERS ====================

static void resetTransferState() {
  camReady = false;
  photoStartReceived = false;
  photoEndReceived = false;
  slaveError = false;
  slaveErrorCode = 0;
  transferAborted = false;
  lastAckedPacket = 0;
  photoSize = 0;
  totalPacketsExpected = 0;
  packetsReceived = 0;
  receivedCRC32 = 0;
  lastPacketTime = millis();
}

static bool startsWith(const char *line, const char *prefix) {
  return line && prefix && strncmp(line, prefix, strlen(prefix)) == 0;
}

static bool readAsciiLine(char *outLine, size_t outLen) {
  if (!outLine || outLen == 0) {
    return false;
  }

  while (camUart.available()) {
    int c = camUart.read();
    if (c < 0) {
      break;
    }

    if (c == '\n' || c == '\r') {
      if (asciiRxIdx == 0) {
        continue;
      }
      size_t n = (asciiRxIdx < outLen - 1) ? asciiRxIdx : (outLen - 1);
      memcpy(outLine, asciiRx, n);
      outLine[n] = '\0';
      asciiRxIdx = 0;
      memset(asciiRx, 0, sizeof(asciiRx));
      return true;
    }

    if (asciiRxIdx < sizeof(asciiRx) - 1) {
      asciiRx[asciiRxIdx++] = (char)c;
    } else {
      asciiRxIdx = 0;
      memset(asciiRx, 0, sizeof(asciiRx));
    }
  }

  return false;
}

static bool waitForSizeLine(uint32_t timeoutMs, uint32_t &outSize) {
  char line[96] = {0};
  uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    if (readAsciiLine(line, sizeof(line))) {
      if (startsWith(line, CMD_SIZE ":")) {
        unsigned long parsed = 0;
        if (sscanf(line, "SIZE:%lu", &parsed) == 1 && parsed > 0 && parsed <= PHOTO_MAX_SIZE) {
          outSize = (uint32_t)parsed;
          return true;
        }
        Serial.printf("[UART] Invalid SIZE line: %s\n", line);
        slaveError = true;
        slaveErrorCode = ERR_INVALID_PARAMS;
        return false;
      }

      if (startsWith(line, CMD_ERROR ":")) {
        unsigned int err = 0;
        if (sscanf(line, "ERROR:%u", &err) == 1) {
          slaveErrorCode = (uint8_t)err;
        }
        slaveError = true;
        return false;
      }
    }

    delay(5);
  }

  return false;
}

static bool waitForDoneLine(uint32_t timeoutMs, uint32_t &outRemoteCRC, bool &outHasCRC) {
  char line[96] = {0};
  uint32_t start = millis();
  outRemoteCRC = 0;
  outHasCRC = false;

  while (millis() - start < timeoutMs) {
    if (readAsciiLine(line, sizeof(line))) {
      if (strcmp(line, CMD_DONE) == 0) {
        return true;
      }
      if (startsWith(line, CMD_DONE ":")) {
        unsigned long parsedCRC = 0;
        if (sscanf(line, "DONE:%lx", &parsedCRC) == 1) {
          outRemoteCRC = (uint32_t)parsedCRC;
          outHasCRC = true;
          return true;
        }
        Serial.printf("[PHOTO] Invalid DONE line: %s\n", line);
        slaveError = true;
        slaveErrorCode = ERR_INVALID_PARAMS;
        return false;
      }
      if (startsWith(line, CMD_ERROR ":")) {
        unsigned int err = 0;
        if (sscanf(line, "ERROR:%u", &err) == 1) {
          slaveErrorCode = (uint8_t)err;
        }
        slaveError = true;
        return false;
      }
    }

    delay(5);
  }

  return false;
}

// ==================== PUBLIC API ====================

void initESPNOW() {
  Serial.println("[LINK] Initializing UART camera link...");
  WiFi.mode(WIFI_OFF);
  btStop();

  camUart.begin(MASTER_UART_BAUD_RATE, SERIAL_8N1, 19, 18);
  camTransfer.begin(camUart, false);
  transferInitialized = true;

  resetTransferState();

  if (photoBuffer) {
    free(photoBuffer);
    photoBuffer = nullptr;
  }

  Serial.println("[LINK] UART ready: Serial2 @115200, RX=GPIO19 TX=GPIO18");
}

void cleanupESPNOW() {
  Serial.println("[LINK] Cleaning up UART camera link...");

  if (photoBuffer) {
    free(photoBuffer);
    photoBuffer = nullptr;
  }

  if (transferInitialized) {
    camTransfer.reset();
    transferInitialized = false;
  }

  camUart.flush();
  camUart.end();

  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.println("[LINK] UART camera link stopped");
}

bool waitForCameraReady() {
  char line[96] = {0};
  uint32_t start = millis();

  camReady = false;
  Serial.println("[LINK] Waiting for READY...");

  while (millis() - start < READY_TIMEOUT_MS) {
    if (readAsciiLine(line, sizeof(line))) {
      if (strcmp(line, CMD_READY) == 0 || startsWith(line, CMD_READY ":")) {
        camReady = true;
        Serial.printf("[LINK] READY received after %lums\n", millis() - start);
        return true;
      }
      if (startsWith(line, CMD_ERROR ":")) {
        unsigned int err = 0;
        if (sscanf(line, "ERROR:%u", &err) == 1) {
          slaveErrorCode = (uint8_t)err;
        }
        slaveError = true;
        return false;
      }
    }

    delay(5);
  }

  Serial.printf("[TIMEOUT] READY not received after %dms\n", READY_TIMEOUT_MS);
  return false;
}

bool sendPhotoCommand(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality) {
  char cmd[64] = {0};
  snprintf(cmd, sizeof(cmd), "%s:%u,%u,%u,%u\n", CMD_SNAP, lux, width, height, quality);

  size_t sent = camUart.write((const uint8_t *)cmd, strlen(cmd));
  camUart.flush();

  Serial.printf("[LINK] >> %s", cmd);
  return sent == strlen(cmd);
}

bool receivePhoto() {
  if (!transferInitialized) {
    Serial.println("[ERROR] UART transfer not initialized");
    return false;
  }

  resetTransferState();

  uint32_t expectedSize = 0;
  Serial.println("[PHOTO] Waiting for SIZE response...");
  if (!waitForSizeLine(SNAP_CAPTURE_TIMEOUT_MS, expectedSize)) {
    if (slaveError) {
      Serial.printf("[PHOTO] Slave ERROR:%u\n", slaveErrorCode);
    } else {
      Serial.println("[PHOTO] SIZE timeout");
    }
    return false;
  }

  photoSize = expectedSize;
  totalPacketsExpected = (uint16_t)((photoSize + PHOTO_CHUNK_DATA_MAX - 1) / PHOTO_CHUNK_DATA_MAX);
  photoStartReceived = true;

  if (photoBuffer) {
    free(photoBuffer);
    photoBuffer = nullptr;
  }

  photoBuffer = (uint8_t *)ps_malloc(photoSize);
  if (!photoBuffer) {
    Serial.printf("[PHOTO] ps_malloc failed for %lu bytes\n", photoSize);
    return false;
  }

  memset(photoBuffer, 0, photoSize);

  camUart.print(CMD_SEND);
  camUart.print("\n");
  camUart.flush();
  Serial.printf("[LINK] >> %s\n", CMD_SEND);

  uint32_t startTime = millis();
  uint32_t bytesReceived = 0;
  uint16_t expectedChunkId = 0;

  while (bytesReceived < photoSize) {
    if (millis() - startTime > PHOTO_TIMEOUT_MS) {
      Serial.println("[PHOTO] Absolute transfer timeout");
      transferAborted = true;
      break;
    }

    if (!camTransfer.available()) {
      if (millis() - lastPacketTime > SERIAL_CHUNK_TIMEOUT_MS) {
        Serial.println("[PHOTO] Chunk receive timeout");
        transferAborted = true;
        break;
      }
      delay(2);
      continue;
    }

    uint8_t packetId = camTransfer.currentPacketID();
    if (packetId != PACKET_ID_PHOTO_CHUNK) {
      Serial.printf("[PHOTO] Ignoring unexpected packet ID: 0x%02X\n", packetId);
      continue;
    }

    uint16_t idx = 0;
    uint16_t chunkId = 0;
    idx = camTransfer.rxObj(chunkId, idx, sizeof(chunkId));

    if (chunkId != expectedChunkId) {
      Serial.printf("[PHOTO] Sequence mismatch: expected %u got %u\n", expectedChunkId, chunkId);
      transferAborted = true;
      break;
    }

    size_t payloadLen = (camTransfer.bytesRead > idx) ? (camTransfer.bytesRead - idx) : 0;
    if (payloadLen == 0 || payloadLen > PHOTO_CHUNK_DATA_MAX) {
      Serial.printf("[PHOTO] Invalid payload length: %u\n", (unsigned)payloadLen);
      transferAborted = true;
      break;
    }

    if (bytesReceived + payloadLen > photoSize) {
      Serial.println("[PHOTO] Payload would overflow destination buffer");
      transferAborted = true;
      break;
    }

    uint8_t tmp[PHOTO_CHUNK_DATA_MAX] = {0};
    camTransfer.rxObj(tmp, idx, payloadLen);
    memcpy(photoBuffer + bytesReceived, tmp, payloadLen);

    bytesReceived += payloadLen;
    packetsReceived++;
    lastAckedPacket = chunkId;
    expectedChunkId++;
    lastPacketTime = millis();

    if ((packetsReceived % 20) == 0 || bytesReceived == photoSize) {
      Serial.printf("[PHOTO] Progress: %lu/%lu bytes (%u/%u chunks)\n",
                    bytesReceived, photoSize, packetsReceived, totalPacketsExpected);
    }
  }

  if (transferAborted) {
    free(photoBuffer);
    photoBuffer = nullptr;
    return false;
  }

  if (bytesReceived != photoSize) {
    Serial.printf("[PHOTO] Incomplete receive: %lu/%lu bytes\n", bytesReceived, photoSize);
    free(photoBuffer);
    photoBuffer = nullptr;
    return false;
  }

  uint32_t remoteCRC = 0;
  bool hasRemoteCRC = false;
  photoEndReceived = waitForDoneLine(ASCII_RESPONSE_TIMEOUT_MS, remoteCRC, hasRemoteCRC);
  if (!photoEndReceived) {
    Serial.println("[PHOTO] DONE timeout");
    free(photoBuffer);
    photoBuffer = nullptr;
    return false;
  }

  uint32_t calculatedCRC = calculateCRC32(photoBuffer, photoSize);
  receivedCRC32 = calculatedCRC;

  if (hasRemoteCRC) {
    Serial.printf("[PHOTO] CRC local=%08X remote=%08X\n", calculatedCRC, remoteCRC);
    if (calculatedCRC != remoteCRC) {
      Serial.println("[PHOTO] CRC mismatch: payload corrupted in transit");
      free(photoBuffer);
      photoBuffer = nullptr;
      return false;
    }
  } else {
    Serial.println("[PHOTO] WARNING: remote CRC missing in DONE line");
  }

  Serial.printf("[PHOTO] Transfer complete: %lu bytes CRC32=%08X\n", photoSize, calculatedCRC);

  // Save the validated payload so SD copy reflects post-transfer integrity.
  if (isSDAvailable()) {
    if (!savePhotoToSD(photoBuffer, photoSize)) {
      Serial.println("[SD] WARN: Master failed to archive received photo, continuing upload");
    }
  } else {
    Serial.println("[SD] Master SD not available, skipping archive");
  }

  // Upload photo to Supabase via A7670 modem.
  UploadMetadata meta;
  meta.trapId = DEFAULT_TRAP_ID;
  meta.capturedAt = DEFAULT_CAPTURED_AT;
  meta.gpsLat = DEFAULT_GPS_LAT;
  meta.gpsLon = DEFAULT_GPS_LON;
  meta.ldrValue = capturedLuxValue;
  meta.isFallen = capturedIsFallen ? "true" : "false";
  meta.batteryVoltage = DEFAULT_BATTERY_VOLTAGE;

  if (initModem()) {
    int httpCode = uploadPhoto(photoBuffer, photoSize, meta);
    if (httpCode == 200 || httpCode == 201) {
      Serial.println("[UPLOAD] Photo uploaded successfully!");
    } else {
      Serial.printf("[UPLOAD] Upload failed! HTTP %d\n", httpCode);
    }
  } else {
    Serial.println("[UPLOAD] Modem init failed, photo not uploaded");
  }

  free(photoBuffer);
  photoBuffer = nullptr;
  return true;
}
