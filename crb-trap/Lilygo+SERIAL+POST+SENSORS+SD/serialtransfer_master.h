/**
 * @file      serialtransfer_master.h
 * @brief     Hybrid protocol handler for LilyGo master:
 *            ASCII commands + SerialTransfer binary image data
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-31
 * @note      Commands use simple serial (text), image chunks use SerialTransfer (binary)
 *
 * Responsibilities:
 *  - Initialize UART and SerialTransfer on master side
 *  - Parse ASCII READY message with sensor metadata from slave
 *  - Send ASCII PHOTO command to slave
 *  - Receive binary photo chunks via SerialTransfer and reassemble
 *  - Validate JPEG and handle retries
 */

#ifndef SERIALTRANSFER_MASTER_H_
#define SERIALTRANSFER_MASTER_H_

#include "serialtransfer_protocol.h"
#include <Arduino.h>

// ==================== INITIALIZATION ====================

/**
 * @brief Initialize UART and SerialTransfer on master
 * SerialTransfer is configured in BINARY mode (for image chunks only)
 * ASCII commands are sent via simple UART writes
 *
 * @param txPin GPIO pin for UART TX (e.g., 18 on LilyGo)
 * @param rxPin GPIO pin for UART RX (e.g., 19 on LilyGo)
 * @return true if initialized successfully, false if failed
 */
bool initMasterSerialTransfer(uint8_t txPin = 18, uint8_t rxPin = 19);

// ==================== SLAVE DISCOVERY & READINESS (ASCII) ====================

/**
 * @brief Wait for ASCII READY message from slave (blocking with timeout)
 * Format: "READY:lux:fall:timestamp:width:height\n"
 *
 * @param timeoutMs Max time to wait for READY message
 * @return true if READY received and parsed, false on timeout
 */
bool waitForSlaveReady(uint32_t timeoutMs = ASCII_RESPONSE_TIMEOUT_MS);

// ==================== PHOTO COMMAND (ASCII) ====================

/**
 * @brief Send ASCII PHOTO command to slave
 * Format: "PHOTO:width:height:quality\n"
 * Master → Slave: triggers photo capture at specified parameters
 *
 * @param width Requested photo width (pixels)
 * @param height Requested photo height (pixels)
 * @param quality JPEG quality (1-63)
 * @return true if command sent successfully
 */
bool sendPhotoCommand(uint16_t width, uint16_t height, uint8_t quality);

// ==================== PHOTO RECEPTION (BINARY VIA SERIALTRANSFER) ====================

/**
 * @brief Receive photo chunks and reassemble complete image
 * Sequence:
 * 1. Receive PHOTO_HEADER packet (binary via SerialTransfer)
 * 2. Allocate buffer
 * 3. Loop: receive PHOTO_CHUNK packets with retry logic
 * 4. Verify JPEG SOI marker
 * 5. Wait for ASCII "DONE:0\n" confirmation
 *
 * Buffer is dynamically allocated; caller must free() after use
 *
 * @param outBuffer Output parameter: pointer to allocated photo buffer
 * @param outSize Output parameter: total photo size in bytes
 * @return true if photo completely received and valid, false on error
 */
bool receivePhotoChunked(uint8_t *&outBuffer, uint32_t &outSize);

/**
 * @brief Receive a single photo chunk via SerialTransfer (blocking)
 * @param chunkId Output: sequence number of this chunk
 * @param chunkData Output: pointer to chunk payload data (dynamically allocated)
 * @param chunkLen Output: length of chunk data
 * @param maxRetries Number of retries if reception timeout
 * @return true if chunk received successfully, false on error
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

// ==================== STATUS & DIAGNOSTICS ====================

/**
 * @brief Check if slave connection is alive (based on recent activity)
 * @return true if last activity within SERIAL_CHUNK_TIMEOUT_MS
 */
bool isSlaveAlive();

/**
 * @brief Free dynamically allocated photo buffer
 * Safe to call even if buffer is nullptr
 */
void freePhotoBuffer();

/**
 * @brief Log UART, protocol, and master status to Serial
 */
void diagnosticMasterUARTStatus();

/**
 * @brief Purge any remaining ASCII bytes from UART buffer
 * Used to clear residual data after ASCII mode transitions to binary mode
 * Prevents stale ASCII bytes from corrupting binary packet reception
 */
void purgeUARTBuffer();

#endif // SERIALTRANSFER_MASTER_H_
