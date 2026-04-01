/**
 * @file      serialtransfer_slave.h
 * @brief     Hybrid protocol handler for ESP32-CAM slave:
 *            ASCII commands + SerialTransfer binary image data
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-31
 * @note      Commands use simple serial (text), image chunks use SerialTransfer (binary)
 *
 * Responsibilities:
 *  - Initialize UART and SerialTransfer (binary mode only)
 *  - Send ASCII READY signal with sensor metadata
 *  - Receive and parse ASCII PHOTO command from master
 *  - Capture photo and stream it as binary chunks via SerialTransfer
 *  - Handle error/retry logic with timeout management
 */

#ifndef SERIALTRANSFER_SLAVE_H_
#define SERIALTRANSFER_SLAVE_H_

#include "serialtransfer_protocol.h"
#include "uart_config.h"
#include <Arduino.h>

// ==================== DATA STRUCTURES ====================

/**
 * @struct PhotoCommand
 * @brief Parsed photo command from master
 */
struct PhotoCommand {
  char cmdName[16]; /**< Command name (e.g., "PHOTO", "STAT") */
  uint16_t width;   /**< Requested photo width (PHOTO command) */
  uint16_t height;  /**< Requested photo height (PHOTO command) */
  uint8_t quality;  /**< Requested JPEG quality 1-63 (PHOTO command) */
};

// ==================== INITIALIZATION ====================

/**
 * @brief Initialize UART and SerialTransfer library
 * SerialTransfer is configured in BINARY mode (for image chunks only)
 * ASCII commands are received via simple UART polling
 * @return true if initialized successfully, false if failed
 */
bool initSerialTransfer();

// ==================== ASCII COMMAND PARSING ====================

/**
 * @brief Check if a complete ASCII command line has been received
 * Non-blocking; returns true when a full line ending with \n is available
 * @return true if new command ready (call getCommand to retrieve)
 */
bool commandAvailable();

/**
 * @brief Retrieve the last parsed command
 * @param outCmd [out] PhotoCommand structure to fill
 * @return true if valid command available, false if none
 */
bool getCommand(PhotoCommand &outCmd);

// ==================== READY SIGNAL (ASCII) ====================

/**
 * @brief Send ASCII READY signal with sensor metadata to master
 * Format: "READY:lux:fall:timestamp:width:height\n"
 * Slave → Master: announces camera is ready for capture command
 *
 * @param lux Light intensity (LUX) from ambient light sensor
 * @param isFallen Fall detection status (0 or 1)
 * @param photoWidth Camera capture width (pixels) that will be used
 * @param photoHeight Camera capture height (pixels) that will be used
 * @param timestamp System time or uptime (seconds, 32-bit)
 * @return true if sent successfully, false on error
 */
bool sendReadyPacket(uint16_t lux, uint8_t isFallen, uint16_t photoWidth,
                     uint16_t photoHeight, uint32_t timestamp);

// ==================== PHOTO TRANSMISSION (BINARY VIA SERIALTRANSFER) ====================

/**
 * @brief Send photo header and streaming chunks
 * Slave → Master: transmit entire photo in chunked binary packets
 *
 * Sequence:
 * 1. Send PHOTO_HEADER packet (binary via SerialTransfer)
 * 2. Loop: send PHOTO_CHUNK packets with chunkId + data
 * 3. Send ASCII "DONE:0\n" response indicating completion
 *
 * @param photoBuffer Pointer to JPEG photo data
 * @param photoSize Total size of photo in bytes
 * @param quality JPEG quality (1-63)
 * @return true if all chunks sent, false on error
 */
bool sendPhotoChunked(const uint8_t *photoBuffer, uint32_t photoSize, uint8_t quality);

/**
 * @brief Send a single photo chunk with retry logic (BINARY)
 * @param chunkId Sequence number (0 to numChunks-1)
 * @param chunkData Pointer to chunk payload data
 * @param chunkLen Length of chunk data (must be <= PHOTO_CHUNK_SIZE - 2)
 * @param maxRetries Number of retries on transmission failure (default 3)
 * @return true if chunk sent successfully, false after retries exhausted
 */
bool sendPhotoChunkWithRetry(uint16_t chunkId, const uint8_t *chunkData,
                             size_t chunkLen, uint8_t maxRetries = RETRY_MAX_ATTEMPTS);

// ==================== ASCII RESPONSE MESSAGES ====================

/**
 * @brief Send ASCII error message to master
 * Format: "ERR:code\n"
 * @param errorCode Error code (ERR_*)
 * @return true if sent, false on write error
 */
bool sendErrorMessage(uint8_t errorCode);

/**
 * @brief Send ASCII acknowledgment message to master
 * Format: "ACK:status\n"
 * @param status 0 for success, non-zero for failure
 * @return true if sent, false on write error
 */
bool sendAckMessage(uint8_t status);

// ==================== STATUS & DIAGNOSTICS ====================

/**
 * @brief Check if master connection is alive (based on recent command)
 * @return true if last command received within SERIAL_CHUNK_TIMEOUT_MS
 */
bool isMasterAlive();

/**
 * @brief Log current UART and SerialTransfer status to serial console
 */
void diagnosticUARTStatus();

#endif // SERIALTRANSFER_SLAVE_H_
