/**
 * @file      serialtransfer_slave.cpp
 * @brief     SerialTransfer protocol implementation for ESP32-CAM slave
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-21
 */

#include "serialtransfer_slave.h"
#include "serialtransfer_protocol.h"
#include "uart_config.h"
#include <SerialTransfer.h>

// ==================== GLOBAL STATE ====================

static SerialTransfer myTransfer;
static volatile bool masterAckReceived = false;
static volatile bool masterCommandReceived = false;
static volatile uint32_t lastPacketTime = 0;

// ==================== INITIALIZATION ====================

bool initSerialTransfer() {
  Serial.println("[UART] Initializing Serial2 (UART1)...");

  // Configure Serial2 with custom UART pins
  UART_PORT.begin(UART_BAUD_RATE, UART_CONFIG, UART_RX_PIN, UART_TX_PIN);

  Serial.printf("[UART] Configured: GPIO%d (TX) + GPIO%d (RX) @ %d baud\n",
                UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);

  delay(100);

  // Initialize SerialTransfer on Serial2
  myTransfer.begin(UART_PORT, true, Serial); // debug=true, debugPort=Serial

  Serial.println("[UART] SerialTransfer initialized");
  Serial.printf("[UART] RX buffer ready (max %d bytes per packet)\n", PHOTO_CHUNK_SIZE);

  return true;
}

// ==================== READY SIGNAL ====================

bool sendReadyPacket(const PhotoMetadata &metadata) {
  Serial.println("[TX] Sending READY packet with metadata...");

  uint16_t sendSize = 0;

  // Pack metadata into transfer buffer
  sendSize = myTransfer.txObj(metadata, sendSize, sizeof(PhotoMetadata));

  // Send as PACKET_ID_READY
  uint8_t status = myTransfer.sendData(sendSize, PACKET_ID_READY);

  if (status == 0) {
    Serial.printf("[TX] ERROR: Failed to send READY packet (status=%d)\n", status);
    return false;
  }

  Serial.printf("[TX] READY packet sent (size=%d, LUX=%d, fallen=%d)\n",
                sendSize, metadata.luxValue, metadata.isFallen);

  lastPacketTime = millis();
  return true;
}

bool waitForMasterAck(uint32_t timeoutMs) {
  uint32_t startTime = millis();

  Serial.printf("[RX] Waiting for master ACK (timeout=%d ms)...\n", timeoutMs);

  while (millis() - startTime < timeoutMs) {
    myTransfer.tick(); // Update SerialTransfer state machine

    if (myTransfer.available()) {
      uint8_t packetID = myTransfer.currentPacketID();

      if (packetID == PACKET_ID_ACK) {
        AckPacket ack = {0};
        uint16_t recSize = 0;
        recSize = myTransfer.rxObj(ack, recSize, sizeof(AckPacket));

        Serial.printf("[RX] ACK received (command=%d)\n", ack.command);
        lastPacketTime = millis();
        masterAckReceived = true;
        return true;
      } else {
        Serial.printf("[RX] Unexpected packet ID (0x%02X), waiting for ACK...\n", packetID);
      }
    }

    delay(10);
  }

  Serial.printf("[RX] ERROR: ACK timeout after %d ms\n", timeoutMs);
  return false;
}

// ==================== PHOTO TRANSMISSION ====================

