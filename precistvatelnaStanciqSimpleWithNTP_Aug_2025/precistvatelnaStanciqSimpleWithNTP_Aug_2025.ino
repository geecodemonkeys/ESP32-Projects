#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <I2C_RTC.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SD.h"
#include "time.h"

const char* ntpServer = "europe.pool.ntp.org";
const long  gmtOffset_sec = 2 * 3600;
const int   daylightOffset_sec = 3600;

static DS1307 RTC;

// set the LCD number of columns and rows
int lcdColumns = 16;
int lcdRows = 2;

// set LCD address, number of columns and rows
// also 0x50 0x68
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);


const int echoPin = 27;
const int trigPin = 26;
const int aerationRelayPin = 32;
const int releasingRelayPin = 33;
const int fanRelayPin = 25;

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
IPAddress nullIP(0,0,0,0);
//---------------------------------------------
int loopCounter = 0;
//---------------------------------------------
float currentWaterLevel = 0;
float releaseWaterLevelStopPercent = 0.8;
const float distanceFromBottomToTop = 218.0;
const float maxAllowedWaterLevel = 114.0;
const float waterLevelTillExitSyphon = 146.5;
const float minWaterLevel = 59.0;
//----------------------------------------------

bool nopCycleActive = false;
bool testAeration = false;
bool testRelease = false;

int nopAerationDuration = 5;

//---------------------------------------------------------------------
const String defaultAerationSlots = "19:00-03:00;07:00-15:00";
const String defaultReleaseSlots = "05:00-05:30;17:00-17:30";
String aerationSlotsString = defaultAerationSlots;
String releaseSlotsString = defaultReleaseSlots;

const int timeSlotArraySize = 16;
int aerationSlots[timeSlotArraySize];
int releaseSlots[timeSlotArraySize];
int idleSlots[timeSlotArraySize];

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
   // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  updateRTCTime(true);

  //start webserver
  server.begin();

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(fanRelayPin, OUTPUT);
  pinMode(aerationRelayPin, OUTPUT);
  pinMode(releasingRelayPin, OUTPUT);
  pinMode(fanRelayPin, OUTPUT);
}



