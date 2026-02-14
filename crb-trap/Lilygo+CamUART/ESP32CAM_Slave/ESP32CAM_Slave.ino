/**
 * @file      ESP32CAM_Slave.ino
 * @author    Camera UART Slave Controller
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-02-10
 * @note      ESP32-CAM Slave - Receives UART commands from LilyGo master
 *
 * Hardware Setup:
 *  - ESP32-CAM board with OV2640 camera
 *  - GPIO16 (U2RXD) ← LilyGo GPIO18 (TX)
 *  - GPIO13 (HS2_DATA3) → LilyGo GPIO19 (RX)
 *  - GPIO4 connected to flash LED
 *  - SD Card in slot (1-bit mode: CLK=14, CMD=15, DATA0=2)
 *  - Power controlled by LilyGo GPIO23
 *
 * Communication Protocol:
 *  Receives: "PHOTO:lux:width:height:quality\n"
 *  Example: "PHOTO:500:1600:1200:10\n"
 *  Sends: "READY\n" on boot, then "OK:filename.jpg\n" or "ERROR:msg\n"
 *
 * Flash LED Brightness Algorithm (based on LUX):
 *  - 0-50 LUX (very dark): Flash at 255 (100%)
 *  - 50-150 LUX (dark): Flash at 192 (75%)
 *  - 150-300 LUX (dim): Flash at 128 (50%)
 *  - 300-500 LUX (indoor): Flash at 64 (25%)
 *  - 500+ LUX (bright): Flash at 0 (OFF)
 *
 * Camera Resolutions:
 *  - UXGA: 1600x1200
 *  - SXGA: 1280x1024
 *  - XGA: 1024x768
 *  - SVGA: 800x600
 *  - VGA: 640x480
 */

#include "Arduino.h"
#include "EEPROM.h"
#include "FS.h"
#include "SD_MMC.h"
#include "driver/rtc_io.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

// ==================== PIN DEFINITIONS ====================
// Camera pins for AI-THINKER ESP32-CAM
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

// Flash LED
#define FLASH_LED_PIN 4

// UART pins
#define UART_RX_PIN 16 // GPIO16 (U2RXD) ← LilyGo TX
#define UART_TX_PIN 13 // GPIO13 → LilyGo RX

// ==================== UART CONFIGURATION ====================
#define UART_BAUD_RATE 115200
#define CMD_BUFFER_SIZE 128

// ==================== EEPROM CONFIGURATION ====================
#define EEPROM_SIZE 1 // 1 byte for photo counter (0-255)

// Use Serial2 for communication with master
HardwareSerial MasterSerial(2);

// ==================== GLOBAL VARIABLES ====================
int pictureNumber = 1; // Counter for filenames
bool cameraInitialized = false;
bool sdCardInitialized = false; // Track SD card state

// ==================== FUNCTION PROTOTYPES ====================
bool initCamera();
bool initSDCard();
uint8_t calculateFlashBrightness(uint16_t lux);
framesize_t selectFrameSize(uint16_t width, uint16_t height);
bool captureAndSavePhoto(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality);
void processCommand(String command);
void setFlashBrightness(uint8_t brightness);

// ==================== SETUP ====================
void setup() {
  // CRITICAL: Disable brownout detector to prevent crashes during camera init
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200); // Debug console (USB)
  delay(100);

  Serial.println("\n===========================================");
  Serial.println("ESP32-CAM UART Slave");
  Serial.println("===========================================");
  Serial.println("[INIT] Brownout detector disabled");

  // Initialize EEPROM FIRST (matches working reference exactly)
  EEPROM.begin(EEPROM_SIZE);
  pictureNumber = EEPROM.read(0) + 1;
  if (pictureNumber > 255) {
    pictureNumber = 1;
  }
  Serial.printf("[EEPROM] Picture number: %d\n", pictureNumber);

  // Initialize SD Card BEFORE camera (CRITICAL - matches working reference)
  Serial.println("[SD] Starting SD Card");
  if (!SD_MMC.begin()) {
    Serial.println("[SD] ERROR: SD Card Mount Failed");
    delay(1000);
    ESP.restart();
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[SD] ERROR: No SD Card attached");
    delay(1000);
    ESP.restart();
  }

  Serial.printf("[SD] Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  sdCardInitialized = true;

  // Camera configuration (exactly like working reference)
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

  // CRITICAL: Reduced settings to prevent stack overflow (from working reference)
  if (psramFound()) {
    Serial.println("[CAM] PSRAM found");
    config.frame_size = FRAMESIZE_XGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  } else {
    Serial.println("[CAM] PSRAM not found");
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }

  config.grab_mode = CAMERA_GRAB_LATEST;

  // Initialize Camera
  Serial.println("[CAM] Initializing camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] ERROR: Camera init failed with error 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }
  Serial.println("[CAM] Camera initialized successfully");
  cameraInitialized = true;

  // Get sensor settings
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_special_effect(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 0);
    s->set_ae_level(s, 0);
    s->set_aec_value(s, 300);
    s->set_gain_ctrl(s, 1);
    s->set_agc_gain(s, 0);
    s->set_gainceiling(s, (gainceiling_t)0);
    s->set_bpc(s, 0);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    s->set_dcw(s, 1);
    s->set_colorbar(s, 0);
  }

  // Wait for camera to stabilize
  delay(1000);

  // NOW initialize UART after everything else is stable
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  MasterSerial.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.printf("[UART] Initialized on RX:%d TX:%d @ %d baud\n",
                UART_RX_PIN, UART_TX_PIN, UART_BAUD_RATE);

  delay(500);
  MasterSerial.println("READY");
  Serial.println("[UART] Sent READY signal to master");

  Serial.println("[STATUS] Waiting for commands...");
  Serial.println("===========================================\n");
}