bool sendPhotoChunked(const uint8_t *photoBuffer, uint32_t photoSize, uint8_t quality) {
  if (!photoBuffer || photoSize == 0) {
    Serial.println("[TX] ERROR: Invalid photo buffer");
    return sendErrorPacket(ERR_INVALID_PARAMS);
  }

  if (photoSize > PHOTO_MAX_SIZE) {
    Serial.printf("[TX] ERROR: Photo too large (%lu > %d bytes)\n", photoSize, PHOTO_MAX_SIZE);
    return sendErrorPacket(ERR_BUFFER_OVERFLOW);
  }

  Serial.printf("[TX] Starting photo transmission: %lu bytes, quality=%d\n", photoSize, quality);

  // Calculate chunk parameters
  uint16_t numChunks = (photoSize + PHOTO_CHUNK_SIZE - 3 - 1) / (PHOTO_CHUNK_SIZE - 2);

  // Send PHOTO_HEADER first
  PhotoHeader header;
  header.totalSize = photoSize;
  header.numChunks = numChunks;
  header.quality = quality;
  header.reserved = 0;

  Serial.printf("[TX] Sending PHOTO_HEADER: size=%lu bytes, chunks=%d\n",
                header.totalSize, header.numChunks);

  uint16_t sendSize = 0;
  sendSize = myTransfer.txObj(header, sendSize, sizeof(PhotoHeader));
  uint8_t status = myTransfer.sendData(sendSize, PACKET_ID_PHOTO_HEADER);

  if (status == 0) {
    Serial.printf("[TX] ERROR: Failed to send PHOTO_HEADER (status=%d)\n", status);
    return sendErrorPacket(ERR_GENERIC);
  }

  delay(100); // Brief pause before streaming chunks

  // Stream photo chunks
  uint32_t bytesSent = 0;

  for (uint16_t chunkId = 0; chunkId < numChunks; chunkId++) {
    uint32_t bytesRemaining = photoSize - bytesSent;
    size_t chunkDataLen = (bytesRemaining > (PHOTO_CHUNK_SIZE - 2))
                              ? (PHOTO_CHUNK_SIZE - 2)
                              : bytesRemaining;

    const uint8_t *chunkData = photoBuffer + bytesSent;

    if (!sendPhotoChunkWithRetry(chunkId, chunkData, chunkDataLen)) {
      Serial.printf("[TX] ERROR: Failed to send chunk %d after retries\n", chunkId);
      return sendErrorPacket(ERR_GENERIC);
    }

    bytesSent += chunkDataLen;

    // Log progress every 10 chunks
    if ((chunkId + 1) % 10 == 0) {
      Serial.printf("[TX] Progress: %d/%d chunks sent (%lu/%lu bytes)\n",
                    chunkId + 1, numChunks, bytesSent, photoSize);
    }
  }

  Serial.printf("[TX] All %d chunks sent (%lu bytes)\n", numChunks, bytesSent);

  // Send COMPLETE packet
  uint8_t completeStatus = 0; // 0 = success
  sendSize = 0;
  sendSize = myTransfer.txObj(completeStatus, sendSize, sizeof(uint8_t));
  status = myTransfer.sendData(sendSize, PACKET_ID_COMPLETE);

  if (status == 0) {
    Serial.printf("[TX] ERROR: Failed to send COMPLETE packet (status=%d)\n", status);
    return false;
  }

  Serial.println("[TX] COMPLETE packet sent");
  lastPacketTime = millis();

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

    // Pack chunkId + data
    sendSize = myTransfer.txObj(chunkId, sendSize, sizeof(uint16_t));
    sendSize = myTransfer.txObj(chunkData, sendSize, chunkLen);

    uint8_t status = myTransfer.sendData(sendSize, PACKET_ID_PHOTO_CHUNK);

    if (status == 0) {
      Serial.printf("[TX] ERROR: Send failed for chunk %d (status=%d)\n", chunkId, status);
      continue;
    }

    // Wait for ACK or next packet
    masterAckReceived = false;
    delay(50);
    myTransfer.tick();
    processIncomingPackets();

    if (masterAckReceived || attempt == maxRetries - 1) {
      // Either ACK received or last attempt
      lastPacketTime = millis();
      return true;
    }
  }

  return false;
}

// ==================== ERROR HANDLING ====================

bool sendErrorPacket(uint8_t errorCode, const uint8_t *details) {
  Serial.printf("[TX] Sending ERROR packet (code=%d)\n", errorCode);

  ErrorPacket errPkt;
  errPkt.errorCode = errorCode;

  if (details) {
    errPkt.details[0] = details[0];
    errPkt.details[1] = details[1];
    errPkt.details[2] = details[2];
  } else {
    errPkt.details[0] = 0;
    errPkt.details[1] = 0;
    errPkt.details[2] = 0;
  }

  uint16_t sendSize = 0;
  sendSize = myTransfer.txObj(errPkt, sendSize, sizeof(ErrorPacket));
  uint8_t status = myTransfer.sendData(sendSize, PACKET_ID_ERROR);

  return (status != 0);
}

void processIncomingPackets() {
  myTransfer.tick(); // Non-blocking update

  if (!myTransfer.available()) {
    return;
  }

  uint8_t packetID = myTransfer.currentPacketID();

  switch (packetID) {
  case PACKET_ID_ACK: {
    Serial.println("[RX] ACK received");
    masterAckReceived = true;
    break;
  }

  case PACKET_ID_ERROR: {
    ErrorPacket errPkt;
    uint16_t recSize = 0;
    recSize = myTransfer.rxObj(errPkt, recSize, sizeof(ErrorPacket));
    Serial.printf("[RX] ERROR from master (code=%d)\n", errPkt.errorCode);
    break;
  }

  default: {
    Serial.printf("[RX] Unexpected packet ID (0x%02X)\n", packetID);
    break;
  }
  }

  lastPacketTime = millis();
}

bool isMasterAlive() {
  return (millis() - lastPacketTime) < SERIAL_TIMEOUT_MS;
}

// ==================== DIAGNOSTICS ====================

void diagnosticUARTStatus() {
  Serial.println("\n[DIAG] UART Status:");
  Serial.printf("  Port:             Serial2 (UART1)\n");
  Serial.printf("  TX Pin:           GPIO%d\n", UART_TX_PIN);
  Serial.printf("  RX Pin:           GPIO%d\n", UART_RX_PIN);
  Serial.printf("  Baud Rate:        %d\n", UART_BAUD_RATE);
  Serial.printf("  Last Packet:      %lu ms ago\n", millis() - lastPacketTime);
  Serial.printf("  Master Alive:     %s\n", isMasterAlive() ? "YES" : "NO (TIMEOUT)");
  Serial.printf("  ACK Received:     %s\n", masterAckReceived ? "YES" : "NO");
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
