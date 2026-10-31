void setup() {
  Serial.begin(9600);
}

void loop() {
  pinMode(25, OUTPUT); pinMode(26, OUTPUT);
  pinMode(27, INPUT_PULLDOWN);
  digitalWrite(25, HIGH); digitalWrite(26, LOW); delay(50);
  int a = digitalRead(27);
  digitalWrite(25, LOW); digitalWrite(26, HIGH); delay(50);
  int b = digitalRead(27);
  pinMode(25, INPUT); pinMode(26, INPUT);
  pinMode(27, INPUT_PULLDOWN); delay(50);
  int c = digitalRead(27);
  pinMode(27, INPUT_PULLUP); delay(50);
  int d = digitalRead(27);
  Serial.printf("a=%d b=%d c=%d d=%d\n", a, b, c, d);
  delay(1000);
}
