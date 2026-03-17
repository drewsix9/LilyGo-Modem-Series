#include "sensor.h"
#include <Arduino.h>
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n[BMI160 TEST] Starting BMI160 unit test...");

  Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);

  if (!initSensors()) {
    Serial.println("[BMI160 TEST] Sensor initialization failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("[BMI160 TEST] Sensor initialized");
  Serial.println("[BMI160 TEST] Connect ONLY BMI160 for this test");
}

void loop() {
  Bmi160Reading reading;

  if (readBmi160Raw(reading)) {
    Serial.println((detectFallFromBmi160(reading)) ? "FALL DETECTED: " : "No fall: ");
  } else {
    Serial.println("[BMI160 TEST] Read failed");
  }

  delay(10);
}
