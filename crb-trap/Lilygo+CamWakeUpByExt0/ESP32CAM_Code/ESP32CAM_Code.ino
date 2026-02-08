/**
 * @file      ESP32CAM_Code.ino
 * @brief     Simple LED blink test for ESP32-CAM module
 * @date      2025-02-08
 * @note      This sketch blinks the camera flash LED (GPIO 4) to verify
 *            the ESP32-CAM is receiving power from the LILYGO board.
 *            No serial output - pure visual feedback only.
 *
 * Hardware:
 *  - ESP32-CAM module
 *  - Flash LED on GPIO 4
 *
 * Behavior:
 *  - LED turns ON for 500ms
 *  - LED turns OFF for 500ms
 *  - Repeats indefinitely while powered
 */

#define FLASH_LED_PIN 4 // Camera flash LED on GPIO 4

void setup() {
  // Configure flash LED as output
  pinMode(FLASH_LED_PIN, OUTPUT);

  // Start with LED off
  digitalWrite(FLASH_LED_PIN, LOW);
}

void loop() {
  // Turn LED ON
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(500); // ON for 500ms

  // Turn LED OFF
  digitalWrite(FLASH_LED_PIN, LOW);
  delay(500); // OFF for 500ms
}
