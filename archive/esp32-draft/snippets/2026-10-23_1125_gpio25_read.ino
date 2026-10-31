void setup() {
  Serial.begin(9600);
  pinMode(25, INPUT);
}

void loop() {
  Serial.println(digitalRead(25));
  delay(500);
}
