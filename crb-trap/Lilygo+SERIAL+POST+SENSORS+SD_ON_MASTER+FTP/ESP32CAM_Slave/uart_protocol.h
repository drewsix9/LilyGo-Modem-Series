/**
 * @file      uart_protocol.h
 * @brief     Shared UART hybrid protocol definitions for master and slave.
 *            ASCII control commands + SerialTransfer binary chunk IDs + CRC32.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

// ==================== UART CONFIGURATION ====================
#define MASTER_UART_BAUD_RATE 115200

// ==================== ASCII COMMANDS ====================
// Slave -> Master
#define CMD_READY "READY"
#define CMD_SIZE "SIZE"
#define CMD_DONE "DONE"
#define CMD_ERROR "ERROR"

// Master -> Slave
#define CMD_SNAP "SNAP"
#define CMD_SEND "SEND"

// ==================== SERIALTRANSFER PACKET IDS ====================
#define PACKET_ID_PHOTO_CHUNK 0x04

// ==================== PHOTO CHUNK LAYOUT ====================
#define PHOTO_CHUNK_DATA_MAX 248
#define PHOTO_MAX_SIZE 500000

// ==================== ERROR CODES ====================
#define ERR_CMD_TIMEOUT 0x00
#define ERR_CAPTURE_FAILED 0x01
#define ERR_SEND_FAILED 0x02
#define ERR_INVALID_PARAMS 0x03
#define ERR_SEQ_ERROR 0x04
#define ERR_CRC_MISMATCH 0x05

// ==================== TIMING ====================
#define READY_TIMEOUT_MS 3000
#define ASCII_RESPONSE_TIMEOUT_MS 2000
#define SNAP_CAPTURE_TIMEOUT_MS 15000
#define SERIAL_CHUNK_TIMEOUT_MS 15000
#define PHOTO_TIMEOUT_MS 60000
#define PHOTO_MAX_RETRIES 3

// ==================== PARAMETER RANGES ====================
#define PHOTO_WIDTH_MIN 320
#define PHOTO_WIDTH_MAX 1600
#define PHOTO_HEIGHT_MIN 240
#define PHOTO_HEIGHT_MAX 1200
#define PHOTO_QUALITY_MIN 1
#define PHOTO_QUALITY_MAX 63

inline uint32_t calculateCRC32(const uint8_t *data, uint32_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (uint32_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
    }
  }
  return crc ^ 0xFFFFFFFF;
}
