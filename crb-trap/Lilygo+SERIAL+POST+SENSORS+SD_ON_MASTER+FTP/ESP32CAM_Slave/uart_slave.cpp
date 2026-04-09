/**
 * @file      uart_slave.cpp
 * @brief     UART hybrid slave-side communication implementation.
 *            ASCII command parser + SerialTransfer binary photo stream.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "uart_slave.h"
#include "uart_config.h"
#include "uart_protocol.h"
#include <Arduino.h>
#include <SerialTransfer.h>

volatile bool photoRequested = false;
volatile uint16_t cmdLux = 500;
volatile uint16_t cmdWidth = 640;
volatile uint16_t cmdHeight = 480;
volatile uint8_t cmdQuality = 10;

static SerialTransfer slaveTransfer;
static bool transferInitialized = false;

static char asciiLine[96] = {0};
static uint8_t asciiLineIdx = 0;

static bool startsWith(const char *line, const char *prefix) {
  return line && prefix && strncmp(line, prefix, strlen(prefix)) == 0;
}

static bool readAsciiLine(char *outLine, size_t outLen) {
  if (!outLine || outLen == 0) {
    return false;
  }

  while (UART_PORT.available()) {
    int c = UART_PORT.read();
    if (c < 0) {
      break;
    }

    if (c == '\n' || c == '\r') {
      if (asciiLineIdx == 0) {
        continue;
      }

      size_t n = (asciiLineIdx < outLen - 1) ? asciiLineIdx : (outLen - 1);
      memcpy(outLine, asciiLine, n);
      outLine[n] = '\0';
      asciiLineIdx = 0;
      memset(asciiLine, 0, sizeof(asciiLine));
      return true;
    }

    if (asciiLineIdx < sizeof(asciiLine) - 1) {
      asciiLine[asciiLineIdx++] = (char)c;
    } else {
      asciiLineIdx = 0;
      memset(asciiLine, 0, sizeof(asciiLine));
    }
  }

  return false;
}

void pollSerialCommands() {
  char line[96] = {0};
  if (!readAsciiLine(line, sizeof(line))) {
    return;
  }

  if (!startsWith(line, CMD_SNAP ":")) {
    return;
  }

  unsigned int l = 0;
  unsigned int w = 0;
  unsigned int h = 0;
  unsigned int q = 0;

  if (sscanf(line, "SNAP:%u,%u,%u,%u", &l, &w, &h, &q) != 4) {
    Serial.printf("[UART] Invalid SNAP syntax: %s\n", line);
    sendErrorMessage(ERR_INVALID_PARAMS);
    return;
  }

  cmdLux = (uint16_t)l;
  cmdWidth = (uint16_t)w;
  cmdHeight = (uint16_t)h;
  cmdQuality = (uint8_t)q;

  photoRequested = true;
  Serial.printf("[UART] SNAP received LUX=%u %ux%u Q=%u\n", cmdLux, cmdWidth, cmdHeight, cmdQuality);
}

bool scanAndPairWithMaster() {
  UART_PORT.begin(UART_BAUD_RATE, UART_CONFIG, UART_RX_PIN, UART_TX_PIN);
  slaveTransfer.begin(UART_PORT, false);
  transferInitialized = true;

  Serial.printf("[UART] Slave link ready: Serial2 @%d RX=GPIO%d TX=GPIO%d\n",
                UART_BAUD_RATE, UART_RX_PIN, UART_TX_PIN);
  return true;
}

bool sendReadySignal() {
  UART_PORT.print(CMD_READY);
  UART_PORT.print("\n");
  UART_PORT.flush();
  Serial.println("[UART] >> READY");
  return true;
}

bool sendErrorMessage(uint8_t errorCode) {
  char msg[32] = {0};
  snprintf(msg, sizeof(msg), "%s:%u\n", CMD_ERROR, errorCode);
  UART_PORT.write((const uint8_t *)msg, strlen(msg));
  UART_PORT.flush();
  Serial.printf("[UART] >> %s", msg);
  return true;
}

static bool waitForSendCommand(uint32_t timeoutMs) {
  char line[64] = {0};
  uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    if (readAsciiLine(line, sizeof(line))) {
      if (strcmp(line, CMD_SEND) == 0) {
        return true;
      }
      if (startsWith(line, CMD_SNAP ":")) {
        // Ignore overlapping re-tries while waiting for SEND.
        continue;
      }
    }
    delay(2);
  }

  return false;
}

static bool sendChunkWithRetry(uint16_t chunkId, const uint8_t *chunkData, size_t chunkLen) {
  if (chunkLen == 0 || chunkLen > PHOTO_CHUNK_DATA_MAX) {
    return false;
  }

  // SerialTransfer::txObj copies bytes from the passed object itself.
  // Passing a pointer would serialize pointer memory, not pointed payload.
  uint8_t payload[PHOTO_CHUNK_DATA_MAX] = {0};
  memcpy(payload, chunkData, chunkLen);

  for (uint8_t attempt = 0; attempt < MAX_SEND_RETRIES; attempt++) {
    uint16_t sendSize = 0;
    sendSize = slaveTransfer.txObj(chunkId, sendSize, sizeof(chunkId));
    sendSize = slaveTransfer.txObj(payload, sendSize, chunkLen);

    uint8_t status = slaveTransfer.sendData(sendSize, PACKET_ID_PHOTO_CHUNK);
    if (status != 0) {
      return true;
    }

    delay(20 * (attempt + 1));
  }

  return false;
}

bool sendPhotoViaESPNOW(camera_fb_t *fb) {
  if (!transferInitialized || !fb || fb->len == 0) {
    sendErrorMessage(ERR_CAPTURE_FAILED);
    return false;
  }

  char sizeMsg[48] = {0};
  snprintf(sizeMsg, sizeof(sizeMsg), "%s:%lu\n", CMD_SIZE, (unsigned long)fb->len);
  UART_PORT.write((const uint8_t *)sizeMsg, strlen(sizeMsg));
  UART_PORT.flush();
  Serial.printf("[UART] >> %s", sizeMsg);

  if (!waitForSendCommand(ASCII_RESPONSE_TIMEOUT_MS)) {
    Serial.println("[UART] SEND timeout");
    sendErrorMessage(ERR_CMD_TIMEOUT);
    return false;
  }

  uint32_t bytesSent = 0;
  uint16_t chunkId = 0;

  while (bytesSent < fb->len) {
    size_t remaining = fb->len - bytesSent;
    size_t chunkLen = (remaining > PHOTO_CHUNK_DATA_MAX) ? PHOTO_CHUNK_DATA_MAX : remaining;

    if (!sendChunkWithRetry(chunkId, fb->buf + bytesSent, chunkLen)) {
      Serial.printf("[UART] Failed chunk %u\n", chunkId);
      sendErrorMessage(ERR_SEND_FAILED);
      return false;
    }

    bytesSent += chunkLen;
    chunkId++;

    if ((chunkId % 20) == 0 || bytesSent == fb->len) {
      Serial.printf("[UART] Progress: %lu/%lu bytes\n", (unsigned long)bytesSent, (unsigned long)fb->len);
    }

    delay(1);
  }

  uint32_t crc = calculateCRC32(fb->buf, fb->len);
  char doneMsg[32] = {0};
  snprintf(doneMsg, sizeof(doneMsg), "%s:%08lX\n", CMD_DONE, (unsigned long)crc);
  UART_PORT.write((const uint8_t *)doneMsg, strlen(doneMsg));
  UART_PORT.flush();
  Serial.printf("[UART] >> %s", doneMsg);
  return true;
}

bool espnowSendReliable(const uint8_t *data, size_t len) {
  if (!data || len < 2) {
    return false;
  }

  if (data[0] == 0xF0) {
    return sendErrorMessage(data[1]);
  }

  return false;
}
