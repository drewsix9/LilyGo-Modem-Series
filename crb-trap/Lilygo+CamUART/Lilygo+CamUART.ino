/**
 * @file      Lilygo+CamUART.ino
 * @author    Modified for UART camera control with LUX sensor
 * @license   MIT
 * @copyright Copyright (c) 2026
 * @date      2026-02-10
 * @note      LilyGo A7670 Master - Controls ESP32CAM via UART with light-adaptive flash
 *
 * Hardware Setup:
 *  - LILYGO_T_A7670 board (ESP32 Master)
 *  - PIR sensor output → GPIO32 (wakeup trigger)
 *  - GPIO23 → ESP32CAM power enable (LOW = ON, HIGH = OFF - inverted logic for MOSFET driver)
 *  - GPIO18 (TX) → ESP32CAM GPIO16 (RX)
 *  - GPIO19 (RX) → ESP32CAM GPIO13 (TX)
 *  - Light sensor (e.g., BH1750) on I2C bus for LUX reading
 *
 * Communication Protocol:
 *  Command: "PHOTO:lux:width:height:quality\n"
 *  Example: "PHOTO:500:1600:1200:10\n"
 *  Responses: "READY\n", "OK:filename.jpg\n", "ERROR:msg\n"
 *
 * CRITICAL NOTE:
 *  GPIO1/GPIO3 are used by Serial (USB) and CANNOT be used for HardwareSerial!
 *  Using UART2 (GPIO18/19) instead to avoid boot loops.
 *
 * Workflow:
 *  1. PIR detects motion → ESP32 wakes from deep sleep
 *  2. Powers ON ESP32CAM via GPIO17 (LOW = ON due to inverted logic)
 *  3. Reads LUX value from light sensor
 *  4. Waits for "READY" from camera
 *  5. Sends PHOTO command with parameters
 *  6. Waits for "OK" confirmation
 *  7. Powers OFF camera
 *  8. Returns to deep sleep
 */

#include "utilities.h"
#include <Wire.h>
#include <driver/gpio.h>

// ==================== PIN DEFINITIONS ====================
#define PIR_SENSOR_PIN 32 // PIR sensor wakeup source
#define CAM_PWR_EN_PIN 23 // Camera power enable (LOW = ON, HIGH = OFF - inverted logic) - GPIO23
#define UART_TX_PIN 18    // Free GPIO for TX to camera (avoid GPIO1,16,17)
#define UART_RX_PIN 19    // Free GPIO for RX from camera (avoid GPIO3,16,17)

// ==================== I2C LIGHT SENSOR ====================
// If using BH1750 or similar, uncomment and configure:
// #include <BH1750.h>
// BH1750 lightMeter;
#define I2C_SDA 21 // Default I2C SDA
#define I2C_SCL 22 // Default I2C SCL

// ==================== UART CONFIGURATION ====================
#define UART_BAUD_RATE 57600
#define UART_RX_BUFFER_SIZE 16384 // 16KB RX buffer - prevents overflow during photo reception
#define UART_TIMEOUT_MS 10000     // Timeout waiting for camera response (increased for boot time)
#define CAM_BOOT_TIME_MS 2500     // Initial power-on delay (camera needs ~2s to boot and send READY)

// ==================== PHOTO PARAMETERS ====================
#define PHOTO_WIDTH 1600  // Image width in pixels
#define PHOTO_HEIGHT 1200 // Image height in pixels
#define PHOTO_QUALITY 10  // JPEG quality (1-63, lower = higher quality)

// ==================== TIMING ====================
#define CAMERA_ON_DURATION_MS 15000 // Max time camera stays on (15 seconds)
#define PIR_SETTLE_TIME_MS 2000     // PIR sensor settle time
#define PHOTO_MAX_RETRIES 3         // Retry photo on CRC failure (UART bit errors at 115200 baud)

// UART Serial for camera communication
HardwareSerial CameraSerial(2); // Use UART2 (GPIO16/17)

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

