/**
 * @file      i2cscan.ino
 * @brief     I2C scanner for ESP32 — discovers all devices on the I2C bus
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include <Arduino.h>
#include <Wire.h>

// LilyGo A7670 I2C pins
#define I2C_SDA 21
#define I2C_SCL 22

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n[I2C SCAN] Starting I2C device scanner...");
  Serial.println("[I2C SCAN] Initializing I2C bus...");

  Wire.begin(I2C_SDA, I2C_SCL);

  Serial.println("[I2C SCAN] Scanning for devices...\n");
}

void loop() {
  byte error, address;
  int nDevices = 0;

  Serial.println("[I2C SCAN] =============== Device Scan ===============");

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("[I2C SCAN] Device found at address 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.print(address, HEX);
      Serial.println(" !");
      nDevices++;
    } else if (error == 4) {
      Serial.print("[I2C SCAN] Unknown error at address 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
    }
  }

  Serial.print("[I2C SCAN] Found ");
  Serial.print(nDevices);
  Serial.println(" device(s)");
  Serial.println("[I2C SCAN] ==========================================\n");

  delay(5000); // Wait 5 seconds before next scan
}
