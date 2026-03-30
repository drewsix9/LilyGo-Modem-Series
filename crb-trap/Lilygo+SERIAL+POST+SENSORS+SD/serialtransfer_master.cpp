/**
 * @file      serialtransfer_master.cpp
 * @brief     SerialTransfer protocol implementation for LilyGo master
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-21
 */

#include "serialtransfer_master.h"
#include "serialtransfer_protocol.h"
#include "utilities.h"
#include <SerialTransfer.h>

// ==================== GLOBAL STATE ====================

static SerialTransfer masterTransfer;
static HardwareSerial masterUART(1); // UART1 on LilyGo

static PhotoMetadata lastReceivedMetadata = {0};
static uint8_t *photoBuffer = nullptr;
static uint32_t photoBufferSize = 0;
static bool photoReceptionComplete = false;

static volatile uint32_t lastSlavePacketTime = 0;
static volatile bool slaveErrorReceived = false;

// ==================== INITIALIZATION ====================

bool initMasterSerialTransfer(uint8_t txPin, uint8_t rxPin) {
  Serial.println("[MASTER-UART] Initializing master UART communication...");

  // Configure custom UART with specified pins
  masterUART.begin(MASTER_UART_BAUD_RATE, SERIAL_8N1, rxPin, txPin);

  Serial.printf("[MASTER-UART] Configured: GPIO%d (RX) + GPIO%d (TX) @ %d baud\n",
                rxPin, txPin, MASTER_UART_BAUD_RATE);

  delay(100);

  // Initialize SerialTransfer on master UART
  masterTransfer.begin(masterUART, true, Serial); // debug=true

  Serial.println("[MASTER-UART] SerialTransfer initialized");
  Serial.printf("[MASTER-UART] RX buffer ready (max %d bytes per packet)\n", PHOTO_CHUNK_SIZE);

  lastSlavePacketTime = millis();

  return true;
}

// ==================== SLAVE DISCOVERY & READINESS ====================

bool waitForSlaveReady(PhotoMetadata &metadata, uint32_t timeoutMs) {
  uint32_t startTime = millis();

  Serial.printf("[MASTER-RX] Waiting for slave READY packet (timeout=%d ms)...\n", timeoutMs);

  while (millis() - startTime < timeoutMs) {
    masterTransfer.tick(); // Non-blocking update

    if (masterTransfer.available()) {
      uint8_t packetID = masterTransfer.currentPacketID();

      if (packetID == PACKET_ID_READY) {
        uint16_t recSize = 0;
        recSize = masterTransfer.rxObj(metadata, recSize, sizeof(PhotoMetadata));

        Serial.printf("[MASTER-RX] READY received (LUX=%d, fallen=%d, %dx%d)\n",
                      metadata.luxValue, metadata.isFallen,
                      metadata.photoWidth, metadata.photoHeight);

        lastReceivedMetadata = metadata;
        lastSlavePacketTime = millis();
        return true;
      } else {
        Serial.printf("[MASTER-RX] Unexpected packet ID (0x%02X), still waiting for READY...\n", packetID);
      }
    }

    delay(10);
  }

  Serial.printf("[MASTER-RX] ERROR: READY timeout after %d ms\n", timeoutMs);
  return false;
}

// ==================== PHOTO RECEPTION ====================

bool sendSlaveACK() {
  Serial.println("[MASTER-TX] Sending ACK to slave...");

  AckPacket ack;
  ack.command = 0x00; // Command: ready for photo
  ack.reserved[0] = 0;
  ack.reserved[1] = 0;
  ack.reserved[2] = 0;

  uint16_t sendSize = 0;
  sendSize = masterTransfer.txObj(ack, sendSize, sizeof(AckPacket));
  uint8_t status = masterTransfer.sendData(sendSize, PACKET_ID_ACK);

  if (status == 0) {
    Serial.printf("[MASTER-TX] ERROR: Failed to send ACK (status=%d)\n", status);
    return false;
  }

  Serial.println("[MASTER-TX] ACK sent");
  lastSlavePacketTime = millis();
  return true;
}

