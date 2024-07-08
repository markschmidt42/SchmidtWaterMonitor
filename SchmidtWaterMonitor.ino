#include "arduino_secrets.h"
#include "constants.h"
#include "thingProperties.h"
#include <SPI.h>
#include <WiFi.h>
#include "Arduino_LED_Matrix.h"
#include "Arduino_CloudConnectionFeedback.h"
#include "MedianFilterLib.h"
#include "CQRobotTDS.h"
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include "AutomaticBacklight.h"

const int MAIN_LOOP_DELAY_SECONDS = 5;

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
  unsigned int sump_pump;
  unsigned int ro;
};

ArduinoLEDMatrix matrix;

CQRobotTDS tds(CQROBOT_TDS_PIN);

LiquidCrystal_I2C lcd(0x27, 16, 2);
AutomaticBacklight automaticBacklight(&lcd, PIR_MOTION_SENSOR_PIN, 30);

void setup() {
  // Set up serial monitor
  Serial.begin(115200);
  initLCD();
  delay(1500); // small delay for serial port setup 
  Serial.println("Starting up...");

  // Connect to Arduino Cloud
  initProperties();

  /* Initialize Arduino IoT Cloud library */
  updateLcdStatus("Connecting to", "the internets...");
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  ArduinoCloud.printDebugInfo();
  // This line will block until we're connected to Arduino Cloud
  // using the LED matrix to provide feedback
  matrix.begin();
  waitForArduinoCloudConnection(matrix);
  
  updateLcdStatus("We're CONNECTED!");
  Serial.println("Program started!");
  delay(1000); // small delay for status update

  // Use the LED matrix to do something else
  matrix.loadSequence(LEDMATRIX_ANIMATION_TETRIS_INTRO);
  matrix.play(false);

  setDebugMessageLevel(DBG_INFO);

  updateLcdStatus("Initializing", "Sensors...");
  initTankLevelSensor();
}

int loopCounter = 0;
void loop() {
  ArduinoCloud.update();
  updateAutomaticBacklightStatus();

  TankInfo tankInfo = getTankInfo();
  LeakInfo leakInfo = getLeakInfo();

  updateTdsValueWhenAvailable();
  sendTankInfoToCloud(tankInfo);
  sendLeakInfoToCloud(leakInfo);
  updateLcdInfo();

  // Update previousTankInfo
  // only update previous every 5 times, this is used for flow rate only
  // if (loopCounter % 5 == 0) {
  previousTankInfo = tankInfo;
  //}

  // Delay before repeating measurement
  delay(MAIN_LOOP_DELAY_SECONDS * 1000);
  loopCounter++;
}

void onIsWetChange() {
  Serial.print("onIsWetChange: ");
  Serial.println(water_sensor_is_wet);
}

void onIsTankTooLowChange() {
  Serial.print("onIsTankTooLowChange: ");
  Serial.print(tank_is_too_low);
  Serial.print(" gallons: ");
  Serial.println(tank_level_gallons);
}

void onIsTankTooHighChange() {
  Serial.print("onIsTankTooHighChange: ");
  Serial.print(tank_is_too_high);
  Serial.print(" gallons: ");
  Serial.println(tank_level_gallons);
}

void initLCD() {
  // initialize the LCD
	lcd.init();
	// Turn on the blacklight and print a message.
  automaticBacklight.turnOn(); 

  // TODO: motion sensor for backlight, for now... keep it off
  updateLcdStatus("Hold on to your", "butts...");
}

void updateLcdStatus(String line1) {
  updateLcdStatus(line1, "");
}

void updateLcdStatus(String line1, String line2) {
  lcdPrintRow(0, line1);
  lcdPrintRow(1, line2);
}

void updateLcdInfo() {
  // 0000000000111111 ( 2 rows)
  // 0123456789012345 (16 cols)
  // 123g 80% -0.0gpm
  // TDS 114ppm   WET
  // 123g TDS=114ppm
  // 80% -2.34gpm WET

  String lineOne = "";
  lineOne.concat((int)tank_level_gallons);
  lineOne.concat("g ");
  lineOne.concat("TDS=");
  lineOne.concat(water_test_tds_ppm);
  lineOne.concat("ppm");

  String lineTwo = "";
  lineTwo.concat((int)round(tank_level_percent));
  lineTwo.concat("% ");
  lineTwo.concat(tank_flow_rate_gpm);
  lineTwo.concat("gpm ");
  
  if (water_sensor_is_wet) {
    lineTwo.concat("WET");
  } else {
    lineTwo.concat("DRY");
  }

  lcdPrintRow(0, lineOne);
  lcdPrintRow(1, lineTwo);
}

void lcdPrintRow(int rowIndex, String str) {
  lcd.setCursor(0, rowIndex);
  lcd.print(str);
  // fill with trailing spaces
  for(int i = str.length(); i<16;i++) lcd.print(' ');
}

void updateAutomaticBacklightStatus() {
  automaticBacklight.update();
  utility_room_motion = automaticBacklight.isMotion();
}

void initTankLevelSensor() {
  // Set pinmodes for sensor connections
  pinMode(ULTRASONIC_ECHOPIN, INPUT);
  pinMode(ULTRASONIC_TRIGPIN, OUTPUT);
}

