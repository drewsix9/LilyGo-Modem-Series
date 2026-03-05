/**
 * @file      ESP32CAM_Slave.ino
 * @date      2026-02-10
 */

#include "Arduino.h"
#include "EEPROM.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
#define UART_BAUD_RATE 57600
#define CMD_BUFFER_SIZE 128
#define CMD_MAX_WAIT_MS 30000

// ==================== EEPROM CONFIGURATION ====================
#define EEPROM_SIZE 1 // 1 byte for photo counter (0-255)

// Use Serial2 for communication with master
HardwareSerial MasterSerial(2);

// ==================== GLOBAL VARIABLES ====================
uint8_t pictureNumber = 1; // store as byte (0-255)
bool cameraInitialized = false;
char commandBuffer[CMD_BUFFER_SIZE] = {0}; // Use fixed buffer instead of String to avoid heap fragmentation
volatile int commandBufferPos = 0;
volatile uint32_t lastCommandTime = 0;
volatile bool uartReady = false;
static bool uartInitialized = false;   // Track if UART init has been done in loop()
static bool commandInProgress = false; // Guard against duplicate command processing

// ==================== CRC32 CALCULATION ====================
uint32_t calculateCRC32(uint8_t *data, uint32_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (uint32_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
  }
  return crc ^ 0xFFFFFFFF;
}

// ==================== CRC16 CALCULATION (CCITT) ====================
uint16_t calculateCRC16(uint8_t *data, uint32_t length) {
  uint16_t crc = 0xFFFF;
  for (uint32_t i = 0; i < length; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int j = 0; j < 8; j++) {
      if (crc & 0x8000)
        crc = (crc << 1) ^ 0x1021;
      else
        crc <<= 1;
    }
  }
  return crc;
}

// ==================== HELPERS ====================
static framesize_t selectFrameSize(uint16_t width, uint16_t height) {
  // Max resolution capped at SXGA (1280x1024). UXGA (1600x1200) produces
  // corrupted/zeroed JPEG frames on ESP32-CAM because the OV2640 pixel output
  // at 20MHz XCLK overwhelms the SPI bus to PSRAM during DMA transfer.
  if (width >= 1280 || height >= 1024)
    return FRAMESIZE_SXGA;
  if (width >= 1024 || height >= 768)
    return FRAMESIZE_XGA;
  if (width >= 800 || height >= 600)
    return FRAMESIZE_SVGA;
  return FRAMESIZE_VGA;
}

static void initializeSCCBBus() {
  // Reset I2C bus (SCCB protocol) used by camera sensor
  // This prevents stuck bus lines after crashes
  Serial.println("[CAM] Initializing I2C SCCB bus...");
  pinMode(SIOD_GPIO_NUM, OUTPUT);
  pinMode(SIOC_GPIO_NUM, OUTPUT);
  digitalWrite(SIOD_GPIO_NUM, HIGH);
  digitalWrite(SIOC_GPIO_NUM, HIGH);
  delay(10);

  // Send STOP condition (9 clock pulses with SDA low at end)
  for (int i = 0; i < 9; i++) {
    digitalWrite(SIOC_GPIO_NUM, LOW);
    delay(2);
    digitalWrite(SIOC_GPIO_NUM, HIGH);
    delay(2);
  }

  // Final STOP condition
  digitalWrite(SIOD_GPIO_NUM, LOW);
  delay(2);
  digitalWrite(SIOD_GPIO_NUM, HIGH);
  delay(2);

  Serial.println("[CAM] I2C bus reset complete");
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
    // Simple LED control - just turn on for bright environments
    if (FLASH_LED_PIN >= 0) {
      digitalWrite(FLASH_LED_PIN, HIGH); // Turn on flash
      Serial.printf("[LED] Flash ON (level %d/255)\n", brightness);
    }
  } else {
    // Turn off flash
    if (FLASH_LED_PIN >= 0) {
      digitalWrite(FLASH_LED_PIN, LOW);
    }
    Serial.println("[LED] Flash OFF");
  }
}

// ==================== CAMERA INIT ====================
// Reusable camera initialization — called at startup and before each capture
// to ensure DMA, sensor, and JPEG encoder are configured for the target resolution.
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
  // 20MHz XCLK: required for reliable OV2640 sensor detection via SCCB.
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = frameSize;
    config.jpeg_quality = jpegQuality;
    // fb_count=1: proven to produce valid JPEG frames on this board.
    // fb_count=2 with PSRAM causes DMA to write to wrong/stale buffers,
    // resulting in zeroed frame data (00 00 00 headers).
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }

  // GRAB_LATEST: returns the most recently completed frame.
  // Combined with fb_count=1, this is the simplest and most reliable DMA mode.
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

