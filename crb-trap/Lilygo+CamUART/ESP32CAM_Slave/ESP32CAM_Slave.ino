/**
 * @file      ESP32CAM_Slave.ino
 * @date      2026-02-10
 */

#include "Arduino.h"
#include "EEPROM.h"
#include "FS.h"
#include "SD_MMC.h"
#include "esp_camera.h"
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

// Flash LED (NOTE: GPIO4 conflicts with SD 4-bit mode D1; OK in 1-bit mode)
#define FLASH_LED_PIN 4

// UART pins
#define UART_RX_PIN 16 // GPIO16 (U2RXD) ← LilyGo TX
#define UART_TX_PIN 13 // GPIO13 → LilyGo RX (NOTE: conflicts with SD D3 in 4-bit mode)

// ==================== UART CONFIGURATION ====================
#define UART_BAUD_RATE 115200
#define CMD_BUFFER_SIZE 128

// ==================== EEPROM CONFIGURATION ====================
#define EEPROM_SIZE 1 // 1 byte for photo counter (0-255)

// Use Serial2 for communication with master
HardwareSerial MasterSerial(2);

// ==================== GLOBAL VARIABLES ====================
uint8_t pictureNumber = 1;      // store as byte (0-255)
bool cameraInitialized = false;
bool sdCardInitialized = false;

// ==================== HELPERS ====================
static bool sdWriteTest() {
  File t = SD_MMC.open("/test.txt", FILE_WRITE);
  if (!t) return false;
  t.println("sd write ok");
  t.close();
  return true;
}

static framesize_t selectFrameSize(uint16_t width, uint16_t height) {
  if (width >= 1600 || height >= 1200) return FRAMESIZE_UXGA;
  if (width >= 1280 || height >= 1024) return FRAMESIZE_SXGA;
  if (width >= 1024 || height >= 768)  return FRAMESIZE_XGA;
  if (width >= 800  || height >= 600)  return FRAMESIZE_SVGA;
  return FRAMESIZE_VGA;
}

static uint8_t calculateFlashBrightness(uint16_t lux) {
  if (lux < 50)  return 255;
  if (lux < 150) return 192;
  if (lux < 300) return 128;
  if (lux < 500) return 64;
  return 0;
}

static void setFlashBrightness(uint8_t brightness) {
  if (brightness > 0) {
    ledcAttach(FLASH_LED_PIN, 5000, 8);
    ledcWrite(FLASH_LED_PIN, brightness);
    Serial.printf("[LED] Flash brightness set to %d/255\n", brightness);
  } else {
    ledcDetach(FLASH_LED_PIN);
    digitalWrite(FLASH_LED_PIN, LOW);
    Serial.println("[LED] Flash OFF");
  }
}

