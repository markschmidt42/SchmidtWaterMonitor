#include "AutomaticBacklight.h"
#include <Arduino.h>

AutomaticBacklight::AutomaticBacklight(LiquidCrystal_I2C* lcdPtr, int pirPin, unsigned long timeoutInSeconds)
  : lcd(lcdPtr), motionSensorPin(pirPin), timeout(timeoutInSeconds * 1000), lastMotionTime(0), backlightOn(false) {
  pinMode(motionSensorPin, INPUT);
}

void AutomaticBacklight::turnOn() {
  lcd->backlight();
  backlightOn = true;
  lastMotionTime = millis();
}

void AutomaticBacklight::turnOff() {
  lcd->noBacklight();
  backlightOn = false;
}

void AutomaticBacklight::update() {
  int motionState = digitalRead(motionSensorPin);
  Serial.print("motionState: ");
  Serial.print(motionState);
  Serial.print(",  lastMotion: ");
  Serial.println(millis() - lastMotionTime);
  if (motionState == HIGH) {
    turnOn();
  } else if (backlightOn && (millis() - lastMotionTime > timeout)) {
    turnOff();
  }
}