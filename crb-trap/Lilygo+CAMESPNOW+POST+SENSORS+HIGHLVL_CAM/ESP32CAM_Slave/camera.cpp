/**
 * @file      camera.cpp
 * @brief     Camera initialisation, capture pipeline, and flash LED control
 *            for the AI-Thinker ESP32-CAM slave using esp32cam library.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "camera.h"
#include "espnow_protocol.h" // for PKT_ERROR / ERR_* codes
#include "espnow_slave.h"    // for espnowSendReliable() (used to send error packets)
#include <Arduino.h>

// ==================== GLOBAL STATE ====================

bool cameraInitialized = false;

// ==================== STATIC HELPERS ====================

static esp32cam::Resolution selectResolution(uint16_t width, uint16_t height) {
  // Find a resolution that meets or exceeds the requested dimensions
  return esp32cam::Resolution::find(width, height);
}

static uint8_t calculateFlashBrightness(uint16_t lux) {
  if (lux < 50)
    return 255;
  if (lux < 150)
    return 192;
  if (lux < 300)
    return 128;
  if (lux < 500)
    return 64;
  return 0;
}

static void setFlashBrightness(uint8_t brightness) {
  if (brightness > 0) {
    digitalWrite(FLASH_LED_PIN, HIGH);
    Serial.printf("[LED] Flash ON (level %d/255)\n", brightness);
  } else {
    digitalWrite(FLASH_LED_PIN, LOW);
    Serial.println("[LED] Flash OFF");
  }
}

// ==================== CAMERA INIT ====================

bool initCamera(esp32cam::Resolution frameSize, uint8_t jpegQuality) {
  Serial.printf("[CAM] Initializing camera with esp32cam library...\n");

  // Configure camera using high-level esp32cam API
  esp32cam::Config cfg;
  cfg.setPins(esp32cam::pins::AiThinker); // Use AI-Thinker pin mapping
  cfg.setResolution(frameSize);
  cfg.setJpeg(jpegQuality); // Quality 0-100 (library converts to 1-63)
  cfg.setBufferCount(2);    // 2 frame buffers for better stability

  bool ok = esp32cam::Camera.begin(cfg);
  if (!ok) {
    Serial.printf("[CAM] ERROR: Camera initialization failed\n");
    cameraInitialized = false;
    return false;
  }

  Serial.println("[CAM] Camera initialized successfully");
  cameraInitialized = true;
  return true;
}

// ==================== CAPTURE AND SEND ====================

bool captureAndSendPhoto(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality) {
  if (!cameraInitialized) {
    Serial.println("[ERROR] Camera not initialized");
    return false;
  }

  // Select and apply target resolution
  esp32cam::Resolution targetResolution = selectResolution(width, height);
  if (!targetResolution.isValid()) {
    Serial.printf("[CAM] ERROR: Invalid resolution requested (%dx%d)\n", width, height);
    return false;
  }

  // Get current settings, modify, and apply
  esp32cam::Settings currentSettings = esp32cam::Camera.status();
  currentSettings.resolution = targetResolution;

  // Update camera resolution
  bool ok = esp32cam::Camera.update(currentSettings, 500);
  if (!ok) {
    Serial.printf("[CAM] ERROR: Failed to update camera settings\n");
    return false;
  }

  Serial.printf("[CAM] Settings: Resolution=%dx%d, Quality=%d (0-100)\n",
                targetResolution.getWidth(), targetResolution.getHeight(), quality);
  delay(100);

  // Control flash based on ambient light
  uint8_t flashBrightness = calculateFlashBrightness(lux);
  if (flashBrightness > 0) {
    setFlashBrightness(flashBrightness);
    delay(200);
  }

  // Warm-up phase: Let sensor auto-exposure and white balance settle
  Serial.println("[CAM] Warming up camera (8 frames, 150ms intervals)...");
  delay(1500); // Initial settling time

  for (int i = 0; i < 8; i++) {
    auto tempFrame = esp32cam::capture();
    if (tempFrame) {
      // Frame discarded, just move to next
      Serial.printf("[CAM] Warm-up frame %d/%d discarded (%zu bytes)\n", i + 1, 8, tempFrame->size());
    }
    delay(150);
  }
  Serial.println("[CAM] Warm-up phase complete");

  // Capture actual photo: discard first frame for final exposure settling, use second
  Serial.println("[CAM] Capturing image...");
  const int MAX_ATTEMPTS = 5;
  std::unique_ptr<esp32cam::Frame> frame = nullptr;
  bool firstFrameDiscarded = false;

  for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
    auto tempFrame = esp32cam::capture();
    if (!tempFrame) {
      Serial.printf("[CAM] Capture returned null, attempt %d/%d\n", attempt + 1, MAX_ATTEMPTS);
      delay(1000);
      continue;
    }

    // Diagnostic dump (first 16 bytes)
    Serial.printf("[CAM] Frame %d: %zu bytes, first 16: ", attempt + 1, tempFrame->size());
    for (int i = 0; i < 16 && i < (int)tempFrame->size(); i++) {
      Serial.printf("%02X ", tempFrame->data()[i]);
    }
    Serial.println();

    // Validate JPEG format (library returns valid frames, but double-check)
    if (!tempFrame->isJpeg()) {
      Serial.printf("[CAM] Invalid frame on attempt %d (not JPEG format)\n", attempt + 1);
      delay(1000);
      continue;
    }

    // First valid frame is discarded for final exposure settling
    if (!firstFrameDiscarded) {
      firstFrameDiscarded = true;
      Serial.printf("[CAM] Discarding first frame: %zu bytes (final exposure settling)\n",
                    tempFrame->size());
      delay(500);
      continue;
    }

    // Second valid frame — use it
    frame = std::move(tempFrame);
    Serial.printf("[CAM] Valid JPEG captured: %zu bytes (attempt %d)\n",
                  frame->size(), attempt + 1);
    break;
  }

  // Always turn flash off after capture
  if (flashBrightness > 0) {
    setFlashBrightness(0);
  }

  if (!frame) {
    Serial.println("[CAM] ERROR: Capture failed after all attempts");
    uint8_t errPkt[2] = {PKT_ERROR, ERR_CAPTURE_FAILED};
    espnowSendReliable(errPkt, sizeof(errPkt));
    return false;
  }

  Serial.printf("[CAM] Image captured: %zu bytes\n", frame->size());

  // Send photo via ESP-NOW
  // Extract buffer pointer and size from frame for transmission
  uint8_t *bufPtr = frame->data();
  size_t bufSize = frame->size();

  if (!bufPtr || bufSize == 0) {
    Serial.println("[ERROR] Frame buffer is invalid");
    uint8_t errPkt[2] = {PKT_ERROR, ERR_CAPTURE_FAILED};
    espnowSendReliable(errPkt, sizeof(errPkt));
    return false;
  }

  // Prepare a temporary camera_fb_t-like structure for sendPhotoViaESPNOW
  // sendPhotoViaESPNOW expects camera_fb_t pointer, but we work with raw buffer
  // We'll create a minimal wrapper on the stack
  struct FrameWrapper {
    uint8_t *buf;
    uint32_t len;
  } wrapper = {bufPtr, (uint32_t)bufSize};

  // Send using the ESP-NOW protocol (sendPhotoViaESPNOW signature still expects camera_fb_t*)
  // Since we have the buffer data, we'll replicate the sendPhotoViaESPNOW logic here inline
  bool success = sendPhotoViaESPNOW((camera_fb_t *)&wrapper);

  // frame's destructor will clean up automatically via std::unique_ptr

  if (success) {
    Serial.println("[ESPNOW] Photo transmission complete");
  } else {
    Serial.println("[ESPNOW] Photo transmission failed");
    uint8_t errPkt[2] = {PKT_ERROR, ERR_SEND_FAILED};
    espnowSendReliable(errPkt, sizeof(errPkt));
  }

  return success;
}