// ==================== SETUP ====================
void setup() {
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(500); // More stable startup - wait longer for Serial to stabilize

  Serial.println("\n\n===========================================");
  Serial.println("ESP32-CAM UART Slave");
  Serial.println("===========================================");
  Serial.println("[INIT] Brownout detector disabled");

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);
  uint8_t stored = EEPROM.read(0);
  pictureNumber = (stored == 255) ? 1 : (uint8_t)(stored + 1);
  Serial.printf("[EEPROM] Picture number: %d\n", pictureNumber);

  // Reset and initialize I2C bus BEFORE camera init
  initializeSCCBBus();
  delay(500);

  // Initial camera init at SXGA (max usable resolution on ESP32-CAM with PSRAM)
  if (!initCamera(FRAMESIZE_SXGA, 12)) {
    Serial.println("[CAM] FATAL: Camera init failed, restarting...");
    delay(1000);
    ESP.restart();
  }

  delay(1000);

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  Serial.println("[STATUS] Setup complete, deferring UART init to loop...");
  Serial.println("===========================================\n");
}

// ==================== SEND PHOTO VIA UART (BLOCK PROTOCOL) ====================
// Sends photo in 512-byte blocks with per-block CRC16 and ACK/NAK retransmit.
// Protocol:
//   <PHOTO_START>\n
//   SIZE:NNN\n
//   B:offset:len:CRC16\n  (block header)
//   [raw binary block data]
//   (master sends A\n for ACK or N\n for NAK)
//   ... repeat for all blocks ...
//   CRC32:XXXXXXXX\n
//   <PHOTO_END>\n
bool sendPhotoViaUART(camera_fb_t *fb) {
  if (!fb) {
    Serial.println("[UART] ERROR: No frame buffer");
    return false;
  }

  if (!uartReady) {
    Serial.println("[UART] ERROR: UART not ready");
    return false;
  }

  // Calculate CRC32 of entire photo
  uint32_t crc32 = calculateCRC32(fb->buf, fb->len);
  Serial.printf("[UART] CRC32: %08X\n", crc32);

  // Send start marker
  MasterSerial.print("<PHOTO_START>\n");
  MasterSerial.flush();
  Serial.println("[UART] Sent: <PHOTO_START>");

  // Send total size
  MasterSerial.printf("SIZE:%u\n", fb->len);
  MasterSerial.flush();
  Serial.printf("[UART] Sent: SIZE:%u\n", fb->len);

  // Send data in verified blocks
  const size_t BLOCK_SIZE = 512;
  const int MAX_BLOCK_RETRIES = 5;
  size_t totalSent = 0;
  uint32_t totalRetries = 0;

  while (totalSent < fb->len) {
    size_t blockLen = fb->len - totalSent;
    if (blockLen > BLOCK_SIZE)
      blockLen = BLOCK_SIZE;
    uint16_t blockCRC = calculateCRC16(fb->buf + totalSent, blockLen);

    bool blockAcked = false;
    for (int retry = 0; retry < MAX_BLOCK_RETRIES && !blockAcked; retry++) {
      if (retry > 0) {
        Serial.printf("[UART] Block retry %d at offset %u\n", retry, totalSent);
        totalRetries++;
        // Drain any stale bytes before resending
        while (MasterSerial.available())
          MasterSerial.read();
        delay(50);
      }

      // Send block header: B:offset:length:CRC16
      MasterSerial.printf("B:%u:%u:%04X\n", totalSent, blockLen, blockCRC);
      MasterSerial.flush();

      // Send raw block data
      size_t written = MasterSerial.write(fb->buf + totalSent, blockLen);
      MasterSerial.flush();

      if (written != blockLen) {
        Serial.printf("[UART] ERROR: Block write failed (%u/%u)\n", written, blockLen);
        continue; // Retry this block
      }

      // Wait for ACK/NAK from master (timeout 5 seconds)
      uint32_t waitStart = millis();
      char response = 0;
      while (millis() - waitStart < 5000) {
        if (MasterSerial.available()) {
          int c = MasterSerial.read();
          if (c == 'A' || c == 'N') {
            response = (char)c;
            // Drain trailing newline
            delay(5);
            while (MasterSerial.available())
              MasterSerial.read();
            break;
          }
        }
        delay(1);
      }

      if (response == 'A') {
        blockAcked = true;
      } else {
        Serial.printf("[UART] Block at %u: %s\n", totalSent,
                      response == 'N' ? "NAK received" : "ACK timeout");
      }
    }

    if (!blockAcked) {
      Serial.printf("[UART] FATAL: Block at %u failed after %d retries\n", totalSent, MAX_BLOCK_RETRIES);
      return false;
    }

    totalSent += blockLen;
  }

  Serial.printf("[UART] Sent %u bytes in %u blocks (%u retries)\n",
                totalSent, (uint32_t)((fb->len + BLOCK_SIZE - 1) / BLOCK_SIZE), totalRetries);

  // Send overall CRC32
  MasterSerial.printf("CRC32:%08X\n", crc32);
  MasterSerial.flush();
  Serial.printf("[UART] Sent: CRC32:%08X\n", crc32);

  // Send end marker
  MasterSerial.print("<PHOTO_END>\n");
  MasterSerial.flush();
  Serial.println("[UART] Sent: <PHOTO_END>");

  return true;
}

