/**
 * @file      serialtransfer_slave.cpp
 * @brief     Hybrid protocol implementation for ESP32-CAM slave:
 *            ASCII commands + SerialTransfer image data
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-31
 * @note      Commands use simple serial (text), image chunks use SerialTransfer (binary)
 */

#include "serialtransfer_slave.h"
#include "esp_task_wdt.h"
#include "serialtransfer_protocol.h"
#include "uart_config.h"
#include <SerialTransfer.h>

// ==================== GLOBAL STATE ====================

// SerialTransfer instance for binary photo chunk transmission only
static SerialTransfer binaryTransfer;

// Photo transmission state flag (prevents periodic READY from corrupting binary stream)
static bool inPhotoTransmission = false;

// ASCII command state machine
static char asciiLineBuffer[ASCII_CMD_MAX_LEN] = {0};
static uint8_t asciiLineIdx = 0;
static volatile uint32_t lastCommandTime = 0;

// Parsed command state
struct {
  bool hasNewCommand;
  char cmdName[16]; // e.g., "PHOTO", "STAT", "PING"
  uint16_t width;   // PHOTO command parameter
  uint16_t height;  // PHOTO command parameter
  uint8_t quality;  // PHOTO command parameter
} currentCommand = {false, "", 0, 0, 0};

// ==================== INITIALIZATION ====================

bool initSerialTransfer() {
  Serial.println("[UART] Initializing Serial2 (UART1) for hybrid protocol...");

  // Configure Serial2 with custom UART pins
  UART_PORT.begin(UART_BAUD_RATE, UART_CONFIG, UART_RX_PIN, UART_TX_PIN);

  Serial.printf("[UART] Configured: GPIO%d (TX) + GPIO%d (RX) @ %d baud\n",
                UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);

  delay(100);

  // Initialize SerialTransfer on Serial2 for binary image data only
  binaryTransfer.begin(UART_PORT, true, Serial); // debug=true, debugPort=Serial

  Serial.println("[UART] SerialTransfer initialized (binary image data mode)");
  Serial.println("[UART] ASCII command parser ready (text mode)");
  Serial.printf("[UART] RX buffer ready (max %d bytes per packet)\n", PHOTO_CHUNK_SIZE);

  lastCommandTime = millis();
  return true;
}

// ==================== ASCII COMMAND PARSING ====================
/**
 * @brief Read a complete ASCII line from UART (terminated by \n)
 * @param outLine [out] Buffer to store the read line (including \n)
 * @param maxLen Maximum buffer size
 * @return Number of bytes read (0 if no complete line available)
 */
static uint16_t readAsciiLine(char *outLine, uint16_t maxLen) {
  if (!outLine || maxLen == 0)
    return 0;

  while (UART_PORT.available()) {
    int c = UART_PORT.read();

    if (c == -1)
      break; // No more data

    if (c == '\n' || c == '\r') {
      if (asciiLineIdx > 0) {
        // Complete line received
        memcpy(outLine, asciiLineBuffer, asciiLineIdx);
        outLine[asciiLineIdx] = '\0';

        uint16_t result = asciiLineIdx;
        asciiLineIdx = 0;
        memset(asciiLineBuffer, 0, ASCII_CMD_MAX_LEN);

        return result;
      }
      // Skip leading newlines
      continue;
    }

    // Accumulate character
    if (asciiLineIdx < ASCII_CMD_MAX_LEN - 1) {
      asciiLineBuffer[asciiLineIdx++] = (char)c;
    } else {
      // Buffer overflow
      Serial.println("[CMD] ERROR: ASCII command buffer overflow");
      asciiLineIdx = 0;
      return 0;
    }
  }

  return 0; // No complete line yet
}

/**
 * @brief Parse an ASCII command line into command structure
 * @param line Input ASCII line (null-terminated)
 * @return true if parse successful, false otherwise
 */