// ==================== LOOP ====================
void loop() {
  static String commandBuffer = "";

  while (MasterSerial.available()) {
    char c = MasterSerial.read();
    commandBuffer += c;

    if (c == '\n') {
      commandBuffer.trim();
      Serial.printf("[UART] Received: %s\n", commandBuffer.c_str());
      processCommand(commandBuffer);
      commandBuffer = "";
    }

    if (commandBuffer.length() > CMD_BUFFER_SIZE) {
      Serial.println("[ERROR] Command buffer overflow!");
      commandBuffer = "";
    }
  }

  delay(10);
}

// ==================== FUNCTION IMPLEMENTATIONS ====================

/**
 * @brief Initialize the camera with AI-THINKER pin configuration
 * @return true if successful, false otherwise
 */
bool initCamera() {
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

  // CRITICAL: Reduced frame size and quality to prevent stack overflow
  // This matches the working reference code
  if (psramFound()) {
    Serial.println("[CAM] PSRAM found");
    config.frame_size = FRAMESIZE_XGA; // Reduced from UXGA to XGA
    config.jpeg_quality = 12;          // Increased from 10 to 12 (lower quality, smaller file)
    config.fb_count = 1;               // Reduced from 2 to 1 to save memory
  } else {
    Serial.println("[CAM] PSRAM not found");
    config.frame_size = FRAMESIZE_VGA; // Reduced from SVGA to VGA
    config.jpeg_quality = 15;          // Lower quality for boards without PSRAM
    config.fb_count = 1;
  }

  // Additional memory-saving configurations
  config.grab_mode = CAMERA_GRAB_LATEST; // Changed from WHEN_EMPTY to LATEST

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] ERROR: Camera init failed with error 0x%x\n", err);
    return false;
  }

  // Get sensor settings
  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    Serial.println("[CAM] ERROR: Failed to get camera sensor");
    return false;
  }

  // Sensor adjustments for better quality
  s->set_brightness(s, 0);                 // -2 to 2
  s->set_contrast(s, 0);                   // -2 to 2
  s->set_saturation(s, 0);                 // -2 to 2
  s->set_special_effect(s, 0);             // 0 to 6 (0 - No Effect)
  s->set_whitebal(s, 1);                   // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);                   // 0 = disable , 1 = enable
  s->set_wb_mode(s, 0);                    // 0 to 4 - if awb_gain enabled
  s->set_exposure_ctrl(s, 1);              // 0 = disable , 1 = enable
  s->set_aec2(s, 0);                       // 0 = disable , 1 = enable
  s->set_ae_level(s, 0);                   // -2 to 2
  s->set_aec_value(s, 300);                // 0 to 1200
  s->set_gain_ctrl(s, 1);                  // 0 = disable , 1 = enable
  s->set_agc_gain(s, 0);                   // 0 to 30
  s->set_gainceiling(s, (gainceiling_t)0); // 0 to 6
  s->set_bpc(s, 0);                        // 0 = disable , 1 = enable
  s->set_wpc(s, 1);                        // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);                    // 0 = disable , 1 = enable
  s->set_lenc(s, 1);                       // 0 = disable , 1 = enable
  s->set_hmirror(s, 0);                    // 0 = disable , 1 = enable
  s->set_vflip(s, 0);                      // 0 = disable , 1 = enable
  s->set_dcw(s, 1);                        // 0 = disable , 1 = enable
  s->set_colorbar(s, 0);                   // 0 = disable , 1 = enable

  Serial.println("[CAM] Sensor settings optimized for memory usage");

  return true;
}

