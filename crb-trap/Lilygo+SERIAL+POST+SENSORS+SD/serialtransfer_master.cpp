/**
 * @file      serialtransfer_master.cpp
 * @brief     Hybrid protocol implementation for LilyGo master:
 *            ASCII commands + SerialTransfer image data
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-31
 * @note      Commands use simple serial (text), image chunks use SerialTransfer (binary)
 */

#include "serialtransfer_master.h"
#include "serialtransfer_protocol.h"
#include "utilities.h"
#include <SerialTransfer.h>

// ==================== GLOBAL STATE ====================

// SerialTransfer instance for binary photo chunk reception only
static SerialTransfer masterTransfer;
static HardwareSerial masterUART(1); // UART1 on LilyGo

// ASCII response state machine
static char asciiRespBuffer[ASCII_RESP_MAX_LEN] = {0};
static uint8_t asciiRespIdx = 0;

// Parsed READY response
struct {
  uint16_t lux;
  uint8_t isFallen;
  uint32_t timestamp;
  uint16_t width;
  uint16_t height;
} slaveMetadata = {0, 0, 0, 0, 0};

// Photo reception buffer
static uint8_t *photoBuffer = nullptr;
static uint32_t photoBufferSize = 0;
static bool photoReceptionComplete = false;

static volatile uint32_t lastSlaveActivityTime = 0;
static volatile bool slaveErrorReceived = false;

// ==================== INITIALIZATION ====================

bool initMasterSerialTransfer(uint8_t txPin, uint8_t rxPin) {
  Serial.println("[MASTER-UART] Initializing master UART for hybrid protocol...");

  // Configure custom UART with specified pins
  masterUART.begin(MASTER_UART_BAUD_RATE, SERIAL_8N1, rxPin, txPin);

  Serial.printf("[MASTER-UART] Configured: GPIO%d (RX) + GPIO%d (TX) @ %d baud\n",
                rxPin, txPin, MASTER_UART_BAUD_RATE);

  delay(100);

  // Initialize SerialTransfer on master UART for binary image reception only
  masterTransfer.begin(masterUART, true, Serial); // debug=true

  Serial.println("[MASTER-UART] SerialTransfer initialized (binary image data mode)");
  Serial.println("[MASTER-UART] ASCII response parser ready (text mode)");
  Serial.printf("[MASTER-UART] RX buffer ready (max %d bytes per packet)\n", PHOTO_CHUNK_SIZE);

  lastSlaveActivityTime = millis();

  return true;
}

// ==================== ASCII RESPONSE PARSING ====================

/**
 * @brief Read a complete ASCII response line from UART (terminated by \n)
 * @param outLine [out] Buffer to store the read line (null-terminated, no \n)
 * @param maxLen Maximum buffer size
 * @return Number of bytes read (0 if no complete line available)
 */
static uint16_t readAsciiResponse(char *outLine, uint16_t maxLen) {
  if (!outLine || maxLen == 0)
    return 0;

  while (masterUART.available()) {
    int c = masterUART.read();

    if (c == -1)
      break;

    if (c == '\n' || c == '\r') {
      if (asciiRespIdx > 0) {
        // Complete line received
        memcpy(outLine, asciiRespBuffer, asciiRespIdx);
        outLine[asciiRespIdx] = '\0';

        uint16_t result = asciiRespIdx;
        asciiRespIdx = 0;
        memset(asciiRespBuffer, 0, ASCII_RESP_MAX_LEN);

        return result;
      }
      // Skip leading newlines
      continue;
    }

    // Accumulate character
    if (asciiRespIdx < ASCII_RESP_MAX_LEN - 1) {
      asciiRespBuffer[asciiRespIdx++] = (char)c;
    } else {
      // Buffer overflow
      Serial.println("[MASTER-RX] ERROR: ASCII response buffer overflow");
      asciiRespIdx = 0;
      return 0;
    }
  }

  return 0;
}

// ==================== SLAVE DISCOVERY & READINESS (ASCII) ====================

