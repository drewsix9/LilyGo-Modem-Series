/**
 * @file      espnow_master.h
 * @brief     ESP-NOW master-side communication layer.
 *            Manages WiFi AP startup, ESP-NOW init/deinit, slave pairing,
 *            PHOTO_CMD transmission, and complete photo reception with CRC32 verification.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// ==================== MASTER AP CREDENTIALS ====================
#define MASTER_AP_PASS "12345678"

// ==================== TIMING ====================
#define READY_TIMEOUT_MS 15000 // Max wait for slave READY signal (ms)
#define PHOTO_TIMEOUT_MS 60000 // Max wait for complete photo transfer (ms)
#define PHOTO_MAX_RETRIES 3    // Number of photo capture retries on failure

// ==================== EXTERNAL STATE ====================
// These variables are defined in espnow_master.cpp and exposed for the main .ino
// to poll between setup() phases where needed.
extern volatile bool camReady;
extern volatile bool photoStartReceived;
extern volatile bool photoEndReceived;
extern volatile bool slaveError;
extern volatile uint8_t slaveErrorCode;

extern uint8_t *photoBuffer;
extern volatile uint32_t photoSize;
extern volatile uint16_t totalPacketsExpected;
extern volatile uint16_t packetsReceived;
extern volatile uint32_t receivedCRC32;
extern volatile uint32_t lastPacketTime;

extern uint8_t slaveMac[6];
extern bool slavePaired;

extern uint16_t capturedLuxValue;
extern bool capturedIsFallen;

// ==================== API ====================

/**
 * @brief Start WiFi in AP mode (so the slave can scan and find us),
 *        initialise ESP-NOW, and register send/receive callbacks.
 *        Resets all reception state and frees any leftover photo buffer.
 */
void initESPNOW();

/**
 * @brief Deinitialise ESP-NOW, stop the AP, disable WiFi, and free photo buffer.
 */
void cleanupESPNOW();

/**
 * @brief Block until the slave sends a READY packet (or timeout expires).
 *        On success the slave is added as an ESP-NOW peer using the AP interface.
 * @return true if READY received in time, false on timeout.
 */
bool waitForCameraReady();

/**
 * @brief Send a PHOTO_CMD packet to the paired slave.
 * @param lux     Ambient light reading forwarded to slave for flash control.
 * @param width   Requested capture width in pixels.
 * @param height  Requested capture height in pixels.
 * @param quality JPEG quality (1–63, lower = better quality).
 * @return true if the packet was handed off to ESP-NOW successfully.
 */
bool sendPhotoCommand(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality);

/**
 * @brief Wait for the slave to stream PHOTO_START / PHOTO_DATA / PHOTO_END packets,
 *        assemble them into photoBuffer, and verify the CRC32.
 * @return true if the photo was received completely and CRC32 passed.
 */
bool receivePhoto();
