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

static const uint16_t PHOTO_NUM_MIN = 1;
static const uint16_t PHOTO_NUM_MAX = 9999;

static uint16_t readPhotoCounter() {
  uint8_t low = EEPROM.read(0);
  uint8_t high = EEPROM.read(1);

  // Fresh EEPROM state.
  if (low == 0xFF && high == 0xFF) {
    return PHOTO_NUM_MIN;
  }

  // Backward compatibility with legacy 1-byte storage.
  if (high == 0xFF) {
    uint16_t legacyNext = (uint16_t)low + 1;
    if (legacyNext < PHOTO_NUM_MIN || legacyNext > PHOTO_NUM_MAX) {
      return PHOTO_NUM_MIN;
    }
    return legacyNext;
  }

  uint16_t nextNum = (uint16_t)low | ((uint16_t)high << 8);
  if (nextNum < PHOTO_NUM_MIN || nextNum > PHOTO_NUM_MAX) {
    return PHOTO_NUM_MIN;
  }
  return nextNum;
}

static void writePhotoCounter(uint16_t nextNum) {
  EEPROM.write(0, (uint8_t)(nextNum & 0xFF));
  EEPROM.write(1, (uint8_t)((nextNum >> 8) & 0xFF));
  EEPROM.commit();
}

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

  uint16_t nextNum = readPhotoCounter();

  // Format: IMG_0001.jpg
  int written = snprintf(filename, maxLen, "IMG_%04u.jpg", (unsigned int)nextNum);
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
  uint16_t currentNum = readPhotoCounter();
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

  // Increment EEPROM counter after successful write.
  uint16_t nextNum = currentNum + 1;
  if (nextNum > PHOTO_NUM_MAX) {
    nextNum = PHOTO_NUM_MIN;
  }
  writePhotoCounter(nextNum);

  Serial.printf("[SD] Save: %s (%zu bytes) OK\n", filepath, len);
  return true;
}

// ==================== STATUS QUERY ====================

bool isSDAvailable() {
  return sdAvailable;
}