/**
 * @brief Initialize SD card in 1-bit mode
 * @return true if successful, false otherwise
 */
bool initSDCard() {
  Serial.println("[SD] Mounting MicroSD Card...");

  // Simple SD_MMC.begin() - uses default settings and auto-detects 1-bit/4-bit mode
  // This matches the working reference code
  if (!SD_MMC.begin()) {
    Serial.println("[SD] ERROR: MicroSD Card Mount Failed");
    Serial.println("[SD] Check: 1) Card inserted? 2) FAT32 format? 3) Power OK?");
    return false;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[SD] ERROR: No SD card detected");
    return false;
  }

  Serial.print("[SD] Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("[SD] Card Size: %lluMB\n", cardSize);

  return true;
}

/**
 * @brief Calculate flash LED brightness based on ambient light (LUX)
 * @param lux Light intensity value
 * @return PWM brightness value (0-255)
 */
uint8_t calculateFlashBrightness(uint16_t lux) {
  if (lux < 50) {
    return 255; // Very dark - maximum flash
  } else if (lux < 150) {
    return 192; // Dark - 75% flash
  } else if (lux < 300) {
    return 128; // Dim - 50% flash
  } else if (lux < 500) {
    return 64; // Indoor - 25% flash
  } else {
    return 0; // Bright - no flash
  }
}

/**
 * @brief Select appropriate frame size based on width and height
 * @param width Desired width
 * @param height Desired height
 * @return framesize_t enum value
 */
framesize_t selectFrameSize(uint16_t width, uint16_t height) {
  // Match to closest standard resolution
  if (width >= 1600 || height >= 1200) {
    return FRAMESIZE_UXGA; // 1600x1200
  } else if (width >= 1280 || height >= 1024) {
    return FRAMESIZE_SXGA; // 1280x1024
  } else if (width >= 1024 || height >= 768) {
    return FRAMESIZE_XGA; // 1024x768
  } else if (width >= 800 || height >= 600) {
    return FRAMESIZE_SVGA; // 800x600
  } else {
    return FRAMESIZE_VGA; // 640x480
  }
}

/**
 * @brief Set flash LED brightness using PWM
 * @param brightness PWM value 0-255
 */
void setFlashBrightness(uint8_t brightness) {
  if (brightness > 0) {
    // New ESP32 Arduino Core 3.x API
    ledcAttach(FLASH_LED_PIN, 5000, 8); // Pin, 5kHz frequency, 8-bit resolution
    ledcWrite(FLASH_LED_PIN, brightness);
    Serial.printf("[LED] Flash brightness set to %d/255\n", brightness);
  } else {
    ledcDetach(FLASH_LED_PIN);
    digitalWrite(FLASH_LED_PIN, LOW);
    Serial.println("[LED] Flash OFF");
  }
}

/**
 * @brief Capture photo and save to SD card
 * @param lux Light intensity for flash adjustment
 * @param width Desired width
 * @param height Desired height
 * @param quality JPEG quality (1-63, lower = better)
 * @return true if successful, false otherwise
 */
bool captureAndSavePhoto(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality) {
  if (!cameraInitialized) {
    Serial.println("[CAM] ERROR: Camera not initialized");
    return false;
  }

  // SD card should already be initialized in setup(), but check anyway
  if (!sdCardInitialized) {
    Serial.println("[SD] ERROR: SD card not initialized!");
    return false;
  }

  // Configure camera settings
  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    Serial.println("[CAM] ERROR: Failed to get sensor");
    return false;
  }

  // Set frame size
  framesize_t frameSize = selectFrameSize(width, height);
  s->set_framesize(s, frameSize);
  Serial.printf("[CAM] Frame size set to %dx%d\n", width, height);

  // Set JPEG quality (1-63, lower = better quality, larger file)
  s->set_quality(s, quality);
  Serial.printf("[CAM] JPEG quality set to %d\n", quality);

  // Calculate and set flash brightness based on LUX
  uint8_t flashBrightness = calculateFlashBrightness(lux);
  Serial.printf("[FLASH] LUX=%d → Brightness=%d/255\n", lux, flashBrightness);

  // Turn on flash if needed
  if (flashBrightness > 0) {
    setFlashBrightness(flashBrightness);
    delay(200); // Let the flash stabilize
  }

  // Capture photo with retry logic (from working reference code)
  Serial.println("[CAM] Capturing image...");
  camera_fb_t *fb = NULL;

  for (int retry = 0; retry < 3; retry++) {
    fb = esp_camera_fb_get();
    if (fb)
      break;
    Serial.printf("[CAM] Capture failed, retry %d\n", retry + 1);
    delay(500);
  }

  // Turn off flash immediately after capture
  if (flashBrightness > 0) {
    setFlashBrightness(0);
  }

  if (!fb) {
    Serial.println("[CAM] ERROR: Camera capture failed after retries");
    return false;
  }

  Serial.printf("[CAM] Image captured: %zu bytes\n", fb->len);

  // Generate filename using EEPROM counter
  char filename[32];
  snprintf(filename, sizeof(filename), "/image%d.jpg", pictureNumber);

  // Increment counter for next photo
  pictureNumber++;

  // Update EEPROM (simple 1-byte counter)
  EEPROM.write(0, pictureNumber);
  EEPROM.commit();
  Serial.printf("[EEPROM] Counter updated: %d\n", pictureNumber);

  // Save to SD card
  Serial.printf("[SD] Saving to %s...\n", filename);
  File file = SD_MMC.open(filename, FILE_WRITE);

  if (!file) {
    Serial.println("[SD] ERROR: Failed to open file for writing");
    esp_camera_fb_return(fb);
    return false;
  }

  // Write file in chunks to prevent memory issues (from working reference code)
  size_t totalBytes = fb->len;
  size_t bytesWritten = 0;
  const size_t chunkSize = 1024; // Write in 1KB chunks

  while (bytesWritten < totalBytes) {
    size_t toWrite = min(chunkSize, totalBytes - bytesWritten);
    size_t written = file.write(fb->buf + bytesWritten, toWrite);
    if (written != toWrite) {
      Serial.println("[SD] Write error occurred");
      break;
    }
    bytesWritten += written;
  }

  file.close();

  // Return the frame buffer immediately after use
  esp_camera_fb_return(fb);

  if (bytesWritten != totalBytes) {
    Serial.printf("[SD] ERROR: Write incomplete (%zu/%zu bytes)\n", bytesWritten, totalBytes);
    return false;
  }

  Serial.printf("[SD] SUCCESS: %s saved (%zu bytes)\n", filename, bytesWritten);

  // Send confirmation to master
  char response[64];
  snprintf(response, sizeof(response), "OK:%s", filename);
  MasterSerial.println(response);
  Serial.printf("[UART] Sent: %s\n", response);

  return true;
}