// ==================== FUNCTION PROTOTYPES ====================
void print_wakeup_reason();
uint16_t readLuxSensor();
bool waitForCameraReady();
bool sendPhotoCommand(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality);
String readCameraResponse(uint32_t timeout_ms);
bool receivePhotoWithChecksum();
void uartToCameraEnable();
void uartToCameraDisable();
void powerOnCamera();
void powerOffCamera();
void enterDeepSleep();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200); // Debug console
  delay(100);

  Serial.println("\n===========================================");
  Serial.println("LILYGO A7670 - Camera UART Master");
  Serial.println("===========================================");
  Serial.println("[PINS] GPIO18(TX)->CAM_RX, GPIO19(RX)->CAM_TX");
  Serial.println("[PINS] GPIO23->CAM_POWER (LOW=ON), GPIO32->PIR_WAKEUP");

  // Print wakeup reason
  print_wakeup_reason();

  // Release GPIO hold from deep sleep (if it was held)
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)CAM_PWR_EN_PIN);

  // Initialize camera power pin with MAXIMUM drive strength
  pinMode(CAM_PWR_EN_PIN, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)CAM_PWR_EN_PIN, GPIO_DRIVE_CAP_3); // Maximum 40mA
  digitalWrite(CAM_PWR_EN_PIN, HIGH);                                      // Start with camera OFF (inverted logic)

  Serial.println("[GPIO] GPIO23 configured with maximum drive strength");

  // Initialize I2C for light sensor
  Wire.begin(I2C_SDA, I2C_SCL);

  // Uncomment if using BH1750 or similar I2C light sensor:
  // if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
  //   Serial.println("[I2C] Light sensor initialized");
  // } else {
  //   Serial.println("[I2C] ERROR: Light sensor not found!");
  // }

  // Initialize UART to camera with large RX buffer to prevent overflow during photo reception
  CameraSerial.setRxBufferSize(UART_RX_BUFFER_SIZE);
  CameraSerial.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  CameraSerial.setTimeout(5000); // 5s per-byte timeout for readBytes() during binary photo reception
  Serial.printf("[UART] Initialized on TX:%d RX:%d @ %d baud (RX buffer: %d bytes)\n",
                UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE, UART_RX_BUFFER_SIZE);

  // ==================== HANDLE WAKEUP EVENT ====================
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("\n[EVENT] Motion detected by PIR sensor!");

    // Read light intensity
    uint16_t luxValue = readLuxSensor();
    Serial.printf("[SENSOR] Current light intensity: %d LUX\n", luxValue);

    // Power ON camera
    powerOnCamera();

    // Wait for camera to start booting (reduced delay, UART sends status during init)
    Serial.printf("[WAIT] Waiting %d ms for camera power-on...\n", CAM_BOOT_TIME_MS);
    delay(CAM_BOOT_TIME_MS);

    // Wait for camera READY signal (camera needs ~3-4 seconds to init SD+Camera)
    Serial.println("[UART] Waiting for camera READY signal...");
    if (waitForCameraReady()) {
      Serial.println("[UART] Camera is READY");

      // Attempt photo capture with retry on CRC failure (UART bit errors)
      bool photoSuccess = false;
      for (int attempt = 1; attempt <= PHOTO_MAX_RETRIES && !photoSuccess; attempt++) {
        if (attempt > 1) {
          Serial.printf("\n[RETRY] Attempt %d/%d - flushing buffer and re-sending command...\n", attempt, PHOTO_MAX_RETRIES);
          // Drain leftover data from previous failed attempt (e.g. OK:PHOTO_SENT)
          delay(500);
          while (CameraSerial.available())
            CameraSerial.read();
          delay(200);
        }

        bool success = sendPhotoCommand(luxValue, PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY);
        if (success) {
          Serial.println("[SUCCESS] Photo command sent, receiving photo...");
          photoSuccess = receivePhotoWithChecksum();
          if (photoSuccess) {
            Serial.printf("[SUCCESS] Photo received and verified! (attempt %d/%d)\n", attempt, PHOTO_MAX_RETRIES);
          } else {
            Serial.printf("[ERROR] Photo reception failed on attempt %d/%d\n", attempt, PHOTO_MAX_RETRIES);
          }
        } else {
          Serial.println("[ERROR] Photo command transmission failed!");
        }
      }

      if (!photoSuccess) {
        Serial.printf("[FAIL] All %d photo attempts failed!\n", PHOTO_MAX_RETRIES);
      }

    } else {
      Serial.println("[ERROR] Camera READY timeout!");
    }

    // Keep camera on a bit longer for any final operations
    delay(1000);

    // Power OFF camera
    powerOffCamera();

  } else {
    // First boot or reset - just test the hardware
    Serial.println("\n[BOOT] First boot or reset detected");
    Serial.println("[TEST] Testing camera power control...");

    // Quick power test
    powerOnCamera();
    delay(1000);
    powerOffCamera();

    Serial.println("[TEST] Power control test completed");
  }

  // ==================== PREPARE FOR DEEP SLEEP ====================
  // Configure PIR sensor pin for wakeup
  pinMode(PIR_SENSOR_PIN, INPUT_PULLDOWN);
  gpio_pulldown_en((gpio_num_t)PIR_SENSOR_PIN);

  Serial.println("\n[CONFIG] System configuration:");
  Serial.println("  - Wakeup source: GPIO32 (PIR Sensor)");
  Serial.println("  - Camera power: GPIO23 (LOW = ON, HIGH = OFF - inverted logic)");
  Serial.println("  - UART: GPIO18(TX) → CAM_GPIO16(RX), GPIO19(RX) ← CAM_GPIO13(TX)");
  Serial.printf("  - Photo size: %dx%d, Quality: %d\n",
                PHOTO_WIDTH, PHOTO_HEIGHT, PHOTO_QUALITY);
  Serial.printf("  - UART timeout: %d ms (allows ~%ds for camera init)\n",
                UART_TIMEOUT_MS, UART_TIMEOUT_MS / 1000);

  // Enter deep sleep
  enterDeepSleep();
}

