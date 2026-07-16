#include <Arduino.h>
#include "TM1637Display.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
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
  TM1637Display display(26, 27, 60);
  display.show(1, '1');
  display.show(2, '2');
  display.show(3, '3');
  display.show(4, '4');
}
