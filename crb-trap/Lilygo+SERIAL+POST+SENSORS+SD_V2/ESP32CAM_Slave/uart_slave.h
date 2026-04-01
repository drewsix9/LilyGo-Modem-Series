/**
 * @file      uart_slave.h
 * @brief     UART hybrid slave-side communication layer.
 *            ASCII command parser + SerialTransfer binary photo stream.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "esp_camera.h"
#include <Arduino.h>

// ==================== SLAVE TIMING / RETRY CONFIG ====================
#define MAX_SEND_RETRIES 3

// ==================== EXTERNAL STATE ====================
extern volatile bool photoRequested;
extern volatile uint16_t cmdLux;
extern volatile uint16_t cmdWidth;
extern volatile uint16_t cmdHeight;
extern volatile uint8_t cmdQuality;

// ==================== API ====================

/**
 * @brief Poll UART and parse incoming SNAP command lines.
 *        Sets photoRequested/cmd* globals when a valid command is received.
 */
void pollSerialCommands();

/**
 * @brief Initialize UART + SerialTransfer transport.
 */
bool scanAndPairWithMaster();

/**
 * @brief Send READY line to master.
 */
bool sendReadySignal();

/**
 * @brief Send ERROR:code line to master.
 */
bool sendErrorMessage(uint8_t errorCode);

/**
 * @brief Stream JPEG to master using SIZE/SEND ASCII handover then binary chunks.
 * @param fb Camera frame buffer.
 * @return true if all packets were delivered successfully.
 */
bool sendPhotoViaESPNOW(camera_fb_t *fb);

/**
 * @brief Compatibility helper used by camera.cpp error paths.
 */
bool espnowSendReliable(const uint8_t *data, size_t len);