void loop() {
  // Never reached - ESP32 goes to deep sleep in setup()
}

// ==================== FUNCTION IMPLEMENTATIONS ====================

/**
 * @brief Print the reason for ESP32 wakeup
 */
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
  case ESP_SLEEP_WAKEUP_EXT0:
    Serial.println("[WAKEUP] External signal (PIR Sensor) via RTC_IO");
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    Serial.println("[WAKEUP] External signal via RTC_CNTL");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.println("[WAKEUP] Timer");
    break;
  default:
    Serial.printf("[WAKEUP] Not from deep sleep (reason: %d)\n", wakeup_reason);
    break;
  }
}

/**
 * @brief Read light intensity from LUX sensor
 * @return LUX value (0-65535)
 * @note Replace with actual sensor reading code
 */
uint16_t readLuxSensor() {
  // TODO: Replace with actual light sensor reading
  // Example for BH1750:
  // float lux = lightMeter.readLightLevel();
  // return (uint16_t)lux;

  // For testing, return a simulated value based on time
  // In production, replace this with real sensor reading
  uint16_t simulatedLux = 500; // Medium brightness

  // Uncomment when BH1750 is connected:
  // if (lightMeter.measurementReady()) {
  //   simulatedLux = (uint16_t)lightMeter.readLightLevel();
  // }

  return simulatedLux;
}

/**
 * @brief Enable UART to camera and re-initialize the connection
 * @note Prevents ghost power issues when camera is powered on
 */
void uartToCameraEnable() {
  Serial.println("[UART] Enabling UART connection...");
  CameraSerial.end();
  delay(100);
  pinMode(UART_TX_PIN, OUTPUT);
  pinMode(UART_RX_PIN, INPUT);
  CameraSerial.setRxBufferSize(UART_RX_BUFFER_SIZE); // Ensure large RX buffer persists after end()/begin()
  CameraSerial.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  CameraSerial.setTimeout(5000); // 5s per-byte timeout for readBytes()
  delay(50);

  // CRITICAL: Flush any garbage bytes from RX buffer
  while (CameraSerial.available()) {
    CameraSerial.read();
  }

  Serial.println("[UART] UART re-initialized and RX buffer flushed");
}

/**
 * @brief Disable UART to camera and float pins to prevent backfeeding
 * @note CRITICAL: Prevents ghost power through UART protection diodes when camera is off
 */
