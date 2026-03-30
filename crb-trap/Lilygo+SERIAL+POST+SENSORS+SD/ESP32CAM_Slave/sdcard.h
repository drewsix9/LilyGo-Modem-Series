/**
 * @file      sdcard.h
 * @brief     SD card abstraction layer for ESP32-CAM slave.
 *            Handles SD initialization, photo persistence, and filename management.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

// ==================== SD CARD CONFIGURATION ====================
// 1-bit mode recommended to reduce interference with camera/WiFi on power-constrained ESP32-CAM

// SPI pin definitions for SD card (1-bit mode)
#define SD_CS_PIN 13   // Chip Select
#define SD_MOSI_PIN 15 // Data In (MOSI)
#define SD_MISO_PIN 2  // Data Out (MISO)
#define SD_CLK_PIN 14  // Clock

// ==================== GLOBAL SD STATE ====================
extern bool sdAvailable;

/**
 * @brief Initialize SD card in 1-bit SPI mode and mount filesystem.
 *        Creates /crb-trap/TRAP-001/ directory if it does not exist.
 *        Gracefully degrades if SD card is absent or unreadable.
 * @return true if SD card initialized and mounted successfully,
 *         false if SD is unavailable (system continues without archive).
 */
bool initSDCard();

/**
 * @brief Save a JPEG photo buffer to SD card with sequential filename.
 *        Automatically increments EEPROM picture number after successful write.
 *        Gracefully handles write failures (returns false, system continues).
 * @param buffer  Pointer to JPEG data (typically from camera_fb_t->buf).
 * @param len     Size of JPEG data in bytes.
 * @return true if photo successfully written to SD, false on write error.
 */
bool savePhotoToSD(const uint8_t *buffer, size_t len);

/**
 * @brief Get the next sequential filename without incrementing counter.
 *        Returns format: IMG_0001.jpg, IMG_0002.jpg, ... IMG_9999.jpg (then wraps).
 *        Must call savePhotoToSD() to increment the counter (not this function alone).
 * @param filename Output buffer to store filename (min 16 bytes: "IMG_NNNN.jpg\0").
 * @param maxLen   Size of filename buffer.
 * @return true if filename generated successfully, false on error.
 */
bool getNextFilename(char *filename, size_t maxLen);

/**
 * @brief Query whether SD card is available (initialized and mounted).
 * @return true if initialized, false if absent or init failed.
 */
bool isSDAvailable();
