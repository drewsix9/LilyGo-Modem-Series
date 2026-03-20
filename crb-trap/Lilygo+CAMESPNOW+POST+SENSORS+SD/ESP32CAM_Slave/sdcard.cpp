/**
 * @file      sdcard.cpp
 * @brief     SD card abstraction implementation for ESP32-CAM slave.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "sdcard.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

// ==================== GLOBAL STATE ====================
bool sdAvailable = false;

// ==================== SD CARD INITIALIZATION ====================

bool initSDCard() {
  Serial.println("[SD] Initializing SD card in 1-bit SPI mode...");

  // Configure SPI for 1-bit mode (CS, CLK, MOSI, MISO)
  // 1-bit mode reduces pin count and interference with camera/WiFi
  SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

  // Attempt to initialize SD card
  if (!SD.begin(SD_CS_PIN, SPI, 27000000, "/ps", 1, false)) {
    Serial.println("[SD] WARN: Failed to initialize SD card (may not be present)");
    Serial.printf("[SD]   CS=%d, CLK=%d, MOSI=%d, MISO=%d (1-bit mode)\n",
                  SD_CS_PIN, SD_CLK_PIN, SD_MOSI_PIN, SD_MISO_PIN);
    sdAvailable = false;
    return false;
  }

  Serial.println("[SD] SD card initialized successfully");

  // Log SD card info
  uint8_t cardType = SD.cardType();
  Serial.printf("[SD] Card type: ");
  if (cardType == CARD_NONE) {
    Serial.println("NONE");
    sdAvailable = false;
    return false;
  } else if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  // Log cardinformation
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t usedSize = SD.usedBytes() / (1024 * 1024);
  uint64_t totalSize = SD.totalBytes() / (1024 * 1024);
  Serial.printf("[SD] Card size: %llu MB, Used: %llu MB, Free: %llu MB\n",
                cardSize, usedSize, totalSize - usedSize);

  // Create directory structure if missing
  if (!SD.exists("/crb-trap")) {
    Serial.println("[SD] Creating /crb-trap directory...");
    if (!SD.mkdir("/crb-trap")) {
      Serial.println("[SD] WARN: Failed to create /crb-trap directory");
      // Don't fail — may already exist or SD driver limitation
    }
  }

  if (!SD.exists("/crb-trap/TRAP-001")) {
    Serial.println("[SD] Creating /crb-trap/TRAP-001 directory...");
    if (!SD.mkdir("/crb-trap/TRAP-001")) {
      Serial.println("[SD] WARN: Failed to create /crb-trap/TRAP-001 directory");
      // Don't fail — may already exist or SD driver limitation
    }
  }

  sdAvailable = true;
  Serial.println("[SD] SD card ready for photos");
  return true;
}

// ==================== FILENAME MANAGEMENT ====================

bool getNextFilename(char *filename, size_t maxLen) {
  if (!filename || maxLen < 16) {
    Serial.println("[SD] ERROR: Filename buffer too small (min 16 bytes)");
    return false;
  }

  // Initialize EEPROM if not already done (caller handles EEPROM.begin())
  uint8_t stored = EEPROM.read(0);
  uint8_t nextNum = (stored == 255) ? 1 : (uint8_t)(stored + 1);

  // Wrap counter at 9999 (0 means 10000, which wraps to 1)
  if (nextNum == 0) {
    nextNum = 1;
  }

  // Format: IMG_0001.jpg
  int written = snprintf(filename, maxLen, "IMG_%04d.jpg", nextNum);
  if (written < 0 || (size_t)written >= maxLen) {
    Serial.println("[SD] ERROR: Filename format failed");
    return false;
  }

  return true;
}

// ==================== PHOTO PERSISTENCE ====================

bool savePhotoToSD(const uint8_t *buffer, size_t len) {
  if (!sdAvailable) {
    Serial.println("[SD] SD card not available, skipping save");
    return false;
  }

  if (!buffer || len == 0) {
    Serial.println("[SD] ERROR: Invalid buffer or size");
    return false;
  }

  // Get next filename
  char filename[64];
  if (!getNextFilename(filename, sizeof(filename))) {
    Serial.println("[SD] ERROR: Failed to generate filename");
    return false;
  }

  // Build full path
  char filepath[128];
  int pathLen = snprintf(filepath, sizeof(filepath), "/crb-trap/TRAP-001/%s", filename);
  if (pathLen < 0 || (size_t)pathLen >= sizeof(filepath)) {
    Serial.println("[SD] ERROR: Filepath too long");
    return false;
  }

  // Open file for writing
  File file = SD.open(filepath, FILE_WRITE);
  if (!file) {
    Serial.printf("[SD] ERROR: Failed to open file: %s\n", filepath);
    return false;
  }

  // Write JPEG data
  size_t written = file.write(buffer, len);
  file.close();

  if (written != len) {
    Serial.printf("[SD] ERROR: Write incomplete: %zu/%zu bytes\n", written, len);
    return false;
  }

  // Increment EEPROM counter after successful write
  uint8_t stored = EEPROM.read(0);
  uint8_t nextNum = (stored == 255) ? 1 : (uint8_t)(stored + 1);
  if (nextNum == 0) {
    nextNum = 1; // Wrap to 1 (9999 -> 0 -> 1)
  }
  EEPROM.write(0, nextNum - 1); // Store current, next iteration reads and increments
  EEPROM.commit();

  Serial.printf("[SD] Save: %s (%zu bytes) OK\n", filepath, len);
  return true;
}

// ==================== STATUS QUERY ====================

bool isSDAvailable() {
  return sdAvailable;
}
