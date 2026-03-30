#include <Arduino.h>
#include <Wire.h>
#include <ErriezBH1750.h>

// LilyGo A7670 I2C pins
#define SENSOR_I2C_SDA 21
#define SENSOR_I2C_SCL 22

// ADDR low/open -> 0x23
BH1750 lightSensor(LOW);

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n[BH1750 TEST] Starting BH1750 unit test...");
  Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);

  lightSensor.begin(ModeContinuous, ResolutionLow);
  lightSensor.startConversion();

  Serial.println("[BH1750 TEST] Sensor initialized");
  Serial.println("[BH1750 TEST] Connect ONLY BH1750 for this test");
}

void loop() {
  if (lightSensor.isConversionCompleted()) {
    uint16_t lux = lightSensor.read();
    Serial.printf("[BH1750 TEST] Lux: %u\n", lux);
  } else {
    Serial.println("[BH1750 TEST] Waiting for conversion...");
  }

  delay(500);
}
