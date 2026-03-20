/**
 * @file      espnow_protocol.h
 * @brief     Shared ESP-NOW protocol definitions for master and slave.
 *            Packet type constants, channel/SSID config, data size, and CRC32.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

// ==================== ESP-NOW CONFIGURATION ====================
#define ESPNOW_CHANNEL 1
#define MASTER_AP_SSID "CRBMaster"

// ============ ESP-NOW Packet Size Configuration ============
// CURRENT: v1.0 (240 bytes) for compatibility with ESP32-CAM slave
// REASON: ESP32-CAM doesn't support v2.0 (error 0x3066 on >250B packets)
// FUTURE: Can upgrade to v2.0 (1470B) when using ESP32-S3 CAM or newer boards with ESP-IDF 5.0+
// v2.0 would give: 5.7x throughput improvement (63 pkt -> 11 pkt for 15KB JPEG)
#define ESPNOW_DATA_SIZE 240 // v1.0: 240-byte packets (stable, proven on ESP32-CAM)

// ==================== PACKET TYPES ====================
// First byte of every ESP-NOW packet identifies its type.
#define PKT_READY 0x00       // slave  -> master  payload: "READY" (5 bytes)
#define PKT_PHOTO_CMD 0x01   // master -> slave   payload: lux(2)+w(2)+h(2)+q(1) = 7 bytes
#define PKT_PHOTO_START 0x10 // slave  -> master  payload: totalSize(4)+totalPackets(2) = 6 bytes
#define PKT_PHOTO_DATA 0x20  // slave  -> master  payload: packetNum(2)+data(<=240)
#define PKT_PHOTO_END 0x30   // slave  -> master  payload: CRC32(4) = 4 bytes
#define PKT_NEXT 0x40        // master -> slave   payload: ackCounter(2) = 2 bytes
#define PKT_ERROR 0xF0       // slave  -> master  payload: error code(1)

// ==================== ERROR CODES ====================
#define ERR_CAPTURE_FAILED 0x01
#define ERR_SEND_FAILED 0x02
#define ERR_INVALID_PARAMS 0x03
#define ERR_SEQ_ERROR 0x04 // Packet out of order or sequence mismatch

// ==================== HANDSHAKE TIMING ====================
#define SLAVE_SEND_WAIT_MS 1500 // Slave timeout waiting for PKT_NEXT from master (ms)

// ==================== CRC32 ====================
/**
 * @brief Calculate standard CRC32 (IEEE 802.3 polynomial 0xEDB88320).
 *        Declared inline to prevent multiple-definition linker errors when
 *        included in both master and slave translation units.
 */
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
