/**
 * @file      serialtransfer_protocol.h
 * @brief     SerialTransfer protocol definitions for ESP32-CAM (slave) ↔ LilyGo (master) communication
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-03-21
 * @note      Shared by both slave and master; defines packet types, metadata structs, and constants
 *
 * SerialTransfer handles packet framing automatically:
 * [START_BYTE(0x7E)][PACKET_ID][COBS_OVH][LENGTH][PAYLOAD...][CRC8][STOP_BYTE(0x81)]
 *
 * This header defines:
 * - Packet ID constants (semantic meaning of each message type)
 * - Data structures for the payload
 * - Timeouts and retry parameters
 */

#ifndef SERIALTRANSFER_PROTOCOL_H_
#define SERIALTRANSFER_PROTOCOL_H_

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

// ==================== PACKET ID DEFINITIONS ====================
// Packet IDs (1 byte, assigned to SerialTransfer's packetID field)

#define PACKET_ID_READY 0x01        /**< Slave → Master: Camera ready with metadata (PhotoMetadata) */
#define PACKET_ID_ACK 0x02          /**< Master → Slave: Acknowledge command (AckPacket) */
#define PACKET_ID_PHOTO_HEADER 0x03 /**< Slave → Master: Photo dimensions and chunk count (PhotoHeader) */
#define PACKET_ID_PHOTO_CHUNK 0x04  /**< Slave → Master: Photo data chunk (PhotoChunk) */
#define PACKET_ID_COMPLETE 0x05     /**< Slave → Master: Photo transmission complete (uint8_t status) */
#define PACKET_ID_ERROR 0xFE        /**< Error packet: (ErrorPacket) */

// ==================== TIMING & RETRY PARAMETERS ====================

#define SERIAL_TIMEOUT_MS 5000 /**< Max time to wait for incoming SerialTransfer packet */
#define RETRY_MAX_ATTEMPTS 3   /**< Maximum retries for failed transmissions */
#define RETRY_BACKOFF_MS 500   /**< Delay between retries (ms) */

#define PHOTO_CHUNK_SIZE 250  /**< Payload size per photo chunk (max 254 for SerialTransfer) */
#define PHOTO_MAX_SIZE 400000 /**< Max photo size (400 KB) */

// ==================== DATA STRUCTURES ====================

/**
 * @struct PhotoMetadata
 * @brief Metadata sent with READY packet (PACKET_ID_READY)
 * Slave → Master: announces readiness and sensor data
 */
struct PhotoMetadata {
  uint16_t luxValue;    /**< Light intensity (LUX) from ambient light sensor */
  uint8_t isFallen;     /**< Fall detection: 1=fallen, 0=not fallen */
  uint16_t photoWidth;  /**< Camera capture width (pixels) */
  uint16_t photoHeight; /**< Camera capture height (pixels) */
  uint32_t timestamp;   /**< Epoch time or system uptime (seconds) */
} __attribute__((packed));

static_assert(sizeof(PhotoMetadata) <= PHOTO_CHUNK_SIZE,
              "PhotoMetadata must fit in single SerialTransfer packet");

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

/**
 * @struct AckPacket
 * @brief Acknowledgment from Master to Slave (PACKET_ID_ACK)
 * Master → Slave: confirm receipt or request retry
 */
struct AckPacket {
  uint8_t command;     /**< Command: 0x00=ready for photo, 0x01=resend last chunk */
  uint8_t reserved[3]; /**< Reserved for future use */
} __attribute__((packed));

static_assert(sizeof(AckPacket) <= PHOTO_CHUNK_SIZE,
              "AckPacket must fit in single SerialTransfer packet");

/**
 * @struct ErrorPacket
 * @brief Error notification (PACKET_ID_ERROR)
 * Slave → Master or Master → Slave: report error condition
 */
struct ErrorPacket {
  uint8_t errorCode;  /**< Error code (0=generic, 1=timeout, 2=crc_error, 3=invalid_params) */
  uint8_t details[3]; /**< Error-specific details */
} __attribute__((packed));

static_assert(sizeof(ErrorPacket) <= PHOTO_CHUNK_SIZE,
              "ErrorPacket must fit in single SerialTransfer packet");

// ==================== ERROR CODES ====================

#define ERR_NONE 0
#define ERR_GENERIC 1
#define ERR_TIMEOUT 2
#define ERR_CRC_ERROR 3
#define ERR_INVALID_PARAMS 4
#define ERR_BUFFER_OVERFLOW 5
#define ERR_CAMERA_FAIL 6

#endif // SERIALTRANSFER_PROTOCOL_H_
