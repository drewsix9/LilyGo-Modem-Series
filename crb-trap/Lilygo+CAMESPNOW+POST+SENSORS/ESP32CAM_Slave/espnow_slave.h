/**
 * @file      espnow_slave.h
 * @brief     ESP-NOW slave-side communication layer.
 *            WiFi scan for master AP, pairing, reliable send with retry,
 *            READY signal, and full photo streaming.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "esp_camera.h"
#include <Arduino.h>
#include <esp_now.h>

// ==================== SLAVE TIMING / RETRY CONFIG ====================
#define MAX_SEND_RETRIES 5   // Retries per packet on delivery failure
#define SCAN_MAX_RETRIES 10  // WiFi scan attempts to locate master AP
#define SEND_TIMEOUT_MS 2000 // Callback wait timeout per send attempt (ms)

// ==================== EXTERNAL STATE ====================
// Defined in espnow_slave.cpp; read/written by the main loop and camera module.
extern uint8_t masterMac[6];
extern bool isPaired;

extern volatile bool espnowSendDone;
extern volatile bool espnowSendSuccess;
extern volatile bool photoRequested;
extern volatile uint16_t cmdLux;
extern volatile uint16_t cmdWidth;
extern volatile uint16_t cmdHeight;
extern volatile uint8_t cmdQuality;

// ==================== API ====================

/**
 * @brief Send data via ESP-NOW with blocking wait and exponential-backoff retry.
 * @param data Pointer to the packet buffer.
 * @param len  Number of bytes to send (max 250).
 * @return true if the packet was delivered successfully within MAX_SEND_RETRIES.
 */
bool espnowSendReliable(const uint8_t *data, size_t len);

/**
 * @brief Scan WiFi networks and pair with the master AP (MASTER_AP_SSID).
 *        Extracts the BSSID from the scan result and adds it as an ESP-NOW peer.
 * @return true if pairing succeeded, false after SCAN_MAX_RETRIES failures.
 */
bool scanAndPairWithMaster();

/**
 * @brief Send a PKT_READY packet to the master to signal boot completion.
 * @return true if the packet was delivered.
 */
bool sendReadySignal();

/**
 * @brief Stream a complete JPEG frame to the master via ESP-NOW.
 *        Sends PHOTO_START, all PHOTO_DATA chunks, then PHOTO_END with CRC32.
 * @param fb Camera frame buffer to transmit.
 * @return true if all packets were delivered successfully.
 */
bool sendPhotoViaESPNOW(camera_fb_t *fb);