// ==================== PHOTO CAPTURE ====================
bool captureAndSendPhoto(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality) {

  if (!cameraInitialized) {
    Serial.println("[ERROR] Camera not initialized");
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (!s) {
    Serial.println("[CAM] ERROR: Failed to get sensor");
    return false;
  }

  // Configure sensor for requested resolution and quality.
  // Camera inited at SXGA — DMA buffers fit any resolution <= SXGA.
  framesize_t targetSize = selectFrameSize(width, height);
  s->set_framesize(s, targetSize);
  s->set_quality(s, quality);
  Serial.printf("[CAM] Settings: Size=%dx%d, Quality=%d\n", width, height, quality);

  // Wait for sensor to reconfigure and produce a frame with new settings.
  // With fb_count=1 + GRAB_LATEST, the next fb_get() will return the most
  // recently completed frame, which should already use the new settings.
  delay(500);

  // Flash based on LUX
  uint8_t flashBrightness = calculateFlashBrightness(lux);
  if (flashBrightness > 0) {
    setFlashBrightness(flashBrightness);
    delay(200);
  }

  // Capture strategy with fb_count=1:
  // - Grab one warm-up frame for auto-exposure, return it, wait 1 second
  //   for DMA to fully reset before the real capture.
  // - With fb_count=1, rapid get/return cycling exhausts the DMA pipeline
  //   (proven by earlier tests: 3 rapid warm-ups -> zeroed captures).
  //   One warm-up + long delay avoids this.
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

    // Diagnostic: dump first 16 bytes
    Serial.printf("[CAM] Frame %d: %u bytes, first 16: ", attempt + 1, temp->len);
    for (int i = 0; i < 16 && i < (int)temp->len; i++) {
      Serial.printf("%02X ", temp->buf[i]);
    }
    Serial.println();

    // Check for JPEG SOI (FF D8 FF) at offset 0
    bool isValidJpeg = (temp->len >= 3 &&
                        temp->buf[0] == 0xFF &&
                        temp->buf[1] == 0xD8 &&
                        temp->buf[2] == 0xFF);

    // If not at offset 0, scan entire buffer for SOI marker.
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
      // Long delay to let DMA fully reset before next attempt
      delay(1000);
      continue;
    }

    // First valid frame = warm-up (auto-exposure settling). Discard it.
    if (!warmupDone) {
      warmupDone = true;
      Serial.printf("[CAM] Warm-up frame: %u bytes (discarded for auto-exposure)\n", temp->len);
      esp_camera_fb_return(temp);
      // Critical: long delay after returning frame to let DMA pipeline fully
      // reset. With fb_count=1, DMA needs time to reinitialize its descriptor
      // chain and wait for the next VSYNC before writing a new frame.
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

  // Turn off flash
  if (flashBrightness > 0) {
    setFlashBrightness(0);
  }

  if (!fb) {
    Serial.println("[CAM] ERROR: Capture failed after all attempts");
    return false;
  }

  Serial.printf("[CAM] Image captured: %u bytes\n", fb->len);

  // Send via UART
  bool success = sendPhotoViaUART(fb);
  esp_camera_fb_return(fb);

  if (success) {
    MasterSerial.println("OK:PHOTO_SENT");
    Serial.println("[UART] Photo transmission complete");
  } else {
    MasterSerial.println("ERROR:SEND_FAILED");
    Serial.println("[UART] Photo transmission failed");
  }

  return success;
}

// ==================== COMMAND HANDLING ====================
static void processCommand(char *command) {
  if (!uartReady) {
    Serial.println("[ERROR] UART not ready");
    commandInProgress = false;
    return;
  }

  // Guard against duplicate processing (UART buffer can contain the same command twice)
  if (commandInProgress) {
    Serial.println("[WARN] Command already in progress, ignoring duplicate");
    return;
  }
  commandInProgress = true;

  if (strncmp(command, "PHOTO:", 6) != 0) {
    Serial.printf("[ERROR] Unknown command: %s\n", command);
    MasterSerial.println("ERROR:Unknown command");
    commandInProgress = false;
    return;
  }

  // Parse PHOTO:lux:width:height:quality
  int lux = 0, width = 0, height = 0, quality = 0;
  if (sscanf(command, "PHOTO:%d:%d:%d:%d", &lux, &width, &height, &quality) != 4) {
    Serial.println("[ERROR] Invalid command format");
    MasterSerial.println("ERROR:Invalid format");
    commandInProgress = false;
    return;
  }

  Serial.printf("[PARSE] LUX=%d, Size=%dx%d, Quality=%d\n", lux, width, height, quality);

  if (width < 320 || width > 1600 || height < 240 || height > 1200) {
    Serial.println("[ERROR] Invalid resolution");
    MasterSerial.println("ERROR:Invalid resolution");
    commandInProgress = false;
    return;
  }
  if (quality < 1 || quality > 63) {
    Serial.println("[ERROR] Invalid quality (1-63)");
    MasterSerial.println("ERROR:Invalid quality");
    commandInProgress = false;
    return;
  }

  if (!captureAndSendPhoto((uint16_t)lux, (uint16_t)width, (uint16_t)height, (uint8_t)quality)) {
    MasterSerial.println("ERROR:Capture failed");
  }

  // Flush any remaining bytes in the RX buffer that arrived while we were busy
  while (MasterSerial.available()) {
    MasterSerial.read();
  }

  commandInProgress = false;
}

// ==================== LOOP ====================
void loop() {
  // Initialize UART on first loop() call - safe in loop context
  if (!uartInitialized) {
    Serial.println("\n[UART] Initializing Master Serial (UART2) in loop()...");

    // Start UART
    MasterSerial.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    delay(200);

    // Clear any garbage
    while (MasterSerial.available()) {
      MasterSerial.read();
    }

    Serial.printf("[UART] Initialized on RX:%d TX:%d @ %d baud\n",
                  UART_RX_PIN, UART_TX_PIN, UART_BAUD_RATE);

    delay(300);

    // Send READY signal
    MasterSerial.print("<READY>\n");
    MasterSerial.flush();
    delay(150);
    MasterSerial.print("<READY>\n");
    MasterSerial.flush();
    Serial.println("[UART] Sent READY signal to master");

    // Initialize command buffer
    commandBufferPos = 0;
    lastCommandTime = millis();

    // Mark UART ready
    uartReady = true;
    uartInitialized = true;

    Serial.println("[STATUS] Waiting for commands...\n");
    delay(500);
    return;
  }

  // Normal loop operation
  if (!uartReady) {
    delay(100);
    return;
  }

  // Sanity check on buffer position
  if (commandBufferPos < 0 || commandBufferPos >= CMD_BUFFER_SIZE) {
    commandBufferPos = 0;
  }

  // Check UART for incoming data (non-blocking)
  int avail = MasterSerial.available();
  if (avail <= 0) {
    delay(10);
    return;
  }

  // Read bytes available, but limit to prevent overflow
  while (avail > 0 && commandBufferPos < (CMD_BUFFER_SIZE - 1)) {
    int inByte = MasterSerial.read();
    avail--;

    if (inByte < 0)
      break;

    char c = (char)inByte;

    if (c == '\n' || c == '\r') {
      if (commandBufferPos > 0) {
        commandBuffer[commandBufferPos] = '\0';
        Serial.printf("[UART] Received %d bytes: %s\n", commandBufferPos, commandBuffer);
        processCommand(commandBuffer);
        commandBufferPos = 0;
      }
    } else if (c >= 32 && c <= 126) {
      commandBuffer[commandBufferPos] = c;
      commandBufferPos++;
    }
  }

  delay(2);
}
