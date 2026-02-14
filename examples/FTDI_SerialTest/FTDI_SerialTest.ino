/*
 * Minimal ESP32-CAM with FTDI Serial Test
 *
 * Connections:
 * FTDI RX  -> ESP32 GPIO1 (TX)
 * FTDI TX  -> ESP32 GPIO3 (RX)
 * FTDI GND -> ESP32 GND
 *
 * Set FTDI jumper to 3.3V
 * Do NOT connect FTDI VCC (ESP32 powered externally)
 */

#define FLASH_LED_PIN 4 // Flash LED on ESP32-CAM

void setup() {
  // Initialize Flash LED pin
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  // Initialize Serial at 115200 baud
  Serial.begin(115200);
  delay(1000); // Give time for serial to initialize

  Serial.println("\n\n=== ESP32-CAM FTDI Test ===");
  Serial.println("Serial communication working!");
  Serial.println("Flash LED should be blinking every second");
  Serial.print("ESP32 Chip ID: ");
  Serial.println((uint32_t)ESP.getEfuseMac(), HEX);
}

void loop() {
  // Blink Flash LED every 1 second
  static unsigned long lastBlinkMillis = 0;
  static bool ledState = false;

  if (millis() - lastBlinkMillis >= 1000) {
    lastBlinkMillis = millis();
    ledState = !ledState;
    digitalWrite(FLASH_LED_PIN, ledState);
  }

  // Send a message every 2 seconds
  static unsigned long lastMillis = 0;
  static int counter = 0;

  if (millis() - lastMillis >= 2000) {
    lastMillis = millis();
    counter++;

    Serial.print("Counter: ");
    Serial.print(counter);
    Serial.print(" | Uptime: ");
    Serial.print(millis() / 1000);
    Serial.println(" seconds");
  }

  // Echo any received data back
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    Serial.print("You sent: ");
    Serial.println(input);
  }
}
