/**
 * @file      serialtransfer_protocol.h
 * @brief     Dual-mode protocol: ASCII commands + SerialTransfer binary image data
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-31
 * @note      Hybrid protocol for ESP32-CAM (slave) ↔ LilyGo (master) communication
 *
 * PROTOCOL ARCHITECTURE:
 * - Commands & responses: ASCII text (simple serial, line-terminated with \n)
 * - Image data: Binary packets via SerialTransfer library (COBS encoding, CRC8)
 *
 * ASCII Command Format (Slave → Master):
 *   Lines terminated with \n (0x0A)
 *   Examples: "READY:250:0:160000000\n", "ERR:2\n", "DONE:0\n"
 *
 * SerialTransfer Binary Format (Image transmission only):
 *   [START_BYTE(0x7E)][PACKET_ID][COBS_OVH][LENGTH][PAYLOAD...][CRC8][STOP_BYTE(0x81)]
 *   PACKET_ID values: 0x03 (HEADER), 0x04 (CHUNK)
 *
 * This header defines:
 * - ASCII command/response strings
 * - Binary packet IDs (image data only)
 * - Data structures for image payload
 * - Timeouts and retry parameters
 */

#ifndef SERIALTRANSFER_PROTOCOL_H_
#define SERIALTRANSFER_PROTOCOL_H_

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

// ==================== ASCII COMMAND DEFINITIONS ====================
// Commands and responses are plain text, terminated with \n (0x0A)
// Format: "COMMAND:param1:param2:...\n"

// ---- Slave → Master ----
#define CMD_READY "READY" /**< Format: "READY:lux:fall:timestamp:width:height\n" */
#define CMD_DONE "DONE"   /**< Format: "DONE:status\n" (status: 0=success, other=error) */
#define CMD_ERR "ERR"     /**< Format: "ERR:code\n" */

// ---- Master → Slave ----
#define CMD_PHOTO "PHOTO" /**< Format: "PHOTO:width:height:quality\n" */
#define CMD_STAT "STAT"   /**< Format: "STAT\n" - Request status */
#define CMD_PING "PING"   /**< Format: "PING\n" - Heartbeat check */
#define CMD_RESET "RESET" /**< Format: "RESET\n" - Abort and reset */

// ASCII response suffixes (append to command prefix with ':')
#define RESP_OK ":0"   /**< Success response suffix */
#define RESP_FAIL ":1" /**< Failure response suffix */

// ==================== BINARY PACKET ID DEFINITIONS ====================
// Packet IDs used ONLY for image data streaming via SerialTransfer
// Image metadata and command acks now use ASCII text

#define PACKET_ID_PHOTO_HEADER 0x03 /**< Slave → Master: Photo dimensions (binary struct) */
#define PACKET_ID_PHOTO_CHUNK 0x04  /**< Slave → Master: Photo data chunk (binary struct) */
#define PACKET_ID_ERROR 0xFE        /**< Optional: Binary error details (reserved) */

// ==================== TIMING & RETRY PARAMETERS ====================

#define ASCII_RESPONSE_TIMEOUT_MS 2000 /**< Max time to wait for ASCII response (commands) */
#define SERIAL_CHUNK_TIMEOUT_MS 5000   /**< Max time to wait for incoming SerialTransfer chunk */
#define RETRY_MAX_ATTEMPTS 3           /**< Maximum retries for failed chunk transmissions */
#define RETRY_BACKOFF_MS 500           /**< Delay between retries (ms) */

#define PHOTO_CHUNK_SIZE 250  /**< Payload size per photo chunk (max 254 for SerialTransfer) */
#define PHOTO_MAX_SIZE 400000 /**< Max photo size (400 KB) */

// ==================== ASCII BUFFER SIZES ====================
#define ASCII_CMD_MAX_LEN 64   /**< Max length of ASCII command line (including \n) */
#define ASCII_RESP_MAX_LEN 128 /**< Max length of ASCII response line */

// ==================== DATA STRUCTURES ====================

/**
 * @struct PhotoHeader
 * @brief Photo dimensions sent before chunk stream (PACKET_ID_PHOTO_HEADER)
 * Slave → Master: tells master total photo size and chunk count
 */
struct PhotoHeader {
  uint32_t totalSize; /**< Total JPEG size in bytes */
  uint16_t numChunks; /**< Total number of PHOTO_CHUNK packets to follow */
  uint8_t quality;    /**< JPEG quality (1-63) */
  uint8_t reserved;   /**< Reserved for future use */
} __attribute__((packed));

static_assert(sizeof(PhotoHeader) <= PHOTO_CHUNK_SIZE,
              "PhotoHeader must fit in single SerialTransfer packet");

/**
 * @struct PhotoChunk
 * @brief Single chunk of photo data (PACKET_ID_PHOTO_CHUNK)
 * Slave → Master: streaming chunk with sequence ID
 * Layout: [chunkId (2 bytes) | data (PHOTO_CHUNK_SIZE - 2 bytes)]
 */
struct PhotoChunk {
  uint16_t chunkId;                   /**< Sequence number (0 to numChunks-1) */
  uint8_t data[PHOTO_CHUNK_SIZE - 2]; /**< Payload (photo JPEG bytes) */
} __attribute__((packed));

static_assert(sizeof(PhotoChunk) <= PHOTO_CHUNK_SIZE,
              "PhotoChunk must fit in single SerialTransfer packet");

// ==================== ERROR CODES ====================
// Error codes used in ASCII "ERR:code" responses and optionally in binary error packets

#define ERR_NONE 0            /**< No error */
#define ERR_TIMEOUT 1         /**< Communication timeout */
#define ERR_INVALID_PARAMS 2  /**< Invalid command parameters (e.g., resolution > max) */
#define ERR_CAMERA_FAIL 3     /**< Camera capture failed */
#define ERR_BUFFER_OVERFLOW 4 /**< Photo size exceeds buffer capacity */
#define ERR_CRC_ERROR 5       /**< CRC validation failed on chunk */
#define ERR_GENERIC 6         /**< Generic/unknown error */

// ==================== FRAME SIZE & QUALITY CONSTRAINTS ====================
// Valid ranges for photo parameters sent in PHOTO command

#define PHOTO_WIDTH_MIN 320
#define PHOTO_WIDTH_MAX 1600
#define PHOTO_HEIGHT_MIN 240
#define PHOTO_HEIGHT_MAX 1200
#define PHOTO_QUALITY_MIN 1
#define PHOTO_QUALITY_MAX 63

#endif // SERIALTRANSFER_PROTOCOL_H_