static bool parseCommand(const char *line) {
  if (!line || strlen(line) == 0)
    return false;

  // Extract command name (uppercase)
  char cmdName[32] = {0};
  int n = sscanf(line, "%31[^:]", cmdName);
  if (n < 1) {
    Serial.printf("[CMD] Parse error: invalid command format '%s'\n", line);
    return false;
  }

  currentCommand.hasNewCommand = true;
  strncpy(currentCommand.cmdName, cmdName, sizeof(currentCommand.cmdName) - 1);

  Serial.printf("[CMD] Parsed command: %s\n", currentCommand.cmdName);

  // Parse command-specific parameters
  if (strcmp(cmdName, CMD_PHOTO) == 0) {
    // Format: "PHOTO:width:height:quality\n"
    uint16_t w, h;
    uint8_t q;
    n = sscanf(line, "%*[^:]:%hu:%hu:%hhu", &w, &h, &q);

    if (n != 3) {
      Serial.printf("[CMD] PHOTO parse error: expected 3 params, got %d\n", n);
      return false;
    }

    // Validate parameters
    if (w < PHOTO_WIDTH_MIN || w > PHOTO_WIDTH_MAX ||
        h < PHOTO_HEIGHT_MIN || h > PHOTO_HEIGHT_MAX ||
        q < PHOTO_QUALITY_MIN || q > PHOTO_QUALITY_MAX) {
      Serial.printf("[CMD] PHOTO validation: invalid dims %dx%d or quality %d\n", w, h, q);
      return false;
    }

    currentCommand.width = w;
    currentCommand.height = h;
    currentCommand.quality = q;
    Serial.printf("[CMD] PHOTO: %dx%d quality=%d\n", w, h, q);

  } else if (strcmp(cmdName, CMD_STAT) == 0 ||
             strcmp(cmdName, CMD_PING) == 0 ||
             strcmp(cmdName, CMD_RESET) == 0) {
    // These commands have no parameters
    Serial.printf("[CMD] %s received\n", cmdName);
  } else {
    Serial.printf("[CMD] Unknown command: %s\n", cmdName);
    return false;
  }

  return true;
}

// ==================== READY SIGNAL (ASCII) ====================

bool sendReadyPacket(uint16_t lux, uint8_t isFallen, uint16_t photoWidth,
                     uint16_t photoHeight, uint32_t timestamp) {
  // Send ASCII READY response: simple "READY\n" (no metadata)
  char readyMsg[ASCII_RESP_MAX_LEN] = {0};

  snprintf(readyMsg, ASCII_RESP_MAX_LEN, "%s\n", CMD_READY);

  Serial.printf("[TX] Sending ASCII READY: %s", readyMsg);

  // Send via simple Serial write (ASCII, NOT SerialTransfer)
  UART_PORT.write((const uint8_t *)readyMsg, strlen(readyMsg));
  UART_PORT.flush();

  lastCommandTime = millis();
  return true;
}

/**
 * @brief Check if command has been received and is ready to process
 * @return true if new command available (call getCommand to retrieve)
 */
bool commandAvailable() {
  // Try to read a complete ASCII line
  char line[ASCII_CMD_MAX_LEN] = {0};
  uint16_t lineLen = readAsciiLine(line, ASCII_CMD_MAX_LEN);

  if (lineLen > 0) {
    // Parse the received command
    if (parseCommand(line)) {
      lastCommandTime = millis();
      return true;
    } else {
      // Send error response for invalid command
      sendErrorMessage(ERR_INVALID_PARAMS);
      return false;
    }
  }

  return false;
}

/**
 * @brief Retrieve the last parsed command
 * @param outCmd [out] Command structure to fill
 * @return true if valid command available
 */
bool getCommand(PhotoCommand &outCmd) {
  if (!currentCommand.hasNewCommand) {
    return false;
  }

  strncpy(outCmd.cmdName, currentCommand.cmdName, sizeof(outCmd.cmdName) - 1);
  outCmd.width = currentCommand.width;
  outCmd.height = currentCommand.height;
  outCmd.quality = currentCommand.quality;

  currentCommand.hasNewCommand = false;
  return true;
}

/**
 * @brief Send ASCII error message
 * Format: "ERR:code\n"
 */
