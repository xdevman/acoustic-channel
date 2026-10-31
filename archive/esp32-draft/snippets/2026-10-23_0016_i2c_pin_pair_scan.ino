#include <Wire.h>

int pins[] = {21, 22, 25, 26, 32, 33};
const int N = 6;

void setup() {
  Serial.begin(9600);
  delay(1000);
}

void loop() {
  bool found = false;
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      if (i == j) continue;
      Wire.end();
      Wire.begin(pins[i], pins[j]);
      for (byte a = 0x3C; a <= 0x3D; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
          Serial.print("SDA=");
          Serial.print(pins[i]);
          Serial.print(" SCL=");
          Serial.print(pins[j]);
          Serial.print(" addr=0x");
          Serial.println(a, HEX);
          found = true;
        }
      }
    }
  }
  if (!found) Serial.println("Nothing found");
  delay(3000);
}
