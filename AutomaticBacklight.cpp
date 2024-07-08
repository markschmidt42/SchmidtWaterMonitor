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
  bool isMotionState = isMotion();
  Serial.print("isMotionState: ");
  Serial.print(isMotionState);
  Serial.print(",  lastMotion: ");
  Serial.println(millis() - lastMotionTime);
  if (isMotionState) {
    turnOn();
  } else if (backlightOn && (millis() - lastMotionTime > timeout)) {
    turnOff();
  }
}

bool AutomaticBacklight::isMotion() {
  return (digitalRead(motionSensorPin) == HIGH);
}