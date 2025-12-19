#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>


// set the LCD number of columns and rows
int lcdColumns = 16;
int lcdRows = 2;

// set LCD address, number of columns and rows
// also 0x50 0x68
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);


// GPIO where the DS18B20 is connected to
const int oneWireBusSensor1 = 13;

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWireSensor1(oneWireBusSensor1);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensor1(&oneWireSensor1);
//---------------------------------------------
int loopCounter = 0;
//---------------------------------------------

//servo
Servo servo;
const int servoPin = 32;

//---- potentiometers
const int potentiometer1Pin = 35;
const int potentiometer2Pin = 25;
const int potentiometer3Pin = 26;
const int potentiometer4Pin = 27;
const int button1Pin = 16;
const int button2Pin = 17;

//-------------------------------------------------
const float WARM_UP_TEMP = 48.0;
unsigned long lastControlTime = 0;
unsigned long lastTempRequestedTime = 0;
//--------------------------------------------------

// --- PD CONTROLLER PARAMETERS (Global Constants) ---
const float SETPOINT_TEMP = 57.0;   // Desired wood stove temperature in Celsius
const float Kp = 2.5;              // Proportional Gain (Needs tuning)
float Kd = 5.0;             // Derivative Gain (Needs tuning)
unsigned int loopDelayMilis = 10000; // 10 seconds (Parameterized time step)

// --- SERVO FLAP PARAMETERS (Global Constants) ---
const int FLAP_CLOSED_ANGLE = 20;   // Fully closed flap (0 degrees)
const int FLAP_OPEN_ANGLE = 150;   // Fully open flap (180 degrees)

// --- PD CONTROLLER STATE (Global Variables) ---
float lastError = 0.0;           // Previous error value for the Derivative term
unsigned long lastTime = 0;      // Timestamp of the last control loop execution
float currentFlapPercent = 0.0; 
int currentFlapAngle = FLAP_CLOSED_ANGLE;
//--------------------------------------------------
// --- DYNAMIC KD PARAMETERS (NEW) ---
const float KD_BASE = 5.0;            // Kd used when temp is stable (low response)
float KD_MAX = 50.0;            // Kd used when temp changes rapidly (aggressive response)
float DERIVATIVE_LOW_THRESHOLD = 0.15; // dError/dt below this uses KD_BASE (e.g., 0.05 C/min)
float DERIVATIVE_HIGH_THRESHOLD = 0.75; // dError/dt above this uses KD_MAX (e.g., 0.50 C/min)


// --- ROLLING MEAN FILTER PARAMETERS ---
// 6 samples * 10 seconds/sample = 60 seconds (1 minute window)
const int WINDOW_SIZE = 5; 
float tempWindow[WINDOW_SIZE];  // Array to hold the last N temperature samples
float tempRunningSum = 0.0;     // O(1) optimization: holds the sum of all values in tempWindow
int windowIndex = 0;            // Current index in the circular buffer
int currentSampleCount = 0;     // Tracks how many valid samples are currently in the window
float rollingMeanTemp;
//-----------------------------------------------

float temp1, temp2;

void setup(){
  Serial.begin(9600);

  // initialize LCD
  lcd.init();
  // turn on LCD backlight                      
  lcd.backlight();

  //Temp sensors
  sensor1.begin();

  //servo
  servo.attach(servoPin);

  pinMode(button1Pin, INPUT_PULLUP);
  pinMode(button2Pin, INPUT_PULLUP);
}

void loop() {
  updatePotentiometerValues();

  //get temps
  if (millis() - lastTempRequestedTime >= 1000) {
    lastTempRequestedTime = millis();
    requestTemperatures();
    printStatus();
  }

  //manual mode
  if (digitalRead(button1Pin) == LOW) {
      int val = analogRead(potentiometer1Pin);
      float percent = (float) val / (float) 4096;
      setFlapPercent(percent * 100.0);
      return;
  }

  //warm-up mode
  if (temp2 < WARM_UP_TEMP && currentFlapAngle != FLAP_OPEN_ANGLE) {
    setFlapPercent(100.0);
    return;
  }
  
  // Check if the parameterized control delay time has elapsed
  if (millis() - lastControlTime >= loopDelayMilis) {
    lastControlTime = millis();
    if (lastControlTime - lastTempRequestedTime > 1000) {
      requestTemperatures();
    }
    float deltaPercent = computePDOutput(rollingMeanTemp);
    float newTargetPercent = currentFlapPercent + deltaPercent;
    setFlapPercent(newTargetPercent);
  }

  if (lastControlTime > 24 * 60 * 60 * 1000 && rollingMeanTemp < 27.0) {
     ESP.restart();
  }
}

