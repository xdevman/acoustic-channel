void setup() {
  Serial.begin(9600);
  delay(1000);

  Serial.println("ESP32 is working!");
}

void loop() {
  Serial.println("Uptime: " + String(millis() / 1000) + " seconds");
  delay(1000);
}
