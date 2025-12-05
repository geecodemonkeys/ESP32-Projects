
#include <LiquidCrystal_I2C.h>
#include <FS.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>

const float pumpStartTemp = 58.0;
const float pumpStopTemp = 52.0;
const float frostProtStartTemp = 4.0;
const float frostProtStopTemp = 4.5;
const float overHeatAlarmTemp = 70.0;
const float noAcAlarmTempThreshold = 45.0;
const int alarmDurationInSec = 180;

const int relayPin = 25;
const int buzzerPin = 26;
const int acVoltagePin = 34;

// set the LCD number of columns and rows
int lcdColumns = 16;
int lcdRows = 2;

// set LCD address, number of columns and rows
// if you don't know your display address, run an I2C scanner sketch
LiquidCrystal_I2C lcd(0x3F, lcdColumns, lcdRows);

// GPIO where the DS18B20 is connected to
const int oneWireBusSensor1 = 14;
const int oneWireBusSensor2 = 27;     

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWireSensor1(oneWireBusSensor1);
OneWire oneWireSensor2(oneWireBusSensor2);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensor1(&oneWireSensor1);
DallasTemperature sensor2(&oneWireSensor2);

int ctr = 0;
bool pumpRunning = false;
bool frostProtectionON = false;
bool overHeatAlarmOn = false;
bool noAcAlarmOn = false;
int alarmStartCtr = 0;

void setup(){
  Serial.begin(115200);
  // initialize LCD
  lcd.init();
  // turn on LCD backlight                      
  lcd.backlight();

  //Temp sensors
  sensor1.begin();
  sensor2.begin();

  //pins
  pinMode(relayPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(acVoltagePin, INPUT);
  digitalWrite(buzzerPin, HIGH);
  digitalWrite(relayPin, LOW);
  delay(100);
}

void loop() {
  //time integration
  //uint32_t currTime = getTimeSinceStart(); 

  //get temps
  sensor1.requestTemperatures();
  float temp = sensor1.getTempCByIndex(0);
  sensor2.requestTemperatures();
  float temp2 = sensor2.getTempCByIndex(0);
  //START pump
  if (temp >= pumpStartTemp && !pumpRunning) {
    startPump();
  }
  //STOP pump
  if (temp <= pumpStopTemp && pumpRunning) {
    stopPump();
  }

  String alarms = "";

  //freeze protection below 4C START
  if ((temp <= frostProtStartTemp || temp2 <= frostProtStartTemp) && !frostProtectionON) {
    digitalWrite(relayPin, HIGH);
    frostProtectionON = true;
  }

   //freeze protection below 4C STOP
  if ((temp >= frostProtStopTemp && temp2 >= frostProtStopTemp) && frostProtectionON) {
    digitalWrite(relayPin, LOW);
    frostProtectionON = false;
  }

  if (frostProtectionON) {
    alarms = alarms + "FROST";
  }
  
  //Overheat alarm over 70C
  if (temp >= overHeatAlarmTemp && !overHeatAlarmOn) {
    overHeatAlarmOn = true;
    startBuzzer();
    alarmStartCtr = ctr;
  }
  if (temp < overHeatAlarmTemp - 2.0 && overHeatAlarmOn) {
    overHeatAlarmOn = false;
  }
  if (overHeatAlarmOn) {
    alarms = alarms + "OVERHEAT";
  }

  //Check no AC voltage and heater running
  if (digitalRead(acVoltagePin) == LOW && !noAcAlarmOn && temp >= noAcAlarmTempThreshold) {
    noAcAlarmOn = true;
    if (isBuzzerOff()) { //beep only when heater is running
      startBuzzer();
      alarmStartCtr = ctr;
    }
  }

  if (digitalRead(acVoltagePin) == HIGH && noAcAlarmOn) {
    noAcAlarmOn = false;
  }
  if (temp < noAcAlarmTempThreshold - 1.0 && noAcAlarmOn) { //if temp drop below the threshold just stop beeping
    noAcAlarmOn = false;
  }

  if (digitalRead(acVoltagePin) == LOW) {
    alarms = alarms + " !AC";
  }

  // stop buzzer after 3 min
  if (ctr - alarmStartCtr > alarmDurationInSec && isBuzzerOn()) {
    stopBuzzer();
  }
  if (!(overHeatAlarmOn || noAcAlarmOn) && isBuzzerOn()) {
    stopBuzzer();
  }

  //update display
  bool pumpOn = digitalRead(relayPin) == HIGH;
  printCurrentTemps(temp, temp2, alarms, pumpOn);

  delay(1000);
  ctr++;
}

void startPump() {
  digitalWrite(relayPin, HIGH);
  pumpRunning = true;
}

void stopPump() {
  digitalWrite(relayPin, LOW);
  pumpRunning = false;
}

bool isBuzzerOff() {
  return digitalRead(buzzerPin) == HIGH;
}

bool isBuzzerOn() {
  return digitalRead(buzzerPin) == LOW;
}

void startBuzzer() {
  digitalWrite(buzzerPin, LOW);
}

void stopBuzzer() {
  digitalWrite(buzzerPin, HIGH);
}

void printCurrentTemps(float temp1, float temp2, String alarms, bool pumpOn) {
  lcd.clear();
    // set cursor to first column, first row
  lcd.setCursor(0, 0);
  // print message
  String str1 = String(temp1, 0);
  String str2 = String(temp2, 0);
  String diff = String(abs(temp1 - temp2), 1);
  String pumpOnStr = pumpOn ? "ON" : "OFF";
  lcd.print(str1 + " " + str2 + " " + diff + " " + pumpOnStr);
  // set cursor to first column, second row
  lcd.setCursor(0,1);
  lcd.print(alarms);
}