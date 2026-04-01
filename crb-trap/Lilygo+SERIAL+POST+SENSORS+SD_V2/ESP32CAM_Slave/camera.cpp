/**
 * @file      camera.cpp
 * @brief     Camera initialisation, capture pipeline, and flash LED control
 *            for the AI-Thinker ESP32-CAM slave.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "camera.h"
#include "espnow_protocol.h" // for PKT_ERROR / ERR_* codes
#include "espnow_slave.h"    // for espnowSendReliable() (used to send error packets)
#include "sdcard.h"          // for SD card photo persistence
#include <Arduino.h>

// ==================== GLOBAL STATE ====================

bool cameraInitialized = false;

// ==================== STATIC HELPERS ====================

static framesize_t selectFrameSize(uint16_t width, uint16_t height) {
  if (width >= 1280 || height >= 1024)
    return FRAMESIZE_SXGA;
  if (width >= 1024 || height >= 768)
    return FRAMESIZE_XGA;
  if (width >= 800 || height >= 600)
    return FRAMESIZE_SVGA;
  return FRAMESIZE_VGA;
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

// ==================== SCCB BUS RESET ====================

void initializeSCCBBus() {
  Serial.println("[CAM] Initializing I2C SCCB bus...");

  pinMode(SIOD_GPIO_NUM, OUTPUT);
  pinMode(SIOC_GPIO_NUM, OUTPUT);
  digitalWrite(SIOD_GPIO_NUM, HIGH);
  digitalWrite(SIOC_GPIO_NUM, HIGH);
  delay(10);

  // 9 clock pulses to release any stuck I2C device
  for (int i = 0; i < 9; i++) {
    digitalWrite(SIOC_GPIO_NUM, LOW);
    delay(2);
    digitalWrite(SIOC_GPIO_NUM, HIGH);
    delay(2);
  }

  // STOP condition
  digitalWrite(SIOD_GPIO_NUM, LOW);
  delay(2);
  digitalWrite(SIOD_GPIO_NUM, HIGH);
  delay(2);

  Serial.println("[CAM] I2C bus reset complete");
}

// ==================== CAMERA INIT ====================

bool initCamera(framesize_t frameSize, uint8_t jpegQuality) {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = frameSize;
    config.jpeg_quality = jpegQuality;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }

  config.grab_mode = CAMERA_GRAB_LATEST;

  Serial.printf("[CAM] Initializing camera (size=%d, quality=%d, xclk=20MHz)...\n",
                frameSize, jpegQuality);

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] ERROR: Camera init failed 0x%x\n", err);
    cameraInitialized = false;
    return false;
  }

  Serial.println("[CAM] Camera initialized successfully");
  cameraInitialized = true;
  return true;
}

// ==================== CAPTURE AND SEND ====================

bool captureAndSendPhoto(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality, bool saveToDisk) {
  if (!cameraInitialized) {
    Serial.println("[ERROR] Camera not initialized");
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (!s) {
    Serial.println("[CAM] ERROR: Failed to get sensor");
    return false;
  }

  // Apply requested resolution and quality
  framesize_t targetSize = selectFrameSize(width, height);
  s->set_framesize(s, targetSize);
  s->set_quality(s, quality);
  Serial.printf("[CAM] Settings: Size=%dx%d, Quality=%d\n", width, height, quality);
  delay(500);

  // Control flash based on ambient light
  uint8_t flashBrightness = calculateFlashBrightness(lux);
  if (flashBrightness > 0) {
    setFlashBrightness(flashBrightness);
    delay(200);
  }

  // Capture with warm-up frame strategy
  Serial.println("[CAM] Capturing image...");
  camera_fb_t *fb = nullptr;
  const int MAX_ATTEMPTS = 5;
  bool warmupDone = false;

  for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
    camera_fb_t *temp = esp_camera_fb_get();
    if (!temp) {
      Serial.printf("[CAM] Capture returned null, attempt %d/%d\n", attempt + 1, MAX_ATTEMPTS);
      delay(1000);
      continue;
    }

    // Diagnostic dump
    Serial.printf("[CAM] Frame %d: %u bytes, first 16: ", attempt + 1, temp->len);
    for (int i = 0; i < 16 && i < (int)temp->len; i++) {
      Serial.printf("%02X ", temp->buf[i]);
    }
    Serial.println();

    // Validate JPEG SOI marker (FF D8 FF) at offset 0
    bool isValidJpeg = (temp->len >= 3 &&
                        temp->buf[0] == 0xFF &&
                        temp->buf[1] == 0xD8 &&
                        temp->buf[2] == 0xFF);

    // Scan for SOI if not at offset 0 (handles DMA padding)
    uint32_t soiOffset = 0;
    if (!isValidJpeg && temp->len > 3) {
      uint32_t scanLimit = temp->len - 2;
      for (uint32_t i = 1; i < scanLimit; i++) {
        if (temp->buf[i] == 0xFF && temp->buf[i + 1] == 0xD8 && temp->buf[i + 2] == 0xFF) {
          soiOffset = i;
          isValidJpeg = true;
          Serial.printf("[CAM] JPEG SOI found at offset %u\n", soiOffset);
          break;
        }
      }
    }

    if (!isValidJpeg) {
      Serial.printf("[CAM] Invalid frame on attempt %d (%u bytes, no JPEG SOI)\n",
                    attempt + 1, temp->len);
      esp_camera_fb_return(temp);
      delay(1000);
      continue;
    }

    // First valid frame is a warm-up (discard for auto-exposure settling)
    if (!warmupDone) {
      warmupDone = true;
      Serial.printf("[CAM] Warm-up frame: %u bytes (discarded for auto-exposure)\n", temp->len);
      esp_camera_fb_return(temp);
      delay(1000);
      continue;
    }

    // Second valid frame — use it
    if (soiOffset > 0) {
      temp->buf += soiOffset;
      temp->len -= soiOffset;
      Serial.printf("[CAM] Adjusted buffer: skipped %u bytes of DMA padding\n", soiOffset);
    }
    fb = temp;
    Serial.printf("[CAM] Valid JPEG captured: %u bytes (attempt %d)\n",
                  fb->len, attempt + 1);
    break;
  }

  // Always turn flash off after capture
  if (flashBrightness > 0) {
    setFlashBrightness(0);
  }

  if (!fb) {
    Serial.println("[CAM] ERROR: Capture failed after all attempts");
    uint8_t errPkt[2] = {PKT_ERROR, ERR_CAPTURE_FAILED};
    espnowSendReliable(errPkt, sizeof(errPkt));
    return false;
  }

  Serial.printf("[CAM] Image captured: %u bytes\n", fb->len);

  // Optionally save photo to SD card before sending via ESP-NOW
  if (saveToDisk) {
    if (isSDAvailable()) {
      bool sdSaveSuccess = savePhotoToSD(fb->buf, fb->len);
      if (!sdSaveSuccess) {
        Serial.println("[SD] WARN: Photo save to SD failed, continuing with ESP-NOW send");
      }
    } else {
      Serial.println("[SD] SD card not available, skipping archive (ESP-NOW send only)");
    }
  }

  bool success = sendPhotoViaESPNOW(fb);
  esp_camera_fb_return(fb);

  if (success) {
    Serial.println("[ESPNOW] Photo transmission complete");
  } else {
    Serial.println("[ESPNOW] Photo transmission failed");
    uint8_t errPkt[2] = {PKT_ERROR, ERR_SEND_FAILED};
    espnowSendReliable(errPkt, sizeof(errPkt));
  }

  return success;
}