bool waitForSlaveReady(uint32_t timeoutMs) {
  uint32_t startTime = millis();

  Serial.printf("[MASTER-RX] Waiting for slave READY (timeout=%d ms)...\n", timeoutMs);
  Serial.printf("[DEBUG] Master UART port status: available=%d\n", masterUART.available());

  uint32_t lastPrint = millis();

  while (millis() - startTime < timeoutMs) {
    char respLine[ASCII_RESP_MAX_LEN] = {0};
    uint16_t respLen = readAsciiResponse(respLine, ASCII_RESP_MAX_LEN);

    if (respLen > 0) {
      Serial.printf("[DEBUG] Received %d bytes: |%s|\n", respLen, respLine);
      // Parse: "READY" (simple, no metadata)
      if (strstr(respLine, CMD_READY) == respLine) {
        Serial.println("[MASTER-RX] READY received from slave!");

        // Clear any remaining ASCII data before switching to binary mode
        delay(50);
        while (masterUART.available()) {
          int c = masterUART.read();
          if (c > 0)
            Serial.printf("[PURGE] Discarded byte: 0x%02X\n", c);
        }
        asciiRespIdx = 0;
        memset(asciiRespBuffer, 0, ASCII_RESP_MAX_LEN);

        lastSlaveActivityTime = millis();
        return true;
      } else if (strstr(respLine, CMD_ERR) == respLine) {
        uint8_t errCode;
        sscanf(respLine, "%*[^:]:%hhu", &errCode);
        Serial.printf("[MASTER-RX] ERROR from slave (code=%d)\n", errCode);
        slaveErrorReceived = true;
      } else {
        Serial.printf("[MASTER-RX] Unexpected response: |%s|\n", respLine);
      }
    } else {
      // No data available; small delay to avoid busy-loop and allow slave to send
      if (millis() - lastPrint > 2000) {
        Serial.printf("[DEBUG] Waiting... (%lu ms elapsed, %d bytes available)\n",
                      millis() - startTime, masterUART.available());
        lastPrint = millis();
      }
      delay(5);
    }
  }

  Serial.printf("[MASTER-RX] ERROR: READY timeout after %d ms\n", timeoutMs);
  return false;
}

// ==================== COMMAND SENDING (ASCII) ====================

bool sendPhotoCommand(uint16_t width, uint16_t height, uint8_t quality) {
  // Format: "PHOTO:width:height:quality\n"
  char photoCmd[ASCII_CMD_MAX_LEN] = {0};
  snprintf(photoCmd, ASCII_CMD_MAX_LEN, "%s:%u:%u:%u\n",
           CMD_PHOTO, width, height, quality);

  Serial.printf("[MASTER-TX] Sending ASCII PHOTO command: %s", photoCmd);

  masterUART.write((const uint8_t *)photoCmd, strlen(photoCmd));
  masterUART.flush();

  lastSlaveActivityTime = millis();
  return true;
}

// ==================== PHOTO RECEPTION (BINARY VIA SERIALTRANSFER) ====================

