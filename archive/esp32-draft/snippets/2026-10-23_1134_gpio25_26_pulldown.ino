void setup() {
  Serial.begin(9600);
  pinMode(25, INPUT_PULLDOWN);
  pinMode(26, INPUT_PULLDOWN);
}

void loop() {
  Serial.print("25: ");
  Serial.print(digitalRead(25));
  Serial.print("  26: ");
  Serial.println(digitalRead(26));
  delay(500);
}
