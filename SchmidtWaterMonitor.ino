#include "arduino_secrets.h"
#include "constants.h"
#include "thingProperties.h"
#include <SPI.h>
#include <WiFi.h>
#include "Arduino_LED_Matrix.h"
#include "Arduino_CloudConnectionFeedback.h"
#include "MedianFilterLib.h"

const int MAIN_LOOP_DELAY_SECONDS = 1;

const float SENSOR_OFFSET_MM = 21; // minor tweak to get the sensor to match real world measurements
const float SENSOR_MIN_RANGE_MM = 250; // unit handles 20 cm (200 mm), but giving it a bit of a buffer
const float INVALID_READING_TOO_CLOSE = -1000;

// How far away is the top of the tank from the sensor?
const float TOP_OF_TANK_MM = -50;

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
int loopCounter = 0;
void loop() {
  ArduinoCloud.update();

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
  // only update previous every 5 times, this is used for flow rate only
  if (loopCounter % 5 == 0) {
    previousTankInfo = tankInfo;
  }

  // Delay before repeating measurement
  delay(MAIN_LOOP_DELAY_SECONDS * 1000);
  loopCounter++;
}

void initTankLevelSensor() {
  // Set pinmodes for sensor connections
  pinMode(ECHOPIN, INPUT);
  pinMode(TRIGPIN, OUTPUT);
}

void sendToArduinoCloud(TankInfo tankInfo) {
  tank_distance_from_top_mm = tankInfo.distance;
  tank_level_gallons        = round(tankInfo.gallons);
  tank_level_percent        = tankInfo.level * 100;
  tank_flow_rate_gpm        = round(tankInfo.flowRate * 10) / 10.0; // round to 1 decimal
  Serial.println(tank_flow_rate_gpm);
}

void sendLeakInfoToCloud(LeakInfo leakInfo) {
  water_sensor_drain     = leakInfo.drain;
  water_sensor_pump      = leakInfo.pump;
  water_sensor_sump_pump = leakInfo.sumpPump;
  water_sensor_ro        = leakInfo.ro;
}


LeakInfo getLeakInfo() {
  LeakInfo leakInfo;
  leakInfo.drain    = getWaterSensorPercent(WATER_SENSOR_PIN_DRAIN);
  leakInfo.pump     = getWaterSensorPercent(WATER_SENSOR_PIN_PUMP);
  leakInfo.sumpPump = getWaterSensorPercent(WATER_SENSOR_PIN_SUMP_PUMP);
  leakInfo.ro       = getWaterSensorPercent(WATER_SENSOR_PIN_RO);

  return leakInfo;
}

int getWaterSensorPercent(int pin) {
  int rawValue = analogRead(pin);

  // we will use 400 as the MAX VALUE
  int pct = (rawValue / 400.00) * 100;
  if (pct > 100) return 100;
  return pct;
}

TankInfo getTankInfo() {
  TankInfo info;
  info.distance = getAverageDistanceReading(20);
  info.readingTime = millis();

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

// float getMinDistanceReading(int numReadings) {
//   int totalReadings = 0;
//   int validReadings = 0;
  
//   float minValue = 10000000;

//   float distance = 0;

//   while (validReadings < numReadings) {
//     distance = getDistanceReading();
//     Serial.print("   distance: ");
//     Serial.println(distance);


//     if (distance > 0) {
//       if (distance < minValue) {
//         minValue = distance;
//       }
//       validReadings++;
//     }

//     totalReadings++;

//     // if we cannot get valid readings, then we will bail
//     if (totalReadings >= numReadings * 10) {
//       break;
//     }
//     delay(100); // Small delay between readings
//   }

//   Serial.print("* *minValue: ");
//   Serial.println(minValue);
//   return minValue;
// }

float getAverageDistanceReading(int numReadings = 10);
float getAverageDistanceReading(int numReadings) {
  int totalReadings = 0;
  int validReadings = 0;

  float distance = 0;
  float median = 0;

  MedianFilter<float> medianFilter(numReadings);

  while (validReadings < numReadings) {
    distance = getDistanceReading();
 
    if (distance > 0) {
      median = medianFilter.AddValue(distance);
      Serial.print("   distance: ");
      Serial.print(distance);
      Serial.print("   median: ");
      Serial.println(median);
      validReadings++;
    }
    
    totalReadings++;

    // if we cannot get valid readings, then we will bail
    if (totalReadings >= numReadings * 10) {
      break;
    }
    delay(200); // Small delay between readings
  }

  return median;
}

float getDistanceReading() {
    // Set the trigger pin LOW for 2uS
    digitalWrite(TRIGPIN, LOW);
    delayMicroseconds(2);
 
    // Set the trigger pin HIGH for 20us to send pulse
    digitalWrite(TRIGPIN, HIGH);
    delayMicroseconds(10);
 
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
