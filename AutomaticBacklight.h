#ifndef AUTOMATIC_BACKLIGHT_H
#define AUTOMATIC_BACKLIGHT_H

#include <LiquidCrystal_I2C.h>

class AutomaticBacklight {
private:
  LiquidCrystal_I2C* lcd;
  int motionSensorPin;
  unsigned long timeout;
  unsigned long lastMotionTime;
  bool backlightOn;

public:
  AutomaticBacklight(LiquidCrystal_I2C* lcdPtr, int pirPin, unsigned long timeoutInSeconds);
  void turnOn();
  void turnOff();
  void update();
  bool isOn();
};

#endif // AUTOMATIC_BACKLIGHT_H