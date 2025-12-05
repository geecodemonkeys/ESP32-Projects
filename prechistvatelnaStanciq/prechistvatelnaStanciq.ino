#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <I2C_RTC.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SD.h"
#include <ESP32Servo.h>

static DS1307 RTC;

// set the LCD number of columns and rows
int lcdColumns = 16;
int lcdRows = 2;

// set LCD address, number of columns and rows
// also 0x50 0x68
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);


const int echoPin = 27;
const int trigPin = 26;
const int servo1Pin = 32;
const int servo2Pin = 33;
const int relayPin = 25;


//-----------Servo------------------------
Servo servoAirLiftValve;
Servo servoAerationValve;

//-------------------wifi----------------------
// Replace with your network credentials
String ssid     = "fi2";
String password = "mamatatko";

const String defaultSsid     = "prechistvatelna_stanciq";
const String defaultPassword = "mamatatko";
int accessPointEnabled = 0;

// Variable to store the HTTP request
String header;
WiFiServer server(80);
//---------ip adress populated when initialised---
IPAddress IP;
//---------------------------------------------
const char* currentWorkFile = "/currentWork.txt";
//---------------------------------------------
int loopCounter = 0;
int cycleCounter = 0;
//---------------------------------------------
float currentWaterLevel = 0;
float waterLevelAtBeginingOfPreviousCycle = 0;
float waterLevelAtBeginingOfRelease = 0;
float waterInfluxLastTwoCycles = 0;
float minWaterInfluxToStartCycle = 5;
float releaseWaterLevelStopPercent = 0.8;
float emergencyWaterLevelActivationPercent = 0.95;
const float distanceFromBottomToTop = 218.0;
const float maxAllowedWaterLevel = 114.0;
const float waterLevelTillExitSyphon = 146.5;
const float minWaterLevel = 59.0;
//----------------------------------------------
const int baseCloseServeAngle = 150;
const int baseOpenServoAngle = 108;
//----------------------------------------------

bool isEmergencyWaterReleaseRunning = false;
bool isReleasing = false;
bool isAerating = false;
bool isNopCycleRunning = false;
bool testMode = false;
bool testAeration = false;
bool testRelease = false;
bool prevCycleTestMode = false;
bool nopAerationRunning = false;

int aerationStartHour = 7;
int aerationStartMinute = 0;
int aerationDuration = 8;
int aerationEndHour = aerationStartHour + aerationDuration;

int releaseWaterStartHour = 6;
int releaseWaterStartMinute = 30;
int releaseWaterDuration = 30;

int nopAerationDuration = 5;

void setup(){
  Serial.begin(9600);

  RTC.begin();

  // initialize LCD
  lcd.init();
  // turn on LCD backlight                      
  lcd.backlight();

  //sdcard
  initSdCard();
  readConfigFile(SD);

  if (accessPointEnabled == 1) {
    //Wifi
    createHotspot();
  } else {
    connectToWifi();
  }

  //start webserver
  server.begin();

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(relayPin, OUTPUT);

  //continue where we left
  continueWork(SD);

  servoAirLiftValve.attach(servo1Pin);
  servoAerationValve.attach(servo2Pin);
}



void loop() {

  // measure distance on each cycle
  float d = measureDistance();
  currentWaterLevel = distanceFromBottomToTop - d;

  // check if water level reading is OK and raise alarm if not.
  bool isWaterLevelValueOK = checkWaterLevelValue(currentWaterLevel);

  //handle web request
  handleWebRequest(isWaterLevelValueOK, d);

  if (testMode && !prevCycleTestMode) {
    if (testAeration) {
      startAeration();
    }
    if (testRelease) {
      startReleasing();
    }
  } if (!testMode && prevCycleTestMode) {
    digitalWrite(relayPin, LOW);
    if (isReleasing) { //continue what was started before test mode
      startReleasing();
    }
    if (isAerating) {
      startAeration();
    }
  } else {
    //emergency water release
    if (!isEmergencyWaterReleaseRunning && isWaterLevelValueOK && currentWaterLevel >= maxAllowedWaterLevel * emergencyWaterLevelActivationPercent) {
      emergencyWaterRelease();
    }
    if (isEmergencyWaterReleaseRunning && currentWaterLevel < maxAllowedWaterLevel * releaseWaterLevelStopPercent) {
      stopEmergencyWaterRelease();
    }

    //open and close valves based on time
    openCloseValvesAndStartStopCompressor(isWaterLevelValueOK, currentWaterLevel);
  }

  printStatus();
  prevCycleTestMode = testMode;
  loopCounter++;
  if (loopCounter >= 60) {
    loopCounter = 0;
  }
  delay(1000);
}

float measureDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH);
  return duration * 0.034 / 2.0;
}

bool checkWaterLevelValue(float value) {
  return value > minWaterLevel && value < waterLevelTillExitSyphon;
}

void emergencyWaterRelease() {
  if (isReleasing) {
    return;
  }
  startReleasing();
  isEmergencyWaterReleaseRunning = true;
  writeFile(SD, currentWorkFile, "EMERGENCY");
}

void stopEmergencyWaterRelease() {
  digitalWrite(relayPin, LOW);
  isEmergencyWaterReleaseRunning = false;
  writeFile(SD, currentWorkFile, "IDLE");
}

void openCloseValvesAndStartStopCompressor(bool isWaterLevelValueOK, float waterLevel) {
  if (isEmergencyWaterReleaseRunning || testMode) {
    return;
  }
  int hour = RTC.getHours();
  int minute = RTC.getMinutes();
  bool startCycle = startNextCycle(isWaterLevelValueOK, waterLevel);
  if (isTimeToStartAeration(hour, minute) && startCycle && isNopCycleRunning) {
    isNopCycleRunning = false;
  }

  if (isNopCycleRunning) {
    startStopNopAeration(minute);
    return;
  }

  if (!isAerating && !isNopCycleRunning && isTimeToStartAeration(hour, minute) && startCycle) {
    startAeration();
    waterLevelAtBeginingOfPreviousCycle = waterLevel;
    isAerating = true;
    writeFile(SD, currentWorkFile, "AERATION");
  }

  if (!isAerating && !isNopCycleRunning && isTimeToStartAeration(hour, minute) && !startCycle) {
    //NOP Cycle 12h.
    isNopCycleRunning = true;
    startAeration();
    nopAerationRunning = true;
    writeFile(SD, currentWorkFile, "NOP");
    return;
  }

  if (isAerating && isTimeToStopAeration(hour, minute)) {
    digitalWrite(relayPin, LOW);
    isAerating = false;
    writeFile(SD, currentWorkFile, "IDLE");
  }

  if (!isReleasing && !isNopCycleRunning && isTimeToStartRelease(hour, minute)) {
    waterLevelAtBeginingOfRelease = waterLevel;
    startReleasing();
    isReleasing = true;
    writeFile(SD, currentWorkFile, "RELEASING");
  }

  int totalMinutes = (releaseWaterStartHour * 60) + releaseWaterStartMinute + releaseWaterDuration;
  int releaseWaterEndHour = (totalMinutes / 60) % 24;;
  int releaseWaterEndMinute = totalMinutes % 60;
  if (isWaterLevelValueOK) {
    if (isReleasing && waterLevel <= maxAllowedWaterLevel * releaseWaterLevelStopPercent) {
      if (cycleCounter % 2 == 0) {
        waterInfluxLastTwoCycles = 0;
      }
      float waterReleased = waterLevel - waterLevelAtBeginingOfRelease;
      waterInfluxLastTwoCycles += waterReleased;
      digitalWrite(relayPin, LOW);
      isReleasing = false;
      cycleCounter++;
      writeFile(SD, currentWorkFile, "IDLE");
    }
  } else {
    if (isReleasing && (hour == releaseWaterEndHour || hour == (releaseWaterEndHour + 12) % 24) && minute == releaseWaterEndMinute) {
      digitalWrite(relayPin, LOW);
      isReleasing = false;
      cycleCounter++;
      writeFile(SD, currentWorkFile, "IDLE");
    }
  }
}

bool startNextCycle(bool isWaterLevelValueOK, float waterLevel) {
  if (!isWaterLevelValueOK) {
    return false;
  }
  float influxFromPrevCycle = waterLevel - waterLevelAtBeginingOfPreviousCycle;
  return influxFromPrevCycle > minWaterInfluxToStartCycle;
}

bool isTimeToStartAeration(int hour, int minute) {
  return (hour == aerationStartHour % 24 || hour == (aerationStartHour + 12) % 24) && minute == aerationStartMinute;
}

bool isTimeToStopAeration(int hour, int minute) {
  return (hour == aerationEndHour % 24 || hour == (aerationEndHour + 12) % 24) && minute == aerationStartMinute;
}

bool isTimeToStartRelease(int hour, int minute) {
  return (hour == releaseWaterStartHour % 24 || hour == (releaseWaterStartHour + 12) % 24) && minute == releaseWaterStartMinute;
}