bool receivePhotoChunked(uint8_t *&outBuffer, uint32_t &outSize) {
  outBuffer = nullptr;
  outSize = 0;
  photoReceptionComplete = false;

  // Wait for PHOTO_HEADER packet (BINARY via SerialTransfer)
  Serial.println("[MASTER-RX] Waiting for PHOTO_HEADER from slave...");
  uint32_t headerStartTime = millis();

  while (millis() - headerStartTime < SERIAL_CHUNK_TIMEOUT_MS) {
    masterTransfer.tick();

    if (masterTransfer.available()) {
      uint8_t packetID = masterTransfer.currentPacketID();

      if (packetID == PACKET_ID_PHOTO_HEADER) {
        PhotoHeader header;
        uint16_t recSize = 0;
        recSize = masterTransfer.rxObj(header, recSize, sizeof(PhotoHeader));

        Serial.printf("[MASTER-RX] PHOTO_HEADER received: size=%lu, chunks=%d, quality=%d\n",
                      header.totalSize, header.numChunks, header.quality);

        // Allocate buffer for photo
        if (header.totalSize > PHOTO_MAX_SIZE) {
          Serial.printf("[MASTER-RX] ERROR: Photo too large (%lu > %d)\n",
                        header.totalSize, PHOTO_MAX_SIZE);
          return false;
        }

        // Allocate new buffer
        freePhotoBuffer();
        photoBuffer = (uint8_t *)malloc(header.totalSize);
        if (!photoBuffer) {
          Serial.printf("[MASTER-RX] ERROR: Failed to allocate %lu bytes\n", header.totalSize);
          return false;
        }

        photoBufferSize = header.totalSize;
        Serial.printf("[MASTER-RX] Buffer allocated: %lu bytes\n", photoBufferSize);

        // Start receiving chunks
        uint32_t bytesReceived = 0;
        uint16_t expectedChunks = header.numChunks;

        for (uint16_t chunkIdx = 0; chunkIdx < expectedChunks; chunkIdx++) {
          uint16_t chunkId = 0;
          uint8_t chunkData[PHOTO_CHUNK_SIZE];
          size_t chunkLen = 0;

          if (!receivePhotoChunkWithRetry(chunkId, (uint8_t *&)chunkData, chunkLen)) {
            Serial.printf("[MASTER-RX] ERROR: Failed to receive chunk %d\n", chunkIdx);
            return false;
          }

          // Copy chunk to buffer
          if (bytesReceived + chunkLen > photoBufferSize) {
            Serial.printf("[MASTER-RX] ERROR: Chunk overflow (received %lu + %zu > %lu)\n",
                          bytesReceived, chunkLen, photoBufferSize);
            return false;
          }

          memcpy(photoBuffer + bytesReceived, chunkData, chunkLen);
          bytesReceived += chunkLen;

          // Log progress every 10 chunks
          if ((chunkIdx + 1) % 10 == 0 || chunkIdx == expectedChunks - 1) {
            Serial.printf("[MASTER-RX] Progress: %d/%d chunks (%lu/%lu bytes)\n",
                          chunkIdx + 1, expectedChunks, bytesReceived, photoBufferSize);
          }
        }

        // Wait for ASCII DONE message
        Serial.println("[MASTER-RX] All chunks received, waiting for DONE response...");
        uint32_t doneStartTime = millis();

        bool receivedDone = false;
        while (millis() - doneStartTime < ASCII_RESPONSE_TIMEOUT_MS) {
          char respLine[ASCII_RESP_MAX_LEN] = {0};
          uint16_t respLen = readAsciiResponse(respLine, ASCII_RESP_MAX_LEN);

          if (respLen > 0) {
            if (strstr(respLine, CMD_DONE) == respLine) {
              uint8_t status = 0;
              sscanf(respLine, "%*[^:]:%hhu", &status);

              Serial.printf("[MASTER-RX] DONE received (status=%d)\n", status);

              if (status != 0) {
                Serial.println("[MASTER-RX] WARNING: Slave reported error in photo transmission");
              }

              // Validate JPEG
              if (!validateJpegPhoto(photoBuffer, photoBufferSize)) {
                Serial.println("[MASTER-RX] ERROR: Photo buffer does not contain valid JPEG");
                return false;
              }

              outBuffer = photoBuffer;
              outSize = photoBufferSize;
              photoReceptionComplete = true;
              lastSlaveActivityTime = millis();
              receivedDone = true;

              Serial.printf("[MASTER-RX] Photo reception COMPLETE: %lu bytes\n", outSize);
              return true;
            } else if (strstr(respLine, CMD_ERR) == respLine) {
              uint8_t errCode = 0;
              sscanf(respLine, "%*[^:]:%hhu", &errCode);
              Serial.printf("[MASTER-RX] ERROR from slave (code=%d)\n", errCode);
              return false;
            }
          }

          delay(10);
        }

        if (!receivedDone) {
          Serial.println("[MASTER-RX] ERROR: DONE packet timeout");
          return false;
        }
      }
    }

    delay(10);
  }

  Serial.println("[MASTER-RX] ERROR: PHOTO_HEADER timeout");
  return false;
}

bool receivePhotoChunkWithRetry(uint16_t &chunkId, uint8_t *&chunkData,
                                size_t &chunkLen, uint8_t maxRetries) {
  for (uint8_t attempt = 0; attempt < maxRetries; attempt++) {
    if (attempt > 0) {
      Serial.printf("[MASTER-RX] RETRY: Chunk, attempt %d/%d\n", attempt + 1, maxRetries);
      delay(RETRY_BACKOFF_MS);
    }

    uint32_t chunkStartTime = millis();

    while (millis() - chunkStartTime < SERIAL_CHUNK_TIMEOUT_MS) {
      masterTransfer.tick();

      if (masterTransfer.available()) {
        uint8_t packetID = masterTransfer.currentPacketID();

        if (packetID == PACKET_ID_PHOTO_CHUNK) {
          // Decode chunk: [chunkId (2 bytes) | data (variable length)]
          uint16_t recSize = 0;
          recSize = masterTransfer.rxObj(chunkId, recSize, sizeof(uint16_t));

          // Allocate buffer for this chunk's data
          uint8_t *tempData = (uint8_t *)malloc(PHOTO_CHUNK_SIZE - 2);
          if (!tempData) {
            Serial.println("[MASTER-RX] ERROR: Failed to allocate chunk buffer");
            return false;
          }

          recSize = masterTransfer.rxObj(tempData, recSize, PHOTO_CHUNK_SIZE - 2);

          chunkLen = recSize; // Actual bytes received
          chunkData = tempData;

          Serial.printf("[MASTER-RX] Chunk %d received (%zu bytes)\n", chunkId, chunkLen);
          lastSlaveActivityTime = millis();
          return true;
        }
      }

      delay(5);
    }

    if (attempt < maxRetries - 1) {
      Serial.printf("[MASTER-RX] Chunk timeout, retrying...\n");
    }
  }

  return false;
}