void uartToCameraDisable() {
  Serial.println("[UART] Disabling UART connection (floating pins)...");
  CameraSerial.flush();
  CameraSerial.end();
  delay(100);
  pinMode(UART_TX_PIN, INPUT); // high-Z input - prevents backfeeding
  pinMode(UART_RX_PIN, INPUT); // high-Z input - prevents backfeeding
  Serial.println("[UART] UART disabled and pins floating (no ghost power)");
}

/**
 * @brief Power ON the camera
 */
void powerOnCamera() {
  Serial.println("[POWER] Turning camera ON (GPIO23 → LOW - inverted logic)");
  digitalWrite(CAM_PWR_EN_PIN, LOW); // Inverted logic: LOW = ON
  delay(100);                        // Brief stabilization delay
  uartToCameraEnable();              // Re-enable UART for communication
  Serial.println("[STATUS] Camera powered ON");
}

/**
 * @brief Power OFF the camera
 */
void powerOffCamera() {
  Serial.println("[POWER] Turning camera OFF (GPIO23 → HIGH - inverted logic)");
  digitalWrite(CAM_PWR_EN_PIN, HIGH); // Inverted logic: HIGH = OFF
  delay(100);

  // CRITICAL: Disable UART BEFORE holding GPIO to prevent backfeeding
  uartToCameraDisable(); // Float UART pins (prevents ghost power through protection diodes)

  // Hold GPIO23 state during deep sleep
  gpio_hold_en((gpio_num_t)CAM_PWR_EN_PIN);
  gpio_deep_sleep_hold_en();

  Serial.println("[STATUS] Camera powered OFF and isolated");
}

/**
 * @brief Wait for "READY" message from camera (searches for substring, handles garbage bytes)
 * @return true if READY received, false on timeout
 */
bool waitForCameraReady() {
  // Flush any remaining garbage bytes before waiting
  while (CameraSerial.available()) {
    CameraSerial.read();
  }
  delay(100);

  uint32_t startTime = millis();
  String buffer = "";

  Serial.println("[UART] Waiting for READY (searching for substring)...");

  while (millis() - startTime < UART_TIMEOUT_MS) {
    while (CameraSerial.available()) {
      char c = CameraSerial.read();

      // Only keep printable ASCII characters (filters out garbage bytes)
      if (c >= 32 && c <= 126) {
        buffer += c;
      }

      // Check if READY is present anywhere in the buffer
      if (buffer.indexOf("READY") != -1) {
        Serial.printf("[SUCCESS] READY detected in buffer: %s\n", buffer.c_str());

        // Critical: Flush ENTIRE buffer after READY found (consume remaining READY messages)
        delay(200);
        while (CameraSerial.available()) {
          CameraSerial.read();
        }

        return true;
      }

      // Prevent buffer overflow - keep only last 64 characters
      if (buffer.length() > 64) {
        buffer = buffer.substring(buffer.length() - 64);
      }
    }
    delay(10);
  }

  Serial.printf("[TIMEOUT] No READY received after %dms (buffer: %s)\n", UART_TIMEOUT_MS, buffer.c_str());
  return false;
}

/**
 * @brief Send photo command to camera with parameters
 * @param lux Light intensity value for flash adjustment
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param quality JPEG quality (1-63, lower = better)
 * @return true if OK received, false otherwise
 */
bool sendPhotoCommand(uint16_t lux, uint16_t width, uint16_t height, uint8_t quality) {
  // Critical: Flush RX buffer completely - consume all lingering READY data
  delay(200);
  while (CameraSerial.available()) {
    CameraSerial.read();
  }
  delay(100);

  // Build command string
  char command[64];
  snprintf(command, sizeof(command), "PHOTO:%d:%d:%d:%d\n",
           lux, width, height, quality);

  Serial.printf("[UART] Sending command: %s", command);
  CameraSerial.print(command);
  CameraSerial.flush();
  delay(100); // Brief delay for slave to parse command (photo capture takes time anyway)

  Serial.println("[UART] Command sent, slave should begin photo transmission");
  return true;
}