/**
 * @brief Process incoming UART command
 * @param command Command string (e.g., "PHOTO:500:1600:1200:10")
 */
void processCommand(String command) {
  // Expected format: "PHOTO:lux:width:height:quality"
  if (command.startsWith("PHOTO:")) {
    // Parse parameters
    int idx1 = command.indexOf(':', 6);
    int idx2 = command.indexOf(':', idx1 + 1);
    int idx3 = command.indexOf(':', idx2 + 1);

    if (idx1 > 0 && idx2 > 0 && idx3 > 0) {
      uint16_t lux = command.substring(6, idx1).toInt();
      uint16_t width = command.substring(idx1 + 1, idx2).toInt();
      uint16_t height = command.substring(idx2 + 1, idx3).toInt();
      uint8_t quality = command.substring(idx3 + 1).toInt();

      Serial.printf("[PARSE] LUX=%d, Size=%dx%d, Quality=%d\n",
                    lux, width, height, quality);

      // Validate parameters
      if (width < 320 || width > 1600 || height < 240 || height > 1200) {
        Serial.println("[ERROR] Invalid resolution");
        MasterSerial.println("ERROR:Invalid resolution");
        return;
      }

      if (quality < 1 || quality > 63) {
        Serial.println("[ERROR] Invalid quality (must be 1-63)");
        MasterSerial.println("ERROR:Invalid quality");
        return;
      }

      // Capture photo
      if (!captureAndSavePhoto(lux, width, height, quality)) {
        MasterSerial.println("ERROR:Capture failed");
      }

    } else {
      Serial.println("[ERROR] Invalid command format");
      MasterSerial.println("ERROR:Invalid format");
    }

  } else {
    Serial.printf("[ERROR] Unknown command: %s\n", command.c_str());
    MasterSerial.println("ERROR:Unknown command");
  }
}
