#include "EEPROM.h"
#define EEPROM_SIZE 1

void setup() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(0, 0); // Write 0 to address 0
  EEPROM.commit();
  Serial.begin(115200);
  Serial.println("EEPROM reset to 0");
}
void loop() {}
