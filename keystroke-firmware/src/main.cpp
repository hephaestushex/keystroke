#include <Arduino.h>

#define BUILTIN_LED 25

void setup() {
  // put your setup code here, to run once:
  pinMode(BUILTIN_LED, GPIO_OUT);
}

void loop() {
  // put your main code here, to run repeatedly:
  digitalWrite(BUILTIN_LED, HIGH);
  delay(1000);
  digitalWrite(BUILTIN_LED, LOW);
  delay(1000);
}