void loop() {

  // measure distance on each cycle
  float d = measureDistance();
  currentWaterLevel = distanceFromBottomToTop - d;

  // check if water level reading is OK and raise alarm if not.
  bool isWaterLevelValueOK = checkWaterLevelValue(currentWaterLevel);

  //handle web request
  handleWebRequest(isWaterLevelValueOK, d);

  if (testAeration && !testRelease) {
    startAeration();
  }
  if (testRelease && !testAeration) {
    startReleasing();
  }
  openCloseValvesAndStartStopCompressor();
  updateRTCTime(false);

  printStatus();
  loopCounter++;
  if (loopCounter >= 60) {
    loopCounter = 0;
    Serial.println("Wifi Status: " + String(WiFi.status()) + " IP: " + WiFi.localIP().toString());
    //Check if wifi is connected, hotspot can restart at any time
    if ((WiFi.status() != WL_CONNECTED || WiFi.localIP() == nullIP) && accessPointEnabled != 1) {
      Serial.println("Reconecting...");
      WiFi.disconnect();
      WiFi.reconnect();
      IP = WiFi.localIP();
      // Print local IP address
      Serial.print("IP address: ");
      Serial.println(IP);
      if (WiFi.localIP() != nullIP) {
        // Init and get the time
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        updateRTCTime(true);
      }
    }
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

void openCloseValvesAndStartStopCompressor() {
  if (testAeration || testRelease) {
    return;
  }

  int hour = getHour();
  int minute = getMinute();

  if (nopCycleActive) {
    if (minute >= 0 && minute < nopAerationDuration) {
      startAeration();
    } else {
      startIdle();
    }
    return;
  }

  if (isTimeInRange(hour, minute, aerationSlots)) {
    startAeration();
    return;
  }

  if (isTimeInRange(hour, minute, releaseSlots)) {
    startReleasing();
    return;
  }

  startIdle();
}

bool isTimeInRange(int hour, int minute, int array[]) {
  int totalMinute = hour * 60 + minute;
  for (int i = 0; i < timeSlotArraySize; i += 2) {
    int startMinutes = array[i];
    int endMinutes = array[i + 1];
    if (startMinutes  == -1) {
      return false;
    }
    // Normal range (e.g., 10:00 to 14:00)
    if (startMinutes <= endMinutes) {
      if (totalMinute >= startMinutes && totalMinute < endMinutes) {
        return true;
      }
    } else {
      // Range crosses midnight (e.g., 22:00 to 02:00)
      if (totalMinute >= startMinutes || totalMinute < endMinutes) {
        return true;
      }
    }
  }
  return false;
}

void startAeration() {
  digitalWrite(aerationRelayPin, HIGH);
  digitalWrite(releasingRelayPin, LOW);
  digitalWrite(fanRelayPin, HIGH);
}

void startReleasing() {
  digitalWrite(releasingRelayPin, HIGH);
  digitalWrite(aerationRelayPin, LOW);
  digitalWrite(fanRelayPin, HIGH);
}

void startIdle() {
  digitalWrite(aerationRelayPin, LOW);
  digitalWrite(releasingRelayPin, LOW);
  digitalWrite(fanRelayPin, LOW);
}

void printStatus() {
  lcd.clear();
    // set cursor to first column, first row
  lcd.setCursor(0, 0);
  String time = String(getHour()) + ":" + String(getMinute()) + " ";
  lcd.print(time);
  if (digitalRead(aerationRelayPin) == HIGH) {
    lcd.print("AER ");
  }
  if (digitalRead(releasingRelayPin) == HIGH) {
    lcd.print("REL ");
  }
  if (nopCycleActive) {
    lcd.print("NOP ");
  }

  // set cursor to first column, second row
  lcd.setCursor(0,1);
  String secondLine = String(currentWaterLevel, 0) + " ";
  if (secondLine.length() <=5 ) {
    secondLine += WiFi.localIP().toString();
  } else if (loopCounter >= 56 && loopCounter <= 59) {
    secondLine = WiFi.localIP().toString();
  }
  lcd.print(secondLine);
}

int getSecond() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    return RTC.getSeconds();
  }
  return timeinfo.tm_sec;
}

int getHour() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    return RTC.getHours();
  }
  return timeinfo.tm_hour;
}

int getMinute() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    return RTC.getMinutes();
  }
  return timeinfo.tm_min;
}

void setupTimeSlots() {
  for (int i = 0; i < timeSlotArraySize; i++) {
    releaseSlots[i]  = -1;
    aerationSlots[i] = -1;
    idleSlots[i] = -1;
  }
  parseTimeSlots(aerationSlotsString, aerationSlots);
  parseTimeSlots(releaseSlotsString, releaseSlots);
  for (int i = 0; i < timeSlotArraySize; i++) {
    if (aerationSlots[i] == -1) {
      break;
    }
    Serial.println(aerationSlots[i]);
  }
  Serial.println("-----------------------------------------");
  for (int i = 0; i < timeSlotArraySize; i++) {
    if (releaseSlots[i] == -1) {
      break;
    }
    Serial.println(releaseSlots[i]);
  }
}

void parseTimeSlots(String timeString, int array[]) {
    int prevSemicolonIndex = -1;
    int currentSemicolonIndex = 0;
    int currIndex = 0;

    while (currentSemicolonIndex != -1) {
        currentSemicolonIndex = timeString.indexOf(';', prevSemicolonIndex + 1);

        String slotStr;
        if (currentSemicolonIndex == -1) {
            // Last segment
            slotStr = timeString.substring(prevSemicolonIndex + 1);
        } else {
            slotStr = timeString.substring(prevSemicolonIndex + 1, currentSemicolonIndex);
        }

        slotStr.trim(); // Remove any leading/trailing whitespace

        if (slotStr.length() > 0) {
            parseTimeSlot(slotStr, array, currIndex);
            currIndex += 2;
        }
        prevSemicolonIndex = currentSemicolonIndex;
    }
}

