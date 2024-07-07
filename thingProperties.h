#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>
#include "arduino_secrets.h"
#include "constants.h"

float tank_distance_from_top_mm;
float tank_level_gallons;
float tank_level_percent;
float tank_flow_rate_gpm;

int water_sensor_drain;
int water_sensor_pump;
int water_sensor_sump_pump;
int water_sensor_ro;

void initProperties() {
  ArduinoCloud.setThingId(SECRET_THING_ID);
  ArduinoCloud.addProperty(tank_distance_from_top_mm, READ, UPDATE_SENSORS_EVERY_SECONDS * SECONDS, NULL);
  ArduinoCloud.addProperty(tank_level_gallons,        READ, UPDATE_SENSORS_EVERY_SECONDS * SECONDS, NULL);
  ArduinoCloud.addProperty(tank_level_percent,        READ, UPDATE_SENSORS_EVERY_SECONDS * SECONDS, NULL);
  ArduinoCloud.addProperty(tank_flow_rate_gpm,        READ, UPDATE_SENSORS_EVERY_SECONDS * SECONDS, NULL);

  ArduinoCloud.addProperty(water_sensor_drain,        READ, UPDATE_SENSORS_EVERY_SECONDS * SECONDS, NULL);
  ArduinoCloud.addProperty(water_sensor_pump,         READ, UPDATE_SENSORS_EVERY_SECONDS * SECONDS, NULL);
  ArduinoCloud.addProperty(water_sensor_sump_pump,    READ, UPDATE_SENSORS_EVERY_SECONDS * SECONDS, NULL);
  ArduinoCloud.addProperty(water_sensor_ro,           READ, UPDATE_SENSORS_EVERY_SECONDS * SECONDS, NULL);
}

WiFiConnectionHandler ArduinoIoTPreferredConnection(SECRET_WIFI_SSID, SECRET_WIFI_PASS);