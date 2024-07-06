#include "arduino_secrets.h"
#include "thingProperties.h"
#include <SPI.h>
#include <WiFi.h>
#include "Arduino_LED_Matrix.h"
#include "Arduino_CloudConnectionFeedback.h"

// Define connections to sensor
#define TRIGPIN 10
#define ECHOPIN 11

#define WATER_SENSOR_PIN_DRAIN     A0
#define WATER_SENSOR_PIN_PUMP      A1
#define WATER_SENSOR_PIN_SUMP_PUMP A2
#define WATER_SENSOR_PIN_RO        A3


// const int SENSORS_READING_DELAY = 1000 * 30; // every 30 seconds (on top of the read delays due to avgs)

// for testing, do it a little faster
const int SENSORS_READING_DELAY = 1000 * 12; // every 12 seconds (on top of the read delays due to avgs)

const float SENSOR_OFFSET_MM = 21; // minor tweak to get the sensor to match real world measurements
const float SENSOR_MIN_RANGE_MM = 250; // unit handles 20 cm (200 mm), but giving it a bit of a buffer
const float INVALID_READING_TOO_CLOSE = -1000;

// How far away is the top of the tank from the sensor?
const float TOP_OF_TANK_MM = 300;

// How big is the tank?
const int TANK_SIZE_IN_GALLONS = 450;

// How many gallons are there per CM?
// Measure the Tank from the 120 to the 180 mark (or larger), so 60 gals, x 2 (for 2 tanks), 120 gallons / distance in CM
const float TANK_GALLONS_PER_CM = 2.57235; // 80 / 31.1

WiFiClient client;

struct TankInfo {
  float distance;
  float level;
  float gallons;
  float flowRate;
  float mmPerSecondRate;
  unsigned long readingTime;
};

TankInfo previousTankInfo = {0, 0, 0, 0, 0, 0};

struct LeakInfo {
  unsigned int drain;
  unsigned int pump;
  unsigned int sumpPump;
  unsigned int ro;
};

ArduinoLEDMatrix matrix;


unsigned long lastUpdate = 0;

void setup() {
  // Set up serial monitor
  Serial.begin(115200);
  delay(1500); 
  Serial.println("Starting up...");

  // Connect to Arduino Cloud
  initProperties();
  /* Initialize Arduino IoT Cloud library */
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  ArduinoCloud.printDebugInfo();
  // This line will block until we're connected to Arduino Cloud
  // using the LED matrix to provide feedback
  matrix.begin();
  waitForArduinoCloudConnection(matrix);
  
  Serial.println("Program started!");

  // Use the LED matrix to do something else
  matrix.loadSequence(LEDMATRIX_ANIMATION_TETRIS_INTRO);
  matrix.play(false);

  setDebugMessageLevel(DBG_INFO);

  initTankLevelSensor();
}

void loop() {
  ArduinoCloud.update();

  // if (millis() > (lastUpdate + SENSORS_READING_DELAY)) {
  TankInfo tankInfo = getTankInfo();
  LeakInfo leakInfo = getLeakInfo();
  lastUpdate = millis(); // we are going to consider this the last update time (so, if it takes time to push it to the cloud, we are ignorning that part)

  float tankLevelInInches = convertToInches(tankInfo.distance);
  String tankLevelFeetAndInches = formatFeetAndInches(tankLevelInInches);

  if (tankInfo.distance == 0) {
    Serial.println("INVALID READING!!!");
  } else {

    // Print result to serial monitor
    Serial.print("Average distance: ");
    Serial.print(tankInfo.distance);
    Serial.print(" mm (");
    Serial.print(tankLevelFeetAndInches);
    Serial.println(")");
  }

  sendToArduinoCloud(tankInfo);
  sendLeakInfoToCloud(leakInfo);

  // Update previousTankInfo
  previousTankInfo = tankInfo;
  // }

  // Delay before repeating measurement
  delay(1000);
}

void initTankLevelSensor() {
  // Set pinmodes for sensor connections
  pinMode(ECHOPIN, INPUT);
  pinMode(TRIGPIN, OUTPUT);
}

void sendToArduinoCloud(TankInfo tankInfo) {
  tank_distance_from_top_mm = tankInfo.distance;
  tank_level_gallons        = tankInfo.gallons;
  tank_level_percent        = tankInfo.level * 100;
  tank_flow_rate_gpm        = tankInfo.flowRate;
}

void sendLeakInfoToCloud(LeakInfo leakInfo) {
  water_sensor_drain = leakInfo.drain;
}