// Function to parse a time slot string (e.g., "HH:MM-HH:MM")
// slotStr: The time slot string to parse
// parsedTimes: Reference to a vector to store the parsed start and end minutes
// Returns: true on success, false on failure
bool parseTimeSlot(String slotStr, int array[], int currIndex) {
    int hyphenIndex = slotStr.indexOf('-');
    if (hyphenIndex == -1) {
        Serial.println("Error: Hyphen not found in time slot string.");
        return false;
    }

    String startTimeStr = slotStr.substring(0, hyphenIndex);
    String endTimeStr = slotStr.substring(hyphenIndex + 1);

    int startMinutes = parseTimeToMinutes(startTimeStr);
    int endMinutes = parseTimeToMinutes(endTimeStr);

    if (startMinutes == -1 || endMinutes == -1) {
        return false; // Error occurred during time parsing
    }
    array[currIndex] = startMinutes;
    array[currIndex + 1] = endMinutes;
    return true;
}

// Function to parse a time string (e.g., "HH:MM") into minutes from midnight
// timeStr: The time string to parse (e.g., "19:00")
// Returns: The total minutes from midnight (0-1439)
int parseTimeToMinutes(String timeStr) {
    int colonIndex = timeStr.indexOf(':');
    if (colonIndex == -1) {
        // Handle error: colon not found
        Serial.println("Error: Colon not found in time string.");
        return -1; // Or throw an error, depending on desired error handling
    }

    String hourStr = timeStr.substring(0, colonIndex);
    String minuteStr = timeStr.substring(colonIndex + 1);

    int hour = hourStr.toInt();
    int minute = minuteStr.toInt();

    // Basic validation for hour and minute
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        Serial.print("Error: Invalid hour or minute in time string: ");
        Serial.println(timeStr);
        return -1;
    }

    return hour * 60 + minute;
}

void updateRTCTime(bool force) {
  struct tm tm;
  if(!getLocalTime(&tm)) {
    return;
  }
  if ((tm.tm_sec == 0 && tm.tm_min == 0) || force) {
    RTC.setTime(tm.tm_hour, tm.tm_min, tm.tm_sec);
    RTC.setDate(tm.tm_mday, tm.tm_mon, tm.tm_year);
  }
}

void connectToWifi() {
    // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int numTries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    numTries++;
    if (numTries > 10) {
      return;
    }
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
            doc["freeHeap"] = esp_get_free_heap_size();
            doc["aerationCompressorRunning"] = digitalRead(aerationRelayPin) == HIGH ? "ON" : "OFF";
            doc["releaseCompressorRunning"] = digitalRead(releasingRelayPin) == HIGH ? "ON" : "OFF";
            doc["fanRunning"] = digitalRead(fanRelayPin) == HIGH ? "ON" : "OFF";
            doc["currentWaterLevel"] = currentWaterLevel;
            doc["realSensorReading"] = d;
            doc["isWaterLevelValueOK"] = isWaterLevelValueOK;
            configDoc["nopCycleActive"] = nopCycleActive ? 1 : 0;
            configDoc["testAeration"] = testAeration ? 1 : 0;
            configDoc["testRelease"] = testRelease ? 1 : 0;
            configDoc["aerationSlotsString"] = aerationSlotsString;
            configDoc["releaseSlotsString"] = releaseSlotsString;
            configDoc["nopAerationDuration"] = nopAerationDuration;
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

  nopCycleActive = configDoc["nopCycleActive"] == "1";
  testAeration = configDoc["testAeration"] == "1";
  testRelease = configDoc["testRelease"] == "1";

  const char* slots =  configDoc["aerationSlotsString"];
  aerationSlotsString = slots ? String(slots) : defaultAerationSlots;

  const char* slots2 =  configDoc["releaseSlotsString"];
  releaseSlotsString = slots2 ? String(slots2) : defaultReleaseSlots;

  nopAerationDuration = configDoc["nopAerationDuration"];
  accessPointEnabled = configDoc["accessPointEnabled"] ? configDoc["accessPointEnabled"] : 1;
  
  const char* ssidChar = configDoc["ssid"];
  ssid = ssidChar ? String(ssidChar) : defaultSsid;
  
  const char* passwordChar = configDoc["password"];
  password = passwordChar ? String(passwordChar) : defaultPassword;

  setupTimeSlots();
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