void updatePotentiometerValues() {
  int val = analogRead(potentiometer2Pin);
  DERIVATIVE_LOW_THRESHOLD = (val / (float) 4096);
  DERIVATIVE_HIGH_THRESHOLD = DERIVATIVE_LOW_THRESHOLD + 1.0;

  int val2 = analogRead(potentiometer3Pin);
  Kd = (val2 / (float) 4096) * 50.0;

  int val3 = analogRead(potentiometer4Pin);
  KD_MAX = (val3 / (float) 4096) * 100;
}

void requestTemperatures() {
  sensor1.requestTemperatures();
  float t1 = sensor1.getTempCByIndex(0);
  if (t1 < -100.0) {
    return;
  }
  temp1 = t1;
  rollingMeanTemp = getFilteredTemperature(temp1);
  float t2 = sensor1.getTempCByIndex(1);
  if ( t2 < -100.0) {
    return;
  }
  temp2 = t2;
}

void printStatus() {
  lcd.clear();
    // set cursor to first column, first row
  lcd.setCursor(0, 0);
  String t1 = String(rollingMeanTemp, 1);
  String t2 = String(temp2, 0);
  String lowTh = String(DERIVATIVE_LOW_THRESHOLD, 2);
  String firstLine = t1 + " " + t2 + " " + lowTh + " " + currentFlapAngle;
  lcd.print(firstLine);

  // set cursor to first column, second row
  lcd.setCursor(0,1);

  String kd = String(Kd, 1);
  String kdMax = String(KD_MAX, 1);
  String secondLine = kd + " " + kdMax;
  lcd.print(secondLine);
}


void setFlapPercent(float targetPercent) {
  float aLittleOpenAngle = 30.0;
  currentFlapPercent = constrain(targetPercent, 0.0, 100.0);
  float targetAngle = 0.0;
  if (currentFlapPercent < 1.1) {
    targetAngle = FLAP_CLOSED_ANGLE;
  } else {
    float range = FLAP_OPEN_ANGLE - aLittleOpenAngle;
    targetAngle = aLittleOpenAngle +  range * ((currentFlapPercent - 1.1) / 100.0); 
  }

  currentFlapAngle = (int) constrain(targetAngle, FLAP_CLOSED_ANGLE, FLAP_OPEN_ANGLE);
  servo.write(currentFlapAngle);
}


float computePDOutput(float currentTemp) {
  unsigned long now = millis();
  
  // Calculate delta t in minutes. Using global lastTime.
  float timeChange = (float) (now - lastTime) / 60000.0; 

  if (timeChange == 0) {
    return 0.0;
  }

  // 1. Calculate Error
  float error = SETPOINT_TEMP - currentTemp;

  // 2. Proportional Term (P)
  float P = Kp * error;


  // 3. Derivative Term (D)
  float derivative = (error - lastError) / timeChange;
  float D = Kd * derivative;

  if (digitalRead(button2Pin) == LOW) {
    // 2. Dynamic Kd Calculation (Gain Scheduling)
    float dynamicKd;
    float absDerivative = abs(derivative); // Use absolute rate of change

    if (absDerivative <= DERIVATIVE_LOW_THRESHOLD) {
      // A. Very low rate of change: Use BASE Kd
      dynamicKd = Kd;
    }
    else if (absDerivative >= DERIVATIVE_HIGH_THRESHOLD) {
      // B. High rate of change: Use MAX Kd
      dynamicKd = KD_MAX;
    }
    else {
      // Normalize input between 0 and 1
      float normalizedInput = (absDerivative - DERIVATIVE_LOW_THRESHOLD) / (DERIVATIVE_HIGH_THRESHOLD - DERIVATIVE_LOW_THRESHOLD);

      // Scale the output
      dynamicKd = Kd + (KD_MAX - Kd) * normalizedInput;

      // Ensure it's constrained just in case of float math error
      dynamicKd = constrain(dynamicKd, Kd, KD_MAX);
    }
    D = derivative * dynamicKd;
  }

  // 4. Calculate Control Signal (required change in angle)
  float controlSignal = P + D;

  // --- UPDATE GLOBAL STATE ---
  lastError = error;
  lastTime = now;

  return controlSignal;
}


float getFilteredTemperature(float rawTemp) {
  tempRunningSum -= tempWindow[windowIndex];
  tempWindow[windowIndex] = rawTemp;
  tempRunningSum += rawTemp;
  if (currentSampleCount < WINDOW_SIZE) {
    // During startup, gradually increase the count until the window is full
    currentSampleCount++;
  }
  
  windowIndex++;
  if (windowIndex >= WINDOW_SIZE) {
    windowIndex = 0; // Wrap around the circular buffer
  }
  if (currentSampleCount == 0) {
    return rawTemp; // Should not happen after setup
  }
  return tempRunningSum / currentSampleCount;
}