LeakInfo getLeakInfo() {
  LeakInfo leakInfo;
  leakInfo.drain    = analogRead(WATER_SENSOR_PIN_DRAIN);
  leakInfo.pump     = analogRead(WATER_SENSOR_PIN_PUMP);
  leakInfo.sumpPump = analogRead(WATER_SENSOR_PIN_SUMP_PUMP);
  leakInfo.ro       = analogRead(WATER_SENSOR_PIN_RO);

  return leakInfo;
}

TankInfo getTankInfo() {
  TankInfo info;
  info.readingTime = millis();

  info.distance = getAverageDistanceReading(10);

  // Convert mm to gallons
  // This is really the "empty space, in gallons"
  const float GALLONS_FROM_TOP = ((info.distance - TOP_OF_TANK_MM) / 10) * TANK_GALLONS_PER_CM;
  info.gallons = TANK_SIZE_IN_GALLONS - GALLONS_FROM_TOP;
  info.level = info.gallons / TANK_SIZE_IN_GALLONS;


  // default to no change
  info.mmPerSecondRate = 0;
  info.flowRate = 0;

  // Calculate flow rates
  if (previousTankInfo.readingTime > 0) {
    unsigned long timeDifference = info.readingTime - previousTankInfo.readingTime; // Time difference in milliseconds
    float distanceDifference = info.distance - previousTankInfo.distance; // Distance difference in mm
    float gallonsDifference  = info.gallons - previousTankInfo.gallons;

    Serial.print("fabs(distanceDifference): ");
    Serial.println(fabs(distanceDifference));
    // diff needs to be more than 1.1 mm (spec says it is good within 10 mm, but it seems to be better than that, I am going with 1.1)
    if (fabs(distanceDifference) > 1.1) {
      info.mmPerSecondRate = distanceDifference / (timeDifference / 1000.0); // Convert ms to seconds
      info.flowRate = gallonsDifference / (timeDifference / 60000.0); // Convert to gallons per minute
    }
  }


  // DEBUG
  Serial.print("Tank info: Distance: ");
  Serial.print(info.distance);
  Serial.print(", Gallons: ");
  Serial.print(info.gallons);
  Serial.print(", Level: ");
  Serial.print(info.level);
  Serial.print(", Flow Rate: ");
  Serial.print(info.flowRate);
  Serial.print(" GPM, mm/s Rate: ");
  Serial.println(info.mmPerSecondRate);

  return info;
}

float getAverageDistanceReading(int numReadings = 10);
float getAverageDistanceReading(int numReadings) {
  int totalReadings = 0;
  float readings[numReadings]; // The readings from the sensor
  int readIndex = 0;           // The index of the current reading
  float total = 0;             // The running total
  float average = 0;           // The average

  int validReadings = 0;

  float distance = 0;
  while (validReadings < numReadings) {
    distance = getDistanceReading();
 
    if (distance > 0) {
      total += distance;
      validReadings++;
    }
    
    totalReadings++;

    // if we cannot get valid readings, then we will bail
    if (totalReadings >= numReadings * 10) {
      break;
    }
    delay(10); // Small delay between readings
  }

  if (validReadings > 0) {
    average = total / validReadings;
  } else {
    average = 0;
  }

  return average;
}

float getDistanceReading() {
    // Set the trigger pin LOW for 2uS
    digitalWrite(TRIGPIN, LOW);
    delayMicroseconds(2);
 
    // Set the trigger pin HIGH for 20us to send pulse
    digitalWrite(TRIGPIN, HIGH);
    delayMicroseconds(20);
 
    // Return the trigger pin to LOW
    digitalWrite(TRIGPIN, LOW);
 
    // Measure the width of the incoming pulse
    float duration = pulseIn(ECHOPIN, HIGH);
 
    // Determine distance from duration
    // Use 343 metres per second as speed of sound
    // Divide by 1000 as we want millimeters
    float distance = ((duration / 2) * 0.343) + SENSOR_OFFSET_MM;

    if (distance < SENSOR_MIN_RANGE_MM) {
      Serial.print("INVALID READING, BACK UP: ");
      Serial.println(distance);
      return INVALID_READING_TOO_CLOSE;
    }

    return distance;
}

float convertToInches(float mm) {
  return mm / 25.4; // There are 25.4 millimeters in an inch
}

String formatFeetAndInches(float inches) {
  int feet = static_cast<int>(inches) / 12; // There are 12 inches in a foot
  float remainderInches = inches - (feet * 12);
  char buffer[20];
  sprintf(buffer, "%d ft %.1f in", feet, remainderInches);
  return String(buffer);
}