bool receivePhotoWithChecksum() {
  Serial.println("[PHOTO] Waiting for photo transmission...");
  // No delay here - start reading immediately to avoid missing early bytes

  uint32_t startTime = millis();
  String buffer = "";
  uint32_t photoSize = 0;
  uint32_t receivedCRC = 0;
  uint8_t *photoData = nullptr;
  uint32_t bytesReceived = 0;
  uint32_t lastDataTime = millis();
  uint32_t lastProgressLog = 0; // Track progress logging threshold (local, resets on retry)

  // Timeout: 60s absolute, 15s with no data after receiving started
  const uint32_t ABSOLUTE_TIMEOUT_MS = 60000;
  const uint32_t NO_DATA_TIMEOUT_MS = 15000;

  enum State {
    WAITING_START,
    READING_SIZE,
    READING_DATA,
    READING_CRC,
    WAITING_END
  };
  State state = WAITING_START;

  while (millis() - startTime < ABSOLUTE_TIMEOUT_MS) {
    // Check "no data" timeout - if we've started receiving but data stops flowing
    if (state >= READING_DATA && (millis() - lastDataTime > NO_DATA_TIMEOUT_MS)) {
      Serial.printf("[ERROR] No data received for %dms, aborting\n", NO_DATA_TIMEOUT_MS);
      break;
    }

    // Inner loop: Read ALL available data without delay (CRITICAL for binary data)
    bool hadData = false;

    // Use bulk read for READING_DATA state to minimize per-byte overhead
    if (state == READING_DATA && photoData) {
      int avail = CameraSerial.available();
      if (avail > 0) {
        hadData = true;
        lastDataTime = millis();

        // Read as many bytes as possible in bulk (up to remaining needed)
        uint32_t remaining = photoSize - bytesReceived;
        uint32_t toRead = (avail < (int)remaining) ? avail : remaining;
        size_t actualRead = CameraSerial.readBytes(photoData + bytesReceived, toRead);
        bytesReceived += actualRead;

        // Log progress every 10000 bytes (reduced frequency to minimize Serial blocking)
        if (bytesReceived / 10000 > lastProgressLog) {
          lastProgressLog = bytesReceived / 10000;
          Serial.printf("[PHOTO] Progress: %u/%u bytes received (%d%%) at %ldms\n",
                        bytesReceived, photoSize,
                        (int)(bytesReceived * 100 / photoSize),
                        millis() - startTime);
        }

        if (bytesReceived >= photoSize) {
          Serial.printf("[PHOTO] Received %u bytes of JPEG data (total time: %ldms)\n",
                        bytesReceived, millis() - startTime);
          state = READING_CRC;
          buffer = "";
        }
      }
    } else {
      // For non-data states, read byte-by-byte to parse protocol markers
      while (CameraSerial.available()) {
        hadData = true;
        lastDataTime = millis();
        char c = CameraSerial.read();

        if (state == WAITING_START) {
          if (c >= 32 && c <= 126) {
            buffer += c;
          }

          if (buffer.indexOf("<PHOTO_START>") != -1) {
            Serial.println("[PHOTO] Start marker detected");
            state = READING_SIZE;
            buffer = "";
          }

          // Prevent buffer overflow
          if (buffer.length() > 64) {
            buffer = buffer.substring(buffer.length() - 32);
          }
        }

        else if (state == READING_SIZE) {
          if (c == '\n') {
            if (buffer.startsWith("SIZE:")) {
              photoSize = buffer.substring(5).toInt();
              Serial.printf("[DEBUG] SIZE buffer: '%s', parsed size: %u bytes\n", buffer.c_str(), photoSize);
              if (photoSize > 0 && photoSize < 500000) { // Max 500KB
                Serial.printf("[PHOTO] Size: %u bytes\n", photoSize);
                photoData = (uint8_t *)malloc(photoSize);
                if (photoData) {
                  state = READING_DATA;
                  bytesReceived = 0;
                  buffer = "";
                  break; // Exit inner loop to use bulk read path above
                } else {
                  Serial.println("[ERROR] Memory allocation failed");
                  return false;
                }
              } else {
                Serial.printf("[ERROR] Invalid photo size: %u\n", photoSize);
                return false;
              }
            }
          } else if (c >= 32 && c <= 126) {
            buffer += c;
          }
        }

        else if (state == READING_CRC) {
          if (c == '\n') {
            if (buffer.startsWith("CRC32:")) {
              char crcStr[20];
              buffer.substring(6).toCharArray(crcStr, sizeof(crcStr));
              receivedCRC = strtoul(crcStr, nullptr, 16);
              Serial.printf("[PHOTO] Received CRC32: %08X\n", receivedCRC);
              state = WAITING_END;
              buffer = "";
            }
          } else if (c >= 32 && c <= 126) {
            buffer += c;
          }
        }

        else if (state == WAITING_END) {
          if (c >= 32 && c <= 126) {
            buffer += c;
          }

          if (buffer.indexOf("<PHOTO_END>") != -1) {
            Serial.println("[PHOTO] End marker detected");

            // Verify CRC32
            uint32_t calculatedCRC = calculateCRC32(photoData, photoSize);
            Serial.printf("[PHOTO] Calculated CRC32: %08X\n", calculatedCRC);

            if (calculatedCRC == receivedCRC) {
              Serial.println("[PHOTO] CRC32 VERIFIED - Photo is intact!");
              Serial.printf("[PHOTO] SUCCESS: Received %u bytes, CRC32 match\n", photoSize);
              free(photoData);
              return true;
            } else {
              Serial.printf("[ERROR] CRC32 mismatch! Got %08X, expected %08X\n", calculatedCRC, receivedCRC);
              free(photoData);
              return false;
            }
          }
        }
      }
    }

    // Only yield briefly if no data was available, to avoid busy-looping the CPU
    // Use yield() instead of delay(1) to minimize latency while still feeding the watchdog
    if (!hadData) {
      yield();                // Feed watchdog without long delay
      delayMicroseconds(100); // Sub-millisecond wait to prevent tight CPU spin
    }
  }

  Serial.println("[ERROR] Photo reception timeout");
  Serial.printf("[DEBUG] Timeout details: state=%d, bytesReceived=%u, photoSize=%u, elapsed=%ldms, lastDataAt=%ldms\n",
                (int)state, bytesReceived, photoSize, millis() - startTime, millis() - lastDataTime);
  if (photoData)
    free(photoData);
  return false;
}

