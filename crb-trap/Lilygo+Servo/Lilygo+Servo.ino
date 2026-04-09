/**
 * @file      Lilygo+Servo.ino
 * @brief     Simple Serial Monitor servo angle controller for LilyGo T-A7670.
 * @license   MIT
 * @copyright Copyright (c) 2026
 */

#include "servo_controller.h"

static const uint32_t SERIAL_BAUD = 115200;
static String inputLine;

static void printHelp() {
  Serial.println("\nCommands:");
  Serial.println("  <angle>        Move servo to 0..180 degrees");
  Serial.println("  read           Print current angle");
  Serial.println("  help           Show this help");
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  Serial.println("\n===========================================");
  Serial.println("LilyGo T-A7670 Servo Serial Controller");
  Serial.println("===========================================");

  if (!ServoController::begin()) {
    Serial.println("[SERVO] ERROR: attach failed");
    Serial.println("Check wiring and pin choice in servo_controller.h");
  } else {
    Serial.printf("[SERVO] Attached on GPIO%d\n", ServoController::pin());
    Serial.printf("[SERVO] Start angle: %u\n", ServoController::readAngle());
  }

  printHelp();
  Serial.println();
  Serial.print("> ");
}

void loop() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      inputLine.trim();

      if (inputLine.length() == 0) {
        Serial.print("> ");
        continue;
      }

      if (inputLine.equalsIgnoreCase("help")) {
        printHelp();
      } else if (inputLine.equalsIgnoreCase("read")) {
        Serial.printf("[SERVO] Angle: %u\n", ServoController::readAngle());
      } else {
        int angle = inputLine.toInt();
        if (angle < 0 || angle > 180) {
          Serial.println("[SERVO] Invalid angle. Use 0..180");
        } else if (!ServoController::writeAngle((uint8_t)angle)) {
          Serial.println("[SERVO] Write failed (servo not attached)");
        } else {
          Serial.printf("[SERVO] Moved to %d\n", angle);
        }
      }

      inputLine = "";
      Serial.print("> ");
      continue;
    }

    if (isPrintable((unsigned char)c)) {
      inputLine += c;
    }
  }
}