bool sendErrorMessage(uint8_t errorCode) {
  char errMsg[ASCII_RESP_MAX_LEN] = {0};
  snprintf(errMsg, ASCII_RESP_MAX_LEN, "%s:%u\n", CMD_ERR, errorCode);

  Serial.printf("[TX] Sending ASCII ERROR: %s", errMsg);

  UART_PORT.write((const uint8_t *)errMsg, strlen(errMsg));
  UART_PORT.flush();

  lastCommandTime = millis();
  return true;
}

/**
 * @brief Send ASCII acknowledgment
 * Format: "ACK:0\n" (success) or "ACK:1\n" (failure)
 */
bool sendAckMessage(uint8_t status) {
  char ackMsg[ASCII_RESP_MAX_LEN] = {0};
  snprintf(ackMsg, ASCII_RESP_MAX_LEN, "ACK:%u\n", status);

  Serial.printf("[TX] Sending ASCII ACK: %s", ackMsg);

  UART_PORT.write((const uint8_t *)ackMsg, strlen(ackMsg));
  UART_PORT.flush();

  lastCommandTime = millis();
  return true;
}

// ==================== PHOTO TRANSMISSION (BINARY VIA SERIALTRANSFER) ====================

bool sendPhotoChunked(const uint8_t *photoBuffer, uint32_t photoSize, uint8_t quality) {
  if (!photoBuffer || photoSize == 0) {
    Serial.println("[TX] ERROR: Invalid photo buffer");
    return sendErrorMessage(ERR_INVALID_PARAMS);
  }

  if (photoSize > PHOTO_MAX_SIZE) {
    Serial.printf("[TX] ERROR: Photo too large (%lu > %d bytes)\n", photoSize, PHOTO_MAX_SIZE);
    return sendErrorMessage(ERR_BUFFER_OVERFLOW);
  }

  // Set flag to prevent periodic READY from corrupting binary photo stream
  inPhotoTransmission = true;

  Serial.printf("[TX] Starting photo transmission: %lu bytes, quality=%d\n", photoSize, quality);

  // Calculate chunk parameters
  uint16_t numChunks = (photoSize + PHOTO_CHUNK_SIZE - 3 - 1) / (PHOTO_CHUNK_SIZE - 2);

  // Send PHOTO_HEADER packet (BINARY via SerialTransfer)
  PhotoHeader header;
  header.totalSize = photoSize;
  header.numChunks = numChunks;
  header.quality = quality;
  header.reserved = 0;

  Serial.printf("[TX] Sending PHOTO_HEADER via SerialTransfer: size=%lu bytes, chunks=%d\n",
                header.totalSize, header.numChunks);

  uint16_t sendSize = 0;
  sendSize = binaryTransfer.txObj(header, sendSize, sizeof(PhotoHeader));
  uint8_t status = binaryTransfer.sendData(sendSize, PACKET_ID_PHOTO_HEADER);

  if (status == 0) {
    Serial.printf("[TX] ERROR: Failed to send PHOTO_HEADER (status=%d)\n", status);
    return sendErrorMessage(ERR_GENERIC);
  }

  delay(100); // Brief pause before streaming chunks

  // Stream photo chunks (BINARY via SerialTransfer)
  uint32_t bytesSent = 0;

  for (uint16_t chunkId = 0; chunkId < numChunks; chunkId++) {
    uint32_t bytesRemaining = photoSize - bytesSent;
    size_t chunkDataLen = (bytesRemaining > (PHOTO_CHUNK_SIZE - 2))
                              ? (PHOTO_CHUNK_SIZE - 2)
                              : bytesRemaining;

    const uint8_t *chunkData = photoBuffer + bytesSent;

    if (!sendPhotoChunkWithRetry(chunkId, chunkData, chunkDataLen)) {
      Serial.printf("[TX] ERROR: Failed to send chunk %d after retries\n", chunkId);
      return sendErrorMessage(ERR_GENERIC);
    }

    bytesSent += chunkDataLen;

    // Log progress and feed watchdog every 10 chunks
    if ((chunkId + 1) % 10 == 0) {
      Serial.printf("[TX] Progress: %d/%d chunks sent (%lu/%lu bytes)\n",
                    chunkId + 1, numChunks, bytesSent, photoSize);
      esp_task_wdt_reset(); // Feed watchdog during long transmission
    }
  }

  Serial.printf("[TX] All %d chunks sent (%lu bytes)\n", numChunks, bytesSent);

  // Send ASCII DONE response: "DONE:0\n" (status=0 for success)
  char doneMsg[ASCII_RESP_MAX_LEN] = {0};
  snprintf(doneMsg, ASCII_RESP_MAX_LEN, "%s:%u\n", CMD_DONE, 0);

  Serial.printf("[TX] Sending ASCII DONE: %s", doneMsg);
  UART_PORT.write((const uint8_t *)doneMsg, strlen(doneMsg));
  UART_PORT.flush();

  lastCommandTime = millis();

  // Clear flag to resume periodic READY broadcasts
  inPhotoTransmission = false;

  return true;
}

