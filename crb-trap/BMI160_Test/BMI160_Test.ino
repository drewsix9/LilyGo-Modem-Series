#include <BMI160Gen.h>
#include <Wire.h>

const int i2c_addr = 0x68;

// T-A7670X I2C pins
#define SDA_PIN 21
#define SCL_PIN 22

void setup() {
  Serial.begin(115200); // initialize Serial communication
  delay(2000);          // wait for the serial port to open

  Serial.println("\n\nBMI160 I2C Test using BMI160Gen Library");
  Serial.println("=========================================");

  // Initialize I2C with correct pins for T-A7670X
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000); // Set I2C clock to 400kHz

  // initialize device in I2C mode
  Serial.println("Initializing BMI160 in I2C mode...");
  BMI160.begin(BMI160GenClass::I2C_MODE, i2c_addr);

  // Set the accelerometer range to 250 degrees/second for gyro
  BMI160.setGyroRange(250);

  Serial.println("BMI160 initialized successfully!");
  Serial.println("Reading Accelerometer and Gyroscope data...\n");
}

void loop() {
  int aix, aiy, aiz; // raw accel values
  int gx, gy, gz;    // raw gyro values

  // read raw accel measurements from device
  BMI160.readAcceleration(aix, aiy, aiz);

  // read raw gyro measurements from device
  BMI160.readGyro(gx, gy, gz);

  // display tab-separated accel x/y/z values
  Serial.print("Accel:\t");
  Serial.print(aix);
  Serial.print("\t");
  Serial.print(aiy);
  Serial.print("\t");
  Serial.print(aiz);

  // display tab-separated gyro x/y/z values
  Serial.print("\tGyro:\t");
  Serial.print(gx);
  Serial.print("\t");
  Serial.print(gy);
  Serial.print("\t");
  Serial.println(gz);

  delay(500);
}
