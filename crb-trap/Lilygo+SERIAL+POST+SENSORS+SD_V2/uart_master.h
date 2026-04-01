/**
 * @file      uart_master.h
 * @brief     UART hybrid master-side communication layer.
 *            Uses ASCII control messages and SerialTransfer binary JPEG chunks.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>

// ==================== EXTERNAL STATE ====================
extern volatile bool camReady;
extern volatile bool photoStartReceived;
extern volatile bool photoEndReceived;
extern volatile bool slaveError;
extern volatile uint8_t slaveErrorCode;
extern volatile bool transferAborted;

extern uint8_t *photoBuffer;
extern volatile uint32_t photoSize;
extern volatile uint16_t totalPacketsExpected;
extern volatile uint16_t packetsReceived;
extern volatile uint32_t receivedCRC32;
extern volatile uint32_t lastPacketTime;
extern volatile uint16_t lastAckedPacket;

extern uint16_t capturedLuxValue;
extern bool capturedIsFallen;

// ==================== API ====================

/**
 * @brief Initialize UART transport and disable WiFi/Bluetooth for low power.
 *        Resets reception state and frees leftover photo buffers.
 */
void initESPNOW();

/**
 * @brief Deinitialize UART transfer state and free photo buffer.
 *        WiFi and Bluetooth remain disabled.
 */
void cleanupESPNOW();

/**
 * @brief Block until slave sends READY line (or timeout).
 * @return true if READY received in time, false on timeout.
 */
bool waitForCameraReady();

/**
 * @brief Send SNAP command to slave.
 * @param lux     Ambient light reading forwarded to slave for flash control.
 * @param width   Requested capture width in pixels.
 * @param height  Requested capture height in pixels.
 * @param quality JPEG quality (1–63, lower = better quality).
 * @return true if command write succeeded.
 */
bool sendPhotoCommand(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality);

/**
 * @brief Wait for SIZE response, send SEND, receive JPEG chunk stream,
 *        validate CRC32 and hand off for upload.
 * @return true if the photo was received completely and CRC32 passed.
 */
bool receivePhoto();
