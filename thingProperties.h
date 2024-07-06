#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>
#include "arduino_secrets.h"

const char SSID[]     = WIFI_SSID;
const char PASS[]     = WIFI_PASS;

float tank_distance_from_top_mm;
float tank_level_gallons;
float tank_level_percent;
float tank_flow_rate_gpm;

float water_sensor_drain;

const int READ_EVERY_SECONDS = 10;

void initProperties() {
  ArduinoCloud.setThingId(THING_ID);
  ArduinoCloud.addProperty(tank_distance_from_top_mm, READ, READ_EVERY_SECONDS * SECONDS, NULL);
  ArduinoCloud.addProperty(tank_level_gallons,        READ, READ_EVERY_SECONDS * SECONDS, NULL);
  ArduinoCloud.addProperty(tank_level_percent,        READ, READ_EVERY_SECONDS * SECONDS, NULL);
  ArduinoCloud.addProperty(tank_flow_rate_gpm,        READ, READ_EVERY_SECONDS * SECONDS, NULL);

  ArduinoCloud.addProperty(water_sensor_drain,        READ, READ_EVERY_SECONDS * SECONDS, NULL);
}

WiFiConnectionHandler ArduinoIoTPreferredConnection(WIFI_SSID, WIFI_PASS);