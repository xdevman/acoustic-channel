#include <Wire.h>

void setup() {
  Serial.begin(9600);
  Wire.begin(25, 26); // SDA, SCL
  delay(1000);
}

void loop() {
  int n = 0;
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found 0x");
      Serial.println(a, HEX);
      n++;
    }
  }
  if (n == 0) Serial.println("Nothing found");
  delay(2000);
}