bool isAerationTimeSlot(int hour, int minute) {
  return (((hour == aerationStartHour % 24 || hour == (aerationStartHour + 12) % 24) && minute >= aerationStartMinute) || hour > aerationStartHour)
          && (hour <= aerationEndHour % 24 || hour <= (aerationEndHour + 12) % 24);
}

void startStopNopAeration(int minute) {
  if (minute == aerationStartMinute + nopAerationDuration && nopAerationRunning) {
    digitalWrite(relayPin, LOW);
    nopAerationRunning = false;
  }
  if (minute = aerationStartMinute && !nopAerationRunning) {
    startAeration();
    nopAerationRunning = true;
  }
}

void startAeration() {
  //4 degress difference because of mechanical inconsistencies
  servoAirLiftValve.write(baseCloseServeAngle + 4);
  delay(1000);
  servoAerationValve.write(baseOpenServoAngle);
  delay(300);
  digitalWrite(relayPin, HIGH);
}

void startReleasing() {
  //3 degress difference because of mechanical inconsistencies
  servoAirLiftValve.write(baseOpenServoAngle + 3);
  delay(1000);
  servoAerationValve.write(baseCloseServeAngle);
  delay(300);
  digitalWrite(relayPin, HIGH);

}

void printStatus() {
  lcd.clear();
    // set cursor to first column, first row
  lcd.setCursor(0, 0);
  String time = String(RTC.getHours()) + ":" + String(RTC.getMinutes()) + " ";
  lcd.print(time);
  if (isEmergencyWaterReleaseRunning) {
    lcd.print("EME ");
  }
  if (isAerating) {
    lcd.print("AER ");
  }
  if (isReleasing) {
    lcd.print("REL ");
  }
  if (isNopCycleRunning) {
    lcd.print("NOP ");
  }

  // set cursor to first column, second row
  lcd.setCursor(0,1);
  String secondLine = String(currentWaterLevel, 0) + " ";
  if (secondLine.length() <=5 ) {
    secondLine += IP.toString();
  } else if (loopCounter >= 56 && loopCounter <= 59) {
    secondLine = IP.toString();
  }
  lcd.print(secondLine);
}

void setTime() {
  if(RTC.isConnected() == false)
  {
    Serial.println("RTC Not Connected!");
    while(true);
  }

  RTC.setHourMode(CLOCK_H24);

  RTC.setDay(31);
  RTC.setMonth(12);
  RTC.setYear(2024);

  RTC.setHours(19);
  RTC.setMinutes(32);
  RTC.setSeconds(0);
  RTC.setWeek(3);
}

void connectToWifi() {
    // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  IP = WiFi.localIP();
  // Print local IP address
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(IP);

}

void createHotspot() {
  IPAddress localIP(192,168,1,1);
  IPAddress subnet(255,255,255,0);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(localIP, localIP, subnet);
  WiFi.softAP(ssid, password);

  IP = WiFi.softAPIP();

  Serial.print("AP IP address: ");
  Serial.println(IP);
  delay(500);
}

