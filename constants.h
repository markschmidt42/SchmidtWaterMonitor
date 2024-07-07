// Define connections to sensor
#define TRIGPIN 10
#define ECHOPIN 11

#define WATER_SENSOR_PIN_DRAIN     A0
#define WATER_SENSOR_PIN_PUMP      A1
#define WATER_SENSOR_PIN_SUMP_PUMP A2
#define WATER_SENSOR_PIN_RO        A3


#define UPDATE_SENSORS_EVERY_SECONDS 5

#define WET_VALUE_THRESHOLD 5

// our Tank typically starts filling at the 240 mark (120 mark on one of the tanks)
#define TANK_LEVEL_TOO_LOW_VALUE 230 // give it a 10 gallon buffer before it alerts

// our Tank typically starts filling at the 360 mark (180 mark on one of the tanks)
#define TANK_LEVEL_TOO_HIGH_VALUE 370 // give it a 10 gallon buffer before it alerts