/**
 * @brief Read response from camera with timeout
 * @param timeout_ms Timeout in milliseconds
 * @return Response string (empty if timeout)
 */
String readCameraResponse(uint32_t timeout_ms) {
  uint32_t startTime = millis();
  String response = "";

  while (millis() - startTime < timeout_ms) {
    if (CameraSerial.available()) {
      char c = CameraSerial.read();
      response += c;

      // Check for complete response (ends with newline)
      if (c == '\n') {
        return response;
      }

      // Prevent overflow
      if (response.length() > 200) {
        break;
      }
    }
    delay(10);
  }

  return response;
}

/**
 * @brief Enter ESP32 deep sleep mode and wait for PIR trigger
 */
void enterDeepSleep() {
  Serial.println("\n[WAIT] PIR sensor settling...");
  delay(PIR_SETTLE_TIME_MS);

  // CRITICAL: Ensure camera stays OFF during deep sleep
  // Set GPIO23 HIGH (camera OFF) and hold the state
  digitalWrite(CAM_PWR_EN_PIN, HIGH);

  // Try to hold GPIO state during deep sleep
  // Hardware solution recommended: Add 10kΩ pull-up resistor from GPIO23 to 3.3V
  gpio_hold_en((gpio_num_t)CAM_PWR_EN_PIN);
  gpio_deep_sleep_hold_en();

  Serial.println("[GPIO] Camera power pin held HIGH (OFF) for deep sleep");

  Serial.println("[SLEEP] Entering deep sleep mode...");
  Serial.println("         Waiting for PIR motion detection...");
  Serial.println("===========================================\n");
  Serial.flush();

  // Enable wakeup on GPIO32 (PIR sensor HIGH)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_SENSOR_PIN, 1);

  delay(200);
  esp_deep_sleep_start();
}
