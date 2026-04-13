/**
 * @file      ESP32CAM_DATA_GATHERING.ino
 * @brief     One-shot data gatherer for AI-Thinker ESP32-CAM.
 *            On every reset: capture one JPEG and save to SD card.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_camera.h>

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
#define FLASH_LED_PIN 4

// ==================== SD CARD PINS (same as ESP32CAM_Slave) ====================
#define SD_CS_PIN 13
#define SD_MOSI_PIN 15
#define SD_MISO_PIN 2
#define SD_CLK_PIN 14

// ==================== SETTINGS (match ESP32CAM_Slave) ====================
#define EEPROM_SIZE 2
static const uint16_t PHOTO_NUM_MIN = 1;
static const uint16_t PHOTO_NUM_MAX = 65535;

static uint16_t readPhotoCounter() {
  uint8_t low = EEPROM.read(0);
  uint8_t high = EEPROM.read(1);

  if (low == 0xFF && high == 0xFF) {
    return PHOTO_NUM_MIN;
  }

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

static bool initSDCard() {
  Serial.println("[SD] Initializing SD card in 1-bit SPI mode...");
  SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

  if (!SD.begin(SD_CS_PIN, SPI, 27000000, "/ps", 1, false)) {
    Serial.println("[SD] ERROR: SD card init failed");
    return false;
  }

  if (!SD.exists("/crb-trap")) {
    SD.mkdir("/crb-trap");
  }
  if (!SD.exists("/crb-trap/data-gathering")) {
    SD.mkdir("/crb-trap/data-gathering");
  }

  Serial.println("[SD] SD card ready");
  return true;
}

static void initializeSCCBBus() {
  Serial.println("[CAM] Initializing SCCB bus...");

  pinMode(SIOD_GPIO_NUM, OUTPUT);
  pinMode(SIOC_GPIO_NUM, OUTPUT);
  digitalWrite(SIOD_GPIO_NUM, HIGH);
  digitalWrite(SIOC_GPIO_NUM, HIGH);
  delay(10);

  for (int i = 0; i < 9; i++) {
    digitalWrite(SIOC_GPIO_NUM, LOW);
    delay(2);
    digitalWrite(SIOC_GPIO_NUM, HIGH);
    delay(2);
  }

  digitalWrite(SIOD_GPIO_NUM, LOW);
  delay(2);
  digitalWrite(SIOD_GPIO_NUM, HIGH);
  delay(2);

  Serial.println("[CAM] SCCB bus reset complete");
}

static bool initCameraSameAsSlave() {
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
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
  }

  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] ERROR: Camera init failed 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_wb_mode(s, 1); // Force auto white balance on to match slave default (and avoid blue tint in some lighting conditions).
  s->set_vflip(s, 1);   // 1 = enable vertical flip, 0 = disabled
  s->set_hmirror(s, 1); // 1 = enable horizontal mirror, 0 = disabled

  // BRIGHTNESS (-2 to 2)
  s->set_brightness(s, 0);
  // CONTRAST (-2 to 2)
  s->set_contrast(s, 0);
  // SATURATION (-2 to 2)
  s->set_saturation(s, 0);
  // SPECIAL EFFECTS (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  s->set_special_effect(s, 0);
  // WHITE BALANCE (0 = Disable , 1 = Enable)
  s->set_whitebal(s, 1);
  // AWB GAIN (0 = Disable , 1 = Enable)
  s->set_awb_gain(s, 1);
  // WB MODES (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  s->set_wb_mode(s, 0);
  // EXPOSURE CONTROLS (0 = Disable , 1 = Enable)
  s->set_exposure_ctrl(s, 1);
  // AEC2 (0 = Disable , 1 = Enable)
  s->set_aec2(s, 0);
  // AE LEVELS (-2 to 2)
  s->set_ae_level(s, 0);
  // AEC VALUES (0 to 1200)
  s->set_aec_value(s, 300);
  // GAIN CONTROLS (0 = Disable , 1 = Enable)
  s->set_gain_ctrl(s, 1);
  // AGC GAIN (0 to 30)
  s->set_agc_gain(s, 0);
  // GAIN CEILING (0 to 6)
  s->set_gainceiling(s, (gainceiling_t)0);
  // BPC (0 = Disable , 1 = Enable)
  s->set_bpc(s, 0);
  // WPC (0 = Disable , 1 = Enable)
  s->set_wpc(s, 1);
  // RAW GMA (0 = Disable , 1 = Enable)
  s->set_raw_gma(s, 1);
  // LENC (0 = Disable , 1 = Enable)
  s->set_lenc(s, 1);
  // HORIZ MIRROR (0 = Disable , 1 = Enable)
  // s->set_hmirror(s, 0);
  // VERT FLIP (0 = Disable , 1 = Enable)
  // s->set_vflip(s, 0);
  // DCW (0 = Disable , 1 = Enable)
  s->set_dcw(s, 1);
  // COLOR BAR PATTERN (0 = Disable , 1 = Enable)
  s->set_colorbar(s, 0);

  if (s) {
    Serial.printf("[CAM] Sensor PID=0x%04X\n", s->id.PID);
  }

  Serial.println("[CAM] Camera initialized (VGA, JPEG quality 30)");
  return true;
}

static bool buildNextFilePath(char *path, size_t pathLen, uint16_t currentNum) {
  if (!path || pathLen == 0) {
    return false;
  }

  int written = snprintf(path, pathLen, "/crb-trap/data-gathering/IMG_%04u.jpg", (unsigned int)currentNum);
  if (written < 0 || (size_t)written >= pathLen) {
    return false;
  }
  return true;
}

static void enableCaptureFlash() {
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(120);
}

static void disableCaptureFlash() {
  digitalWrite(FLASH_LED_PIN, LOW);
  // Release GPIO4 after flash use because it conflicts with SD-card wiring.
  pinMode(FLASH_LED_PIN, INPUT);
}

static camera_fb_t *captureFrameWithWarmup() {
  // Mirror slave behavior: discard first valid frame for AE settle, keep next valid frame.
  bool warmupDone = false;
  const int maxAttempts = 5;

  enableCaptureFlash();

  for (int attempt = 0; attempt < maxAttempts; attempt++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.printf("[CAM] Capture NULL, attempt %d/%d\n", attempt + 1, maxAttempts);
      delay(300);
      continue;
    }

    bool isValidJpeg = (fb->len >= 3 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8 && fb->buf[2] == 0xFF);
    if (!isValidJpeg) {
      Serial.printf("[CAM] Invalid frame (no SOI), attempt %d/%d\n", attempt + 1, maxAttempts);
      esp_camera_fb_return(fb);
      delay(300);
      continue;
    }

    if (!warmupDone) {
      warmupDone = true;
      Serial.printf("[CAM] Warm-up frame %u bytes discarded\n", fb->len);
      esp_camera_fb_return(fb);
      delay(300);
      continue;
    }

    Serial.printf("[CAM] Capture OK: %u bytes\n", fb->len);
    disableCaptureFlash();
    return fb;
  }

  disableCaptureFlash();
  return nullptr;
}

static bool didReceiveEnterPress() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      // Consume any immediate trailing CR/LF characters.
      while (Serial.available() > 0) {
        char next = (char)Serial.peek();
        if (next == '\n' || next == '\r') {
          Serial.read();
        } else {
          break;
        }
      }
      return true;
    }
  }
  return false;
}

static bool captureAndSaveOnePhoto() {
  camera_fb_t *fb = captureFrameWithWarmup();
  if (!fb) {
    Serial.println("[FATAL] Capture failed");
    return false;
  }

  uint16_t currentNum = readPhotoCounter();
  char filePath[96];
  if (!buildNextFilePath(filePath, sizeof(filePath), currentNum)) {
    Serial.println("[FATAL] File path build failed");
    esp_camera_fb_return(fb);
    return false;
  }

  File file = SD.open(filePath, FILE_WRITE);
  if (!file) {
    Serial.printf("[FATAL] Failed to open: %s\n", filePath);
    esp_camera_fb_return(fb);
    return false;
  }

  size_t frameLen = fb->len;
  size_t written = file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);

  if (written != frameLen) {
    Serial.printf("[FATAL] Partial write: %u/%u bytes\n", (unsigned int)written, (unsigned int)frameLen);
    return false;
  }

  uint32_t nextNum = (uint32_t)currentNum + 1U;
  if (nextNum > PHOTO_NUM_MAX) {
    nextNum = PHOTO_NUM_MIN;
  }
  writePhotoCounter((uint16_t)nextNum);

  Serial.printf("[OK] Saved: %s (%u bytes)\n", filePath, (unsigned int)written);
  return true;
}

void setup() {
  // Disable brownout detector to match slave stability behavior.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(500);

  Serial.println("\n===========================================");
  Serial.println("ESP32-CAM DATA GATHERING");
  Serial.println("===========================================");

  EEPROM.begin(EEPROM_SIZE);

  if (!initSDCard()) {
    Serial.println("[FATAL] SD unavailable; nothing to do");
    return;
  }

  initializeSCCBBus();
  delay(500);

  if (!initCameraSameAsSlave()) {
    Serial.println("[FATAL] Camera init failed; nothing to do");
    return;
  }

  Serial.println("[READY] Press Enter in Serial Monitor to capture and save a photo");
}

void loop() {
  if (!didReceiveEnterPress()) {
    delay(10);
    return;
  }

  Serial.println("[TRIGGER] Enter received, capturing...");
  captureAndSaveOnePhoto();
  Serial.println("[READY] Press Enter to capture again");
}
