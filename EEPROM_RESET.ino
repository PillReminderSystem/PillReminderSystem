#include <EEPROM.h>

#define EEPROM_SIZE 512  // Adjust the EEPROM size if necessary

void setup() {
  Serial.begin(115200);
  
  EEPROM.begin(EEPROM_SIZE);  // Initialize EEPROM with size
  
  // Manually write 0 to each EEPROM address
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();            // Commit the changes
  
  Serial.println("EEPROM Cleared!");
  
  delay(2000); // Wait for 2 seconds
  ESP.restart();  // Restart the ESP8266 to ensure a fresh start
}

void loop() {
  // Nothing to do in the loop
}