void handleWebRequest(bool isWaterLevelValueOK, float d) {
  int contentLength = -1;
  WiFiClient client = server.available();   // Listen for incoming clients
  if (client) { // If a new client connects,
    client.setTimeout(2); // 2 secs
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            if (header.indexOf("POST") >= 0) {
              updateConfig(contentLength, client);
            }
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:application/json");
            client.println("Access-Control-Allow-Origin: *");
            client.println("Connection: close");
            client.println();

            JsonDocument doc, configDoc;
            doc["localDateTime"] = RTC.getDateTimeString();
            doc["compressorRunning"] = digitalRead(relayPin) == HIGH ? "ON" : "OFF";
            doc["currentWaterLevel"] = currentWaterLevel;
            doc["realSensorReading"] = d;
            doc["isWaterLevelValueOK"] = isWaterLevelValueOK;
            doc["waterLevelAtBeginingOfPreviousCycle"] = waterLevelAtBeginingOfPreviousCycle;
            doc["waterInfluxLastTwoCycles"] = waterInfluxLastTwoCycles;
            doc["isEmergencyWaterReleaseRunning"] = isEmergencyWaterReleaseRunning ? "ON" : "OFF";
            doc["isReleasing"] = isReleasing ? "ON" : "OFF";
            doc["isAerating"] = isAerating ? "ON" : "OFF";
            doc["isNopCycleRunning"] = isNopCycleRunning ? "ON" : "OFF";
            doc["nopAerationRunning"] = nopAerationRunning ? "ON" : "OFF";
            configDoc["testMode"] = testMode ? 1 : 0;
            configDoc["testAeration"] = testAeration ? 1 : 0;
            configDoc["testRelease"] = testRelease ? 1 : 0;
            configDoc["releaseWaterLevelStopPercent"] = releaseWaterLevelStopPercent;
            configDoc["emergencyWaterLevelActivationPercent"] = emergencyWaterLevelActivationPercent;
            configDoc["minWaterInfluxToStartCycle"] = minWaterInfluxToStartCycle;
            configDoc["aerationStartHour"] = aerationStartHour;
            configDoc["aerationStartMinute"] = aerationStartMinute;
            configDoc["aerationDuration"] = aerationDuration;
            configDoc["releaseWaterStartHour"] = releaseWaterStartHour;
            configDoc["releaseWaterStartMinute"] = releaseWaterStartMinute;
            configDoc["releaseWaterDuration"] = releaseWaterDuration;
            configDoc["nopAerationDuration"] = nopAerationDuration;
            configDoc["hour"] = RTC.getHours();
            configDoc["minute"] = RTC.getMinutes();
            configDoc["second"] = RTC.getSeconds();
            configDoc["accessPointEnabled"] = accessPointEnabled;
            configDoc["ssid"] = ssid;
            configDoc["password"] = password;
            doc["config"] = configDoc;
            
            String output;
            serializeJson(doc, output);
            client.println(output);
                 
            
            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            if (currentLine.indexOf("Content-Length:") >= 0) {
              String numberAsString = currentLine.substring(currentLine.indexOf(":") + 1);
              contentLength = numberAsString.toInt();
            }
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}

void updateConfig(int length, WiFiClient client) {
  String json = "";
  for (int i = 0; i < length; i++) {
    json += (char) client.read();
  }
  updateConfigFromJson(json);
  // save json to config file
  writeFile(SD, "/config.json", json.c_str());
}

void updateConfigFromJson(String json) {
  Serial.println(json);
  JsonDocument configDoc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(configDoc, json);

  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  testMode = configDoc["testMode"] == "1";
  testAeration = configDoc["testAeration"] == "1";
  testRelease = configDoc["testRelease"] == "1";
  releaseWaterLevelStopPercent = configDoc["releaseWaterLevelStopPercent"];
  emergencyWaterLevelActivationPercent = configDoc["emergencyWaterLevelActivationPercent"];
  minWaterInfluxToStartCycle = configDoc["minWaterInfluxToStartCycle"];
  aerationStartHour = configDoc["aerationStartHour"];
  aerationStartMinute = configDoc["aerationStartMinute"];
  aerationDuration = configDoc["aerationDuration"];
  releaseWaterStartHour = configDoc["releaseWaterStartHour"];
  releaseWaterStartMinute = configDoc["releaseWaterStartMinute"];
  releaseWaterDuration = configDoc["releaseWaterDuration"];
  RTC.setHours(configDoc["hour"]);
  RTC.setMinutes(configDoc["minute"]);
  RTC.setSeconds(configDoc["second"]);
  accessPointEnabled = configDoc["accessPointEnabled"] ? configDoc["accessPointEnabled"] : 1;
  
  const char* ssidChar = configDoc["ssid"];
  ssid = ssidChar ? String(ssidChar) : defaultSsid;
  
  const char* passwordChar = configDoc["password"];
  password = passwordChar ? String(passwordChar) : defaultPassword;
}

void continueWork(fs::FS &fs) {
  File file = fs.open(currentWorkFile);
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  String text = "";
  while(file.available()){
    text += (char) file.read();
  }
  file.close();
  int hour = RTC.getHours();
  int minute = RTC.getMinutes();
  if (text == "IDLE") {
    return;
  }
  if (text == "AERATION" && isAerationTimeSlot(hour, minute)) {
    startAeration();
    isAerating = true;
    return;
  }
  if (text == "RELEASING") {
    startReleasing();
    isReleasing = true;
    return;
  }
  if (text == "NOP") {
    isNopCycleRunning = true;
    return;
  }
  if (text == "EMERGENCY") {
    startReleasing();
    isEmergencyWaterReleaseRunning = true;
    return;
  }
}

void initSdCard() {
  if(!SD.begin(5)){
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
}

void readConfigFile(fs::FS &fs){

  File file = fs.open("/config.json");
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  String json = "";
  while(file.available()){
    json += (char) file.read();
  }
  file.close();
  updateConfigFromJson(json);
}

void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}