bool receivePhotoChunked(uint8_t *&outBuffer, uint32_t &outSize) {
  outBuffer = nullptr;
  outSize = 0;
  photoReceptionComplete = false;

  // Wait for PHOTO_HEADER
  Serial.println("[MASTER-RX] Waiting for PHOTO_HEADER from slave...");
  uint32_t headerStartTime = millis();

  while (millis() - headerStartTime < SERIAL_TIMEOUT_MS) {
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
          return sendSlaveError(ERR_BUFFER_OVERFLOW);
        }

        // Allocate new buffer
        freePhotoBuffer();
        photoBuffer = (uint8_t *)malloc(header.totalSize);
        if (!photoBuffer) {
          Serial.printf("[MASTER-RX] ERROR: Failed to allocate %lu bytes\n", header.totalSize);
          return sendSlaveError(ERR_BUFFER_OVERFLOW);
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
            return sendSlaveError(ERR_GENERIC);
          }

          // Copy chunk to buffer
          if (bytesReceived + chunkLen > photoBufferSize) {
            Serial.printf("[MASTER-RX] ERROR: Chunk overflow (received %lu + %zu > %lu)\n",
                          bytesReceived, chunkLen, photoBufferSize);
            return sendSlaveError(ERR_BUFFER_OVERFLOW);
          }

          memcpy(photoBuffer + bytesReceived, chunkData, chunkLen);
          bytesReceived += chunkLen;

          // Log progress every 10 chunks
          if ((chunkIdx + 1) % 10 == 0 || chunkIdx == expectedChunks - 1) {
            Serial.printf("[MASTER-RX] Progress: %d/%d chunks (%lu/%lu bytes)\n",
                          chunkIdx + 1, expectedChunks, bytesReceived, photoBufferSize);
          }
        }

        // Wait for COMPLETE packet
        Serial.println("[MASTER-RX] All chunks received, waiting for COMPLETE packet...");
        uint32_t completeStartTime = millis();

        while (millis() - completeStartTime < SERIAL_TIMEOUT_MS) {
          masterTransfer.tick();

          if (masterTransfer.available()) {
            uint8_t packetID = masterTransfer.currentPacketID();

            if (packetID == PACKET_ID_COMPLETE) {
              uint8_t completeStatus = 0;
              uint16_t recSize = 0;
              recSize = masterTransfer.rxObj(completeStatus, recSize, sizeof(uint8_t));

              Serial.printf("[MASTER-RX] COMPLETE received (status=%d)\n", completeStatus);

              if (completeStatus != 0) {
                Serial.println("[MASTER-RX] WARNING: Slave reported error in photo transmission");
              }

              // Validate JPEG
              if (!validateJpegPhoto(photoBuffer, photoBufferSize)) {
                Serial.println("[MASTER-RX] ERROR: Photo buffer does not contain valid JPEG");
                return sendSlaveError(ERR_GENERIC);
              }

              outBuffer = photoBuffer;
              outSize = photoBufferSize;
              photoReceptionComplete = true;
              lastSlavePacketTime = millis();

              Serial.printf("[MASTER-RX] Photo reception COMPLETE: %lu bytes\n", outSize);
              return true;
            }
          }

          delay(10);
        }

        Serial.println("[MASTER-RX] ERROR: COMPLETE packet timeout");
        return sendSlaveError(ERR_TIMEOUT);
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

    while (millis() - chunkStartTime < SERIAL_TIMEOUT_MS) {
      masterTransfer.tick();

      if (masterTransfer.available()) {
        uint8_t packetID = masterTransfer.currentPacketID();

        if (packetID == PACKET_ID_PHOTO_CHUNK) {
          // Decode chunk: [chunkId (2) | data (PHOTO_CHUNK_SIZE-2)]
          uint16_t recSize = 0;
          recSize = masterTransfer.rxObj(chunkId, recSize, sizeof(uint16_t));

          // Extract just the data portion (don't read beyond what's in the packet)
          uint8_t *tempData = (uint8_t *)malloc(PHOTO_CHUNK_SIZE);
          recSize = masterTransfer.rxObj(tempData, recSize, PHOTO_CHUNK_SIZE - 2);

          // Calculate actual chunk size from packet
          chunkLen = recSize; // rxObj returns number of bytes actually received
          chunkData = tempData;

          Serial.printf("[MASTER-RX] Chunk %d received (%zu bytes)\n", chunkId, chunkLen);
          lastSlavePacketTime = millis();
          return true;
        }
      }

      delay(5);
    }

    if (attempt < maxRetries - 1) {
      Serial.printf("[MASTER-RX] Chunk timeout, retry...\n");
      // Request resend by sending ACK with resend command
      AckPacket ack;
      ack.command = 0x01; // Resend last chunk
      ack.reserved[0] = 0;
      ack.reserved[1] = 0;
      ack.reserved[2] = 0;

      uint16_t sendSize = 0;
      sendSize = masterTransfer.txObj(ack, sendSize, sizeof(AckPacket));
      masterTransfer.sendData(sendSize, PACKET_ID_ACK);
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

// ==================== ERROR HANDLING ====================

bool sendSlaveError(uint8_t errorCode, const uint8_t *details) {
  Serial.printf("[MASTER-TX] Sending ERROR packet to slave (code=%d)\n", errorCode);

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
  sendSize = masterTransfer.txObj(errPkt, sendSize, sizeof(ErrorPacket));
  uint8_t status = masterTransfer.sendData(sendSize, PACKET_ID_ERROR);

  return (status != 0);
}

void processIncomingSlavePackets() {
  masterTransfer.tick();

  if (!masterTransfer.available()) {
    return;
  }

  uint8_t packetID = masterTransfer.currentPacketID();

  switch (packetID) {
  case PACKET_ID_ERROR: {
    ErrorPacket errPkt;
    uint16_t recSize = 0;
    recSize = masterTransfer.rxObj(errPkt, recSize, sizeof(ErrorPacket));
    Serial.printf("[MASTER-RX] ERROR from slave (code=%d)\n", errPkt.errorCode);
    slaveErrorReceived = true;
    break;
  }

  default: {
    // Ignore other packets in passive processing
    break;
  }
  }

  lastSlavePacketTime = millis();
}

bool isSlaveAlive() {
  return (millis() - lastSlavePacketTime) < SERIAL_TIMEOUT_MS;
}

// ==================== CLEANUP ====================

void freePhotoBuffer() {
  if (photoBuffer) {
    free(photoBuffer);
    photoBuffer = nullptr;
    photoBufferSize = 0;
  }
}

// ==================== DIAGNOSTICS ====================

void diagnosticMasterUARTStatus() {
  Serial.println("\n[MASTER-DIAG] UART Status:");
  Serial.printf("  Port:             Serial1\n");
  Serial.printf("  Last Packet:      %lu ms ago\n", millis() - lastSlavePacketTime);
  Serial.printf("  Slave Alive:      %s\n", isSlaveAlive() ? "YES" : "NO (TIMEOUT)");
  Serial.printf("  Photo Buffer:     %s (%lu bytes)\n",
                photoBuffer ? "ALLOCATED" : "FREE", photoBufferSize);
  Serial.printf("  Photo Complete:   %s\n", photoReceptionComplete ? "YES" : "NO");
  Serial.println();
}
