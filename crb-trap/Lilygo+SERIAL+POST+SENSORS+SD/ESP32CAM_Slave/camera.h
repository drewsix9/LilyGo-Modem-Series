/**
 * @file      camera.h
 * @brief     Camera initialisation, capture, and flash LED control for the
 *            AI-Thinker ESP32-CAM slave.
 *            Internally uses espnow_slave.h to send error packets when capture fails.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "esp_camera.h"
#include <Arduino.h>

// ==================== AI-THINKER ESP32-CAM PIN MAP ====================
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// ==================== FLASH LED ====================
#define FLASH_LED_PIN 4

// ==================== GLOBAL CAMERA STATE ====================
extern bool cameraInitialized;

/**
 * @brief Reset the I2C SCCB bus before camera init to clear any lock-up state.
 *        Must be called before initCamera().
 */
void initializeSCCBBus();

/**
 * @brief Initialise the OV2640 (or compatible) image sensor.
 * @param frameSize    Target FRAMESIZE_* constant.
 * @param jpegQuality  JPEG quality (1–63; lower = better).
 * @return true on success, false on error.
 */
bool initCamera(framesize_t frameSize, uint8_t jpegQuality);

/**
 * @brief Capture a JPEG photo, optionally save to SD card, and stream to master via ESP-NOW.
 *        Controls the flash LED based on the ambient lux value.
 *        If SD save is requested and fails, system gracefully continues with ESP-NOW send.
 *        On capture failure, sends a PKT_ERROR packet to the master.
 * @param lux     Ambient light level for flash-brightness calculation.
 * @param width   Requested frame width (used to select framesize).
 * @param height  Requested frame height.
 * @param quality JPEG quality (1–63).
 * @param saveToDisk Optional: save JPEG to SD card before sending (default true).
 * @return true if the photo was captured and all packets sent successfully.
 */
bool captureAndSendPhoto(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality, bool saveToDisk = true);
