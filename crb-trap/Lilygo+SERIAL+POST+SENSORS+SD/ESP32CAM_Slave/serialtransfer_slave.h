/**
 * @file      serialtransfer_slave.h
 * @brief     SerialTransfer protocol handler for ESP32-CAM slave
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-21
 * @note      Handles UART communication with master via SerialTransfer library
 *
 * Responsibilities:
 *  - Initialize UART and SerialTransfer
 *  - Send READY packet with sensor metadata
 *  - Receive PHOTO command from master
 *  - Capture photo and stream it in chunks
 *  - Handle error/retry logic with CRC validation
 */

#ifndef SERIALTRANSFER_SLAVE_H_
#define SERIALTRANSFER_SLAVE_H_

#include "serialtransfer_protocol.h"
#include "uart_config.h"
#include <Arduino.h>

// ==================== INITIALIZATION ====================

/**
 * @brief Initialize UART and SerialTransfer library
 * @return true if initialized successfully, false if failed
 */
bool initSerialTransfer();

// ==================== READY SIGNAL ====================

/**
 * @brief Send READY packet with sensor metadata to master
 * Slave → Master: announces camera is ready to capture
 * @param metadata PhotoMetadata struct with LUX, fall status, dimensions, timestamp
 * @return true if packet sent successfully, false on error
 */
bool sendReadyPacket(const PhotoMetadata &metadata);

/**
 * @brief Wait for ACK from master (blocking with timeout)
 * @param timeoutMs Max time to wait for ACK (milliseconds)
 * @return true if ACK received, false if timeout
 */
bool waitForMasterAck(uint32_t timeoutMs = SERIAL_TIMEOUT_MS);

// ==================== PHOTO TRANSMISSION ====================

/**
 * @brief Send photo header and then streaming chunks
 * Slave → Master: transmit entire photo in chunked packets
 *
 * Sequence:
 * 1. Send PHOTO_HEADER packet (total size, chunk count)
 * 2. Loop: send PHOTO_CHUNK packets with chunkId + data
 * 3. Send COMPLETE packet (status=0 if success)
 *
 * @param photoBuffer Pointer to JPEG photo data
 * @param photoSize Total size of photo in bytes
 * @param quality JPEG quality (1-63)
 * @return true if all chunks sent and COMPLETE acknowledged, false on error
 */
bool sendPhotoChunked(const uint8_t *photoBuffer, uint32_t photoSize, uint8_t quality);

/**
 * @brief Send a single photo chunk with retry logic
 * @param chunkId Sequence number (0 to numChunks-1)
 * @param chunkData Pointer to chunk payload data
 * @param chunkLen Length of chunk data
 * @param maxRetries Number of retries on failure
 * @return true if chunk acknowledged, false after retries exhausted
 */
bool sendPhotoChunkWithRetry(uint16_t chunkId, const uint8_t *chunkData,
                             size_t chunkLen, uint8_t maxRetries = RETRY_MAX_ATTEMPTS);

// ==================== ERROR HANDLING ====================

/**
 * @brief Send error packet to master
 * @param errorCode Error code (ERR_*)
 * @param details Optional error details (3 bytes)
 * @return true if sent, false on error
 */
bool sendErrorPacket(uint8_t errorCode, const uint8_t *details = nullptr);

/**
 * @brief Check for and handle incoming master commands
 * Non-blocking; updates module state if packet received:
 *  - PACKET_ID_ACK: sets masterAckReceived flag
 *  - Others: logs error
 *
 * Call this regularly from main loop to process incoming data
 */
void processIncomingPackets();

/**
 * @brief Check if master connection is alive (got packet within timeout)
 * @return true if last packet received within SERIAL_TIMEOUT_MS
 */
bool isMasterAlive();

// ==================== DIAGNOSTICS ====================

/**
 * @brief Log current UART and SerialTransfer status to Serial
 */
void diagnosticUARTStatus();

#endif // SERIALTRANSFER_SLAVE_H_