void sendTankInfoToCloud(TankInfo tankInfo) {
  if (tankInfo.distance <= 0) {
    Serial.println("sendTankInfoToCloud() invalid reading, not sending...");
    return;
  }

  tank_distance_from_top_mm = tankInfo.distance;
  tank_level_gallons        = round(tankInfo.gallons);
  tank_level_percent        = tankInfo.level * 100;
  tank_flow_rate_gpm        = round(tankInfo.flowRate * 10) / 10.0; // round to 1 decimal
}

void sendLeakInfoToCloud(LeakInfo leakInfo) {
  water_sensor_drain     = leakInfo.drain;
  water_sensor_pump      = leakInfo.pump;
  water_sensor_sump_pump = leakInfo.sump_pump;
  water_sensor_ro        = leakInfo.ro;
}

LeakInfo getLeakInfo() {
  LeakInfo leakInfo;
  leakInfo.drain     = getWaterSensorPercent(WATER_SENSOR_PIN_DRAIN);
  leakInfo.pump      = getWaterSensorPercent(WATER_SENSOR_PIN_PUMP);
  leakInfo.sump_pump = getWaterSensorPercent(WATER_SENSOR_PIN_SUMP_PUMP);
  leakInfo.ro        = 0;
  //leakInfo.ro        = getWaterSensorPercent(WATER_SENSOR_PIN_RO);

  setLeakAlertFlags(leakInfo);

  return leakInfo;
}

void setLeakAlertFlags(LeakInfo leakInfo) {
  // if any ONE of these values are above the threshold, then consider it wet
  water_sensor_is_wet = (
       leakInfo.drain     >= WET_VALUE_THRESHOLD
    || leakInfo.pump      >= WET_VALUE_THRESHOLD
    || leakInfo.sump_pump >= WET_VALUE_THRESHOLD
    || leakInfo.ro        >= WET_VALUE_THRESHOLD
  );
}

void setTankInfoFlags(TankInfo tankInfo) {
  tank_is_too_low  = (tankInfo.gallons <= TANK_LEVEL_TOO_LOW_VALUE);
  tank_is_too_high = (tankInfo.gallons >= TANK_LEVEL_TOO_HIGH_VALUE);
}

float getTdsValue() {
  float temp = 17.0; // read temprature from a real sensor
	float tdsValue = tds.update(temp);

  Serial.print("TDS value: ");
	Serial.print(tdsValue, 0);
	Serial.println(" ppm");  

  return tdsValue;
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
  info.distance = getAverageDistanceReading(TANK_ULTRASONIC_SAMPLE_COUNT);
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
    float distanceDifference = info.distance - previousTankInfo.distance; // Distance difference in mm, rounded to nearest mm
    float gallonsDifference  = info.gallons - previousTankInfo.gallons;

    Serial.print("fabs(distanceDifference): ");
    Serial.println(fabs(distanceDifference));
    // diff needs to be more than 1.1 mm (spec says it is good within 10 mm, but it seems to be better than that, I am going with 1.1)
    if (fabs(distanceDifference) > 0 && timeDifference > 0) {
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

  // if it is using less than a half gallon a minute, then we are going to say it is not flowing...
  if (fabs(info.flowRate) <= 0.4) {
    info.flowRate        = 0;
    info.mmPerSecondRate = 0;
    Serial.print("TOO SLOW... OVERRDING... Flow Rate: ");
    Serial.print(info.flowRate);
    Serial.print(" GPM, mm/s Rate: ");
    Serial.println(info.mmPerSecondRate);
  }

  float tankLevelInInches = convertToInches(info.distance);
  String tankLevelFeetAndInches = formatFeetAndInches(tankLevelInInches);

  if (info.distance <= 0) {
    Serial.println("INVALID READING!!!");
  } else {
    // Print result to serial monitor
    Serial.print("Average distance: ");
    Serial.print(info.distance);
    Serial.print(" mm (");
    Serial.print(tankLevelFeetAndInches);
    Serial.println(")");
  }

  setTankInfoFlags(info);

  return info;
}

void updateTdsValueWhenAvailable() {
  // if tank level gallons is over X (meaning we can reach it with the sensor, then take a measurement)
  if (tank_level_gallons < TANK_LEVEL_GALLONS_FOR_TDS_MEASUREMENT) {
    return;
  }

  int tdsValue = getTdsValue();
  Serial.println(tdsValue);
  // if invalid reading
  if (tdsValue <= 0) {
    return;
  }

  Serial.println("UPDATING: water_test_tds_ppm");
  water_test_tds_ppm = tdsValue;
}

float getAverageDistanceReading() {
  return getAverageDistanceReading(10);
}

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
    delay(100); // Small delay between readings
  }

  return median;
}

float getDistanceReading() {
    // Set the trigger pin LOW for 2uS
    digitalWrite(ULTRASONIC_TRIGPIN, LOW);
    delayMicroseconds(2);
 
    // Set the trigger pin HIGH for 20us to send pulse
    digitalWrite(ULTRASONIC_TRIGPIN, HIGH);
    delayMicroseconds(10);
 
    // Return the trigger pin to LOW
    digitalWrite(ULTRASONIC_TRIGPIN, LOW);
 
    // Measure the width of the incoming pulse
    float duration = pulseIn(ULTRASONIC_ECHOPIN, HIGH);
 
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