bool sendPhotoChunkWithRetry(uint16_t chunkId, const uint8_t *chunkData,
                             size_t chunkLen, uint8_t maxRetries) {
  if (chunkLen > (PHOTO_CHUNK_SIZE - 2)) {
    Serial.printf("[TX] ERROR: Chunk too large (%zu > %d)\n", chunkLen, PHOTO_CHUNK_SIZE - 2);
    return false;
  }

  for (uint8_t attempt = 0; attempt < maxRetries; attempt++) {
    if (attempt > 0) {
      Serial.printf("[TX] RETRY: Chunk %d, attempt %d/%d\n", chunkId, attempt + 1, maxRetries);
      delay(RETRY_BACKOFF_MS);
    }

    uint16_t sendSize = 0;

    // Pack chunkId + data into PhotoChunk structure
    sendSize = binaryTransfer.txObj(chunkId, sendSize, sizeof(uint16_t));
    sendSize = binaryTransfer.txObj(chunkData, sendSize, chunkLen);

    uint8_t status = binaryTransfer.sendData(sendSize, PACKET_ID_PHOTO_CHUNK);

    if (status == 0) {
      Serial.printf("[TX] ERROR: Send failed for chunk %d (status=%d)\n", chunkId, status);
      continue;
    }

    // Wait briefly to allow master to process
    delay(50);
    binaryTransfer.tick();

    lastCommandTime = millis();
    return true;
  }

  return false;
}

/**
 * @brief Check if master is still connected (based on recent activity)
 */
bool isMasterAlive() {
  return (millis() - lastCommandTime) < SERIAL_CHUNK_TIMEOUT_MS;
}

// ==================== DIAGNOSTICS ====================

void diagnosticUARTStatus() {
  Serial.println("\n[DIAG] UART/SerialTransfer Status:");
  Serial.printf("  Port:             Serial2 (UART1)\n");
  Serial.printf("  TX Pin:           GPIO%d\n", UART_TX_PIN);
  Serial.printf("  RX Pin:           GPIO%d\n", UART_RX_PIN);
  Serial.printf("  Baud Rate:        %d\n", UART_BAUD_RATE);
  Serial.printf("  Last Command:     %lu ms ago\n", millis() - lastCommandTime);
  Serial.printf("  Master Alive:     %s\n", isMasterAlive() ? "YES" : "NO (TIMEOUT)");
  Serial.println("  Protocol Mode:    HYBRID (ASCII commands + SerialTransfer image data)");
  Serial.printf("  ASCII Buffer:     %d/%d bytes used\n", asciiLineIdx, ASCII_CMD_MAX_LEN);
  Serial.println();
}

// ==================== WRAPPER FUNCTIONS FOR CAMERA ====================

/**
 * @brief Wrapper to send photo via SerialTransfer (called from camera.cpp)
 * @param buffer Photo JPEG data
 * @param size Photo size in bytes
 * @param quality JPEG quality (1-63)
 * @return true if sent successfully
 */
bool sendPhotoViaSerialTransfer(const uint8_t *buffer, uint32_t size, uint8_t quality) {
  Serial.println("[SERIALTRANSFER] sendPhotoViaSerialTransfer called");

  // Use the existing sendPhotoChunked function
  return sendPhotoChunked(buffer, size, quality);
}
