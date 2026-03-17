/**
 * @file      camera.h
 * @brief     Camera initialisation, capture, and flash LED control for the
 *            AI-Thinker ESP32-CAM slave using esp32cam high-level abstraction library.
 *            Internally uses espnow_slave.h to send error packets when capture fails.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>
#include <esp32cam.h>

// ==================== FLASH LED ====================
#define FLASH_LED_PIN 4

// ==================== GLOBAL CAMERA STATE ====================
extern bool cameraInitialized;

/**
 * @brief Initialize the OV2640 (or compatible) image sensor using esp32cam library.
 * @param frameSize    Target resolution (e.g., 640x480, 800x600, 1024x768, 1280x1024).
 * @param jpegQuality  JPEG quality (0–100; higher = better). Library converts to internal 1-63 scale.
 * @return true on success, false on error.
 */
bool initCamera(esp32cam::Resolution frameSize, uint8_t jpegQuality);

/**
 * @brief Capture a JPEG photo and stream it to the master via ESP-NOW.
 *        Controls the flash LED based on the ambient lux value.
 *        On failure, sends a PKT_ERROR packet to the master.
 * @param lux     Ambient light level for flash-brightness calculation.
 * @param width   Requested frame width (used to select resolution).
 * @param height  Requested frame height.
 * @param quality JPEG quality (0–100; higher = better).
 * @return true if the photo was captured and all packets sent successfully.
 */
bool captureAndSendPhoto(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality);