// ==================== SETUP ====================
void setup() {
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(100);

  Serial.println("\n===========================================");
  Serial.println("ESP32-CAM UART Slave");
  Serial.println("===========================================");
  Serial.println("[INIT] Brownout detector disabled");

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);
  uint8_t stored = EEPROM.read(0);
  pictureNumber = (stored == 255) ? 1 : (uint8_t)(stored + 1);
  Serial.printf("[EEPROM] Picture number: %d\n", pictureNumber);

  // SD Card (FORCE 1-BIT MODE to avoid GPIO4/GPIO13 conflicts)
  Serial.println("[SD] Starting SD Card (forced 1-bit mode)...");
  if (!SD_MMC.begin("/sdcard", true)) {   // true = 1-bit mode
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

  Serial.print("[SD] Card Type: ");
  if (cardType == CARD_MMC) Serial.println("MMC");
  else if (cardType == CARD_SD) Serial.println("SDSC");
  else if (cardType == CARD_SDHC) Serial.println("SDHC");
  else Serial.println("UNKNOWN");

  sdCardInitialized = true;

  // Quick SD write sanity test
  if (!sdWriteTest()) {
    Serial.println("[SD] ERROR: Write test failed (test.txt cannot be created)");
    Serial.println("[SD] Likely causes: power droop, card not FAT32, or still pin conflict.");
    delay(1000);
    ESP.restart();
  } else {
    Serial.println("[SD] Write test OK (test.txt created/appended)");
  }

  // Camera init
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

  Serial.println("[CAM] Initializing camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] ERROR: Camera init failed 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }
  Serial.println("[CAM] Camera initialized successfully");
  cameraInitialized = true;

  delay(1000);

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  MasterSerial.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.printf("[UART] Initialized on RX:%d TX:%d @ %d baud\n",
                UART_RX_PIN, UART_TX_PIN, UART_BAUD_RATE);

  delay(300);

  // READY framed, double-sent
  MasterSerial.print("<READY>\n");
  MasterSerial.flush();
  delay(100);
  MasterSerial.print("<READY>\n");
  MasterSerial.flush();

  Serial.println("[UART] Sent READY signal to master (framed, double-sent)");
  Serial.println("[STATUS] Waiting for commands...");
  Serial.println("===========================================\n");
}

// ==================== PHOTO CAPTURE ====================
static bool captureAndSavePhoto(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality) {
  if (!cameraInitialized) {
    Serial.println("[CAM] ERROR: Camera not initialized");
    return false;
  }
  if (!sdCardInitialized) {
    Serial.println("[SD] ERROR: SD not initialized");
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (!s) {
    Serial.println("[CAM] ERROR: No sensor");
    return false;
  }

  framesize_t frameSize = selectFrameSize(width, height);
  s->set_framesize(s, frameSize);
  Serial.printf("[CAM] Frame size request: %dx%d\n", width, height);

  s->set_quality(s, quality);
  Serial.printf("[CAM] JPEG quality set to %d\n", quality);

  uint8_t flashBrightness = calculateFlashBrightness(lux);
  Serial.printf("[FLASH] LUX=%d → Brightness=%d/255\n", lux, flashBrightness);

  if (flashBrightness > 0) {
    setFlashBrightness(flashBrightness);
    delay(200);
  }

  Serial.println("[CAM] Capturing image...");
  camera_fb_t *fb = nullptr;

  for (int retry = 0; retry < 3; retry++) {
    fb = esp_camera_fb_get();
    if (fb) break;
    Serial.printf("[CAM] Capture failed, retry %d\n", retry + 1);
    delay(500);
  }

  if (flashBrightness > 0) setFlashBrightness(0);

  if (!fb) {
    Serial.println("[CAM] ERROR: Capture failed after retries");
    return false;
  }

  Serial.printf("[CAM] Image captured: %zu bytes\n", fb->len);

  char filename[32];
  snprintf(filename, sizeof(filename), "/image%u.jpg", pictureNumber);

  // Prepare next counter (wrap)
  uint8_t next = (pictureNumber == 255) ? 1 : (uint8_t)(pictureNumber + 1);
  EEPROM.write(0, next);
  EEPROM.commit();
  Serial.printf("[EEPROM] Counter updated: %u\n", next);

  Serial.printf("[SD] Saving to %s...\n", filename);
  File file = SD_MMC.open(filename, FILE_WRITE);

  if (!file) {
    Serial.println("[SD] ERROR: Failed to open file for writing");
    esp_camera_fb_return(fb);
    return false;
  }

  const size_t totalBytes = fb->len;
  size_t bytesWritten = 0;
  const size_t chunkSize = 1024;

  while (bytesWritten < totalBytes) {
    size_t toWrite = min(chunkSize, totalBytes - bytesWritten);
    size_t written = file.write(fb->buf + bytesWritten, toWrite);
    if (written != toWrite) {
      Serial.printf("[SD] ERROR: Write failed at %zu bytes\n", bytesWritten);
      break;
    }
    bytesWritten += written;
  }

  file.close();
  esp_camera_fb_return(fb);

  if (bytesWritten != totalBytes) {
    Serial.printf("[SD] ERROR: Write incomplete (%zu/%zu)\n", bytesWritten, totalBytes);
    return false;
  }

  Serial.printf("[SD] SUCCESS: %s saved (%zu bytes)\n", filename, bytesWritten);

  // advance pictureNumber only after success
  pictureNumber = next;

  char response[64];
  snprintf(response, sizeof(response), "OK:%s", filename);
  MasterSerial.println(response);
  Serial.printf("[UART] Sent: %s\n", response);
  return true;
}

// ==================== COMMAND HANDLING ====================
static void processCommand(String command) {
  if (!command.startsWith("PHOTO:")) {
    Serial.printf("[ERROR] Unknown command: %s\n", command.c_str());
    MasterSerial.println("ERROR:Unknown command");
    return;
  }

  int idx1 = command.indexOf(':', 6);
  int idx2 = command.indexOf(':', idx1 + 1);
  int idx3 = command.indexOf(':', idx2 + 1);

  if (idx1 <= 0 || idx2 <= 0 || idx3 <= 0) {
    Serial.println("[ERROR] Invalid command format");
    MasterSerial.println("ERROR:Invalid format");
    return;
  }

  uint16_t lux = command.substring(6, idx1).toInt();
  uint16_t width = command.substring(idx1 + 1, idx2).toInt();
  uint16_t height = command.substring(idx2 + 1, idx3).toInt();
  uint8_t quality = command.substring(idx3 + 1).toInt();

  Serial.printf("[PARSE] LUX=%d, Size=%dx%d, Quality=%d\n", lux, width, height, quality);

  if (width < 320 || width > 1600 || height < 240 || height > 1200) {
    Serial.println("[ERROR] Invalid resolution");
    MasterSerial.println("ERROR:Invalid resolution");
    return;
  }
  if (quality < 1 || quality > 63) {
    Serial.println("[ERROR] Invalid quality (1-63)");
    MasterSerial.println("ERROR:Invalid quality");
    return;
  }

  if (!captureAndSavePhoto(lux, width, height, quality)) {
    MasterSerial.println("ERROR:Capture failed");
  }
}

// ==================== LOOP ====================
void loop() {
  static String commandBuffer = "";

  while (MasterSerial.available()) {
    char c = (char)MasterSerial.read();
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
