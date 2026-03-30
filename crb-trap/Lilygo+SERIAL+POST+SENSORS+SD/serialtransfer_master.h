/**
 * @file      serialtransfer_master.h
 * @brief     SerialTransfer protocol handler for LilyGo master
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-21
 * @note      Handles UART communication with ESP32-CAM slave via SerialTransfer library
 *
 * Responsibilities:
 *  - Initialize UART and SerialTransfer on master side
 *  - Wait for READY packet with sensor metadata from slave
 *  - Receive photo chunks and reassemble into complete image
 *  - Validate CRC and handle retries
 *  - Send ACK and command packets to slave
 */

#ifndef SERIALTRANSFER_MASTER_H_
#define SERIALTRANSFER_MASTER_H_

#include "serialtransfer_protocol.h"
#include <Arduino.h>

// ==================== INITIALIZATION ====================

/**
 * @brief Initialize UART and SerialTransfer on master
 * @param txPin GPIO pin for UART TX (e.g., 18 on LilyGo)
 * @param rxPin GPIO pin for UART RX (e.g., 19 on LilyGo)
 * @return true if initialized successfully, false if failed
 */
bool initMasterSerialTransfer(uint8_t txPin = 18, uint8_t rxPin = 19);

// ==================== SLAVE DISCOVERY & READINESS ====================

/**
 * @brief Wait for READY packet from slave (blocking with timeout)
 * Reads sensor metadata from slave to determine LUX, fall status, etc.
 *
 * @param metadata Output parameter: populated with PhotoMetadata from slave
 * @param timeoutMs Max time to wait for READY packet
 * @return true if READY received and metadata extracted, false on timeout
 */
bool waitForSlaveReady(PhotoMetadata &metadata, uint32_t timeoutMs = SERIAL_TIMEOUT_MS);

// ==================== PHOTO RECEPTION ====================

/**
 * @brief Send ACK command to slave to start photo transmission
 * Master → Slave: sends command (0x00=ready for photo)
 * @return true if ACK sent successfully
 */
bool sendSlaveACK();

/**
 * @brief Receive photo chunks and reassemble complete image
 * Master → Slave communication:
 * 1. Receive PHOTO_HEADER (total size, chunk count)
 * 2. Allocate buffer
 * 3. Loop: receive PHOTO_CHUNK packets
 * 4. Verify CRC on complete photo
 * 5. Receive COMPLETE packet
 *
 * Buffer is dynamically allocated; caller must free() after use
 *
 * @param outBuffer Output parameter: pointer to allocated photo buffer
 * @param outSize Output parameter: total photo size in bytes
 * @return true if photo completely received and CRC valid, false on error
 */
bool receivePhotoChunked(uint8_t *&outBuffer, uint32_t &outSize);

/**
 * @brief Receive a single photo chunk (blocking)
 * @param chunkId Output: sequence number of this chunk
 * @param chunkData Output: pointer to chunk payload data
 * @param chunkLen Output: length of chunk data
 * @param maxRetries Number of retries if CRC fails
 * @return true if chunk received with valid CRC, false on error
 */
bool receivePhotoChunkWithRetry(uint16_t &chunkId, uint8_t *&chunkData,
                                size_t &chunkLen, uint8_t maxRetries = RETRY_MAX_ATTEMPTS);

// ==================== PHOTO PROCESSING ====================

/**
 * @brief Validate JPEG photo buffer (check SOI marker)
 * @param buffer Photo data
 * @param size Photo size in bytes
 * @return true if valid JPEG, false otherwise
 */
bool validateJpegPhoto(const uint8_t *buffer, uint32_t size);

/**
 * @brief Calculate CRC32 checksum of photo buffer
 * @param buffer Photo data
 * @param size Photo size
 * @return CRC32 value
 */
uint32_t crc32Photo(const uint8_t *buffer, uint32_t size);

// ==================== ERROR HANDLING ====================

/**
 * @brief Send error packet to slave
 * @param errorCode Error code (ERR_*)
 * @param details Optional error details (3 bytes)
 * @return true if sent
 */
bool sendSlaveError(uint8_t errorCode, const uint8_t *details = nullptr);

/**
 * @brief Check for and handle incoming slave packets
 * Non-blocking; updates module state if packet received
 */
void processIncomingSlavePackets();

/**
 * @brief Check if slave connection is alive (got packet within timeout)
 * @return true if last packet received within SERIAL_TIMEOUT_MS
 */
bool isSlaveAlive();

// ==================== CLEANUP ====================

/**
 * @brief Free dynamically allocated photo buffer
 * Safe to call even if buffer is nullptr
 */
void freePhotoBuffer();

// ==================== DIAGNOSTICS ====================

/**
 * @brief Log UART and master status to Serial
 */
void diagnosticMasterUARTStatus();

#endif // SERIALTRANSFER_MASTER_H_
