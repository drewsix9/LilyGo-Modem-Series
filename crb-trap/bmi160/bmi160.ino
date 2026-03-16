#include <DFRobot_BMI160.h>
#include <Math.h>
#include <Wire.h>

DFRobot_BMI160 bmi160;
const int8_t i2c_addr = 0x68;

// Variables for orientation calculations
float ax, ay, az;
float gx, gy, gz;
float pitch, roll, yaw;
unsigned long lastTime;
float dt = 0.01; // Time interval in seconds (10 ms)

void setup() {
  Serial.begin(115200);
  delay(100);

  if (bmi160.softReset() != BMI160_OK) {
    Serial.println("reset false");
    while (1)
      ;
  }

  if (bmi160.I2cInit(i2c_addr) != BMI160_OK) {
    Serial.println("init false");
    while (1)
      ;
  }

  lastTime = millis();
}

void loop() {
  unsigned long currentTime = millis();
  dt = (currentTime - lastTime) / 1000.0; // Update dt
  lastTime = currentTime;

  int16_t accelGyro[6] = {0};
  int rslt = bmi160.getAccelGyroData(accelGyro);

  if (rslt == 0) {
    ax = accelGyro[3] / 16384.0;
    ay = accelGyro[4] / 16384.0;
    az = accelGyro[5] / 16384.0;

    gx = accelGyro[0] * 3.14 / 180.0;
    gy = accelGyro[1] * 3.14 / 180.0;
    gz = accelGyro[2] * 3.14 / 180.0;

    // Calculate pitch and roll from accelerometer data
    pitch = atan2(ay, az) * 180 / 3.14;
    roll = atan2(-ax, sqrt(ay * ay + az * az)) * 180 / 3.14;

    // Integrate gyroscope data to get yaw
    yaw += gz * dt * 180 / 3.14;

    // Print pitch, roll, and yaw
    Serial.print(pitch);
    Serial.print(",");
    Serial.print(roll);
    Serial.print(",");
    Serial.print(yaw);
    Serial.print("FALLEN: ");
    Serial.println((fabs(roll) >= 70.0 && fabs(roll) <= 90) ? "NO" : "YES");
  } else {
    Serial.println("err");
  }

  delay(10);
}