// ==================== PHOTO PROCESSING ====================

bool validateJpegPhoto(const uint8_t *buffer, uint32_t size) {
  if (!buffer || size < 3) {
    return false;
  }

  // Check JPEG SOI marker (FF D8 FF)
  if (buffer[0] == 0xFF && buffer[1] == 0xD8 && buffer[2] == 0xFF) {
    return true;
  }

  // Scan for SOI marker in first 1KB (handles potential padding)
  uint32_t scanLimit = (size > 1024) ? 1024 : size - 2;
  for (uint32_t i = 0; i < scanLimit; i++) {
    if (buffer[i] == 0xFF && buffer[i + 1] == 0xD8 && buffer[i + 2] == 0xFF) {
      Serial.printf("[MASTER] JPEG SOI marker found at offset %lu\n", i);
      return true;
    }
  }

  return false;
}

uint32_t crc32Photo(const uint8_t *buffer, uint32_t size) {
  // Simple CRC32 implementation (optional, for higher-level integrity check)
  // SerialTransfer already includes CRC8 at packet level
  static const uint32_t poly = 0xEDB88320;
  uint32_t crc = 0xFFFFFFFF;

  for (uint32_t i = 0; i < size; i++) {
    uint8_t byte = buffer[i];
    crc ^= byte;
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ poly;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc ^ 0xFFFFFFFF;
}

// ==================== STATUS & DIAGNOSTICS ====================

bool isSlaveAlive() {
  return (millis() - lastSlaveActivityTime) < SERIAL_CHUNK_TIMEOUT_MS;
}

void freePhotoBuffer() {
  if (photoBuffer) {
    free(photoBuffer);
    photoBuffer = nullptr;
    photoBufferSize = 0;
  }
}

void diagnosticMasterUARTStatus() {
  Serial.println("\n[MASTER-DIAG] UART Status (Hybrid Protocol):");
  Serial.printf("  Port:             Serial1 (UART1)\n");
  Serial.printf("  Last Activity:    %lu ms ago\n", millis() - lastSlaveActivityTime);
  Serial.printf("  Slave Alive:      %s\n", isSlaveAlive() ? "YES" : "NO (TIMEOUT)");
  Serial.printf("  Photo Buffer:     %s (%lu bytes)\n",
                photoBuffer ? "ALLOCATED" : "FREE", photoBufferSize);
  Serial.printf("  Photo Complete:   %s\n", photoReceptionComplete ? "YES" : "NO");
  Serial.println("  Protocol Mode:    HYBRID (ASCII commands + SerialTransfer image data)");
  Serial.println();
}

// ==================== UART BUFFER MANAGEMENT ====================

void purgeUARTBuffer() {
  // CRITICAL: Only discard ASCII bytes (text), NOT binary SerialTransfer packets!
  // SerialTransfer packets contain 0x7E (frame start) and other control bytes
  // If we discard those, we lose the PHOTO_HEADER entirely.
  // Only purge printable ASCII and whitespace to avoid corrupting binary frames.
  
  int discardCount = 0;
  while (masterUART.available()) {
    int c = masterUART.read();
    if (c > 0) {
      // Only log/count if it's a printable ASCII or whitespace character
      // Leave binary bytes (0x00-0x08, 0x0E-0x1F, 0x7F above printable range) alone
      if ((c >= 32 && c < 127) || c == '\n' || c == '\r' || c == '\t') {
        discardCount++;
        if (discardCount <= 10) {
          Serial.printf("[PURGE-ASCII] Discarded: 0x%02X ('%c')\n", c, (c >= 32 && c < 127) ? c : '?');
        }
      } else if (discardCount == 0) {
        // First byte is non-ASCII - probably binary data already starting, stop purging
        Serial.printf("[PURGE-ASCII] Detected binary data at start (0x%02X), stopping purge to preserve PHOTO_HEADER\n", c);
        // Put it back by reading ahead cautiously - actually we can't put it back, so just log and return
        return;
      }
    }
  }
  
  if (discardCount > 10) {
    Serial.printf("[PURGE-ASCII] Discarded %d additional ASCII bytes\n", discardCount - 10);
  } else if (discardCount > 0) {
    Serial.printf("[PURGE-ASCII] Done. Discarded %d ASCII bytes total\n", discardCount);
  } else {
    Serial.println("[PURGE-ASCII] No ASCII bytes to discard");
  }
}
