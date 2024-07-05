#include "arduino_secrets.h"
#include <SPI.h>
#include <WiFi.h>

// Define connections to sensor
#define TRIGPIN 10
#define ECHOPIN 11

const int MAIN_LOOP_DELAY = 1000 * 30; // every 30 seconds (on top of the read delays due to avgs)

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

void setup() {
  // Set up serial monitor
  Serial.begin(115200);
  Serial.println("Starting up...");

  initWifi();
  initTankLevelSensor();
}

void loop() {
  TankInfo tankInfo = getTankInfo();
 
  float tankLevelInInches = convertToInches(tankInfo.distance);
  String tankLevelFeetAndInches = formatFeetAndInches(tankLevelInInches);

  if (tankInfo.distance == 0) {
    Serial.println("INVALID READING!!!");
    sendDataToCloud(tankInfo);
  } else {

    // Print result to serial monitor
    Serial.print("Average distance: ");
    Serial.print(tankInfo.distance);
    Serial.print(" mm (");
    Serial.print(tankLevelFeetAndInches);
    Serial.println(")");

    sendDataToCloud(tankInfo);
    delay(2500);    
  }

  // Update previousTankInfo
  previousTankInfo = tankInfo;

  // Delay before repeating measurement
  delay(MAIN_LOOP_DELAY);
}

void initTankLevelSensor() {
  // Set pinmodes for sensor connections
  pinMode(ECHOPIN, INPUT);
  pinMode(TRIGPIN, OUTPUT);
}

void initWifi() {
  Serial.println("Connecting Wifi...");
  char ssid[] = WIFI_SSID;
  char pass[] = WIFI_PASS;

  int status = WL_IDLE_STATUS;     // the WiFi radio's status
  while (status != WL_CONNECTED) {
    Serial.print("Attempting initial connection to WPA SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(9000);
  }

  Serial.println("Wifi Connected...");
}

void sendDataToCloud(TankInfo tankInfo) {
  // Send data to "api.asksensors.com"
  Serial.println("\nStarting connection to server...");

  // if you get a connection, report back via serial:
  if (client.connect("api.asksensors.com", 80)) {
    Serial.println("connected to server");
    // Make a HTTP request:
    // https://api.asksensors.com/write/eiECk9BfGWMq2o2ubCgkXlTW0szLFj11?module1=3

    String request = "GET /write/";
    request += ASK_SENSORS_API_TANK;
    request += "?module1=";
    request += String(tankInfo.distance, 4); // Convert float to string with 4 decimal places
    request += "&module2=";
    request += String(tankInfo.level, 4); // Convert float to string with 4 decimal places
    request += "&module3=";
    request += String(tankInfo.gallons, 4); // Convert float to string with 4 decimal places
    request += "&module4=";
    request += String(tankInfo.flowRate, 4); // Convert float to string with 4 decimal places
    request += " HTTP/1.1";

    // request = "GET /write/xxxx?module1=2.3455&module2=2&module3=3 HTTP/1.1"

    Serial.println(request);

    client.println(request);
    client.println("Host: api.asksensors.com");
    client.println("Connection: close");
    client.println();
  }
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
