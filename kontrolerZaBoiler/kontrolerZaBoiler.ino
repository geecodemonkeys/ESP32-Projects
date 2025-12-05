#include <WiFi.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <I2C_RTC.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SD.h"

static DS1307 RTC;


// set the LCD number of columns and rows
int lcdColumns = 16;
int lcdRows = 2;

// set LCD address, number of columns and rows
// also 0x50 0x68
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);


// GPIO where the DS18B20 is connected to
const int oneWireBusSensor1 = 32;
const int boilerACHeaterRelayPin = 27;
const int leftRelayPin = 25;
const int rightRelayPin = 26;

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWireSensor1(oneWireBusSensor1);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensor1(&oneWireSensor1);


float targetTempInTheBoiler = 50.0;
float boilerSunActivationTempDiff = 10.0;
float boilerSunDeActivationTempDiff = 2.0;
float boilerWoodActivationTempDiff = 2.0;
float boilerWoodDeActivationTempDiff = 0.5;
float electricityActivationTempDiff = 10;
float woodStoveWorkingTempThreshold = 40.0;
float sunWorkingTempThreshold = 50.0;
float frostProtectionStartTemp = 5.0;
float frostProtectionStopTemp = 6.0;
int acHeatingIsAllowed = 1;
int accessPointEnabled = 1;
float releaseTempInBoiler = 70.0; //temp above to release water from the boiler

bool heatingWithSun = false;
bool heatingWithWood = false;
bool heatingWithElectricity = false;
bool frostProtection = false;

//-------------------wifi----------------------
// Replace with your network credentials
String ssid     = "kontrolerZaBoiler";
String password = "mamatatko";

const String defaultSsid     = "ControlerZaBoiler";
const String defaultPassword = "mamatatko";

// Variable to store the HTTP request
String header;
WiFiServer server(80);
//---------ip adress populated when initialised---
IPAddress IP;
//---------------------------------------------
int loopCounter = 0;
//---------------------------------------------

void setup(){
  Serial.begin(9600);

  RTC.begin();
  
  //setTime();

  // initialize LCD
  lcd.init();
  // turn on LCD backlight                      
  lcd.backlight();

  //Temp sensors
  sensor1.begin();

  pinMode(boilerACHeaterRelayPin, OUTPUT);
  pinMode(leftRelayPin, OUTPUT);
  pinMode(rightRelayPin, OUTPUT);

  digitalWrite(boilerACHeaterRelayPin, LOW);
  digitalWrite(leftRelayPin, HIGH); //logic inverted on those relays
  digitalWrite(rightRelayPin, HIGH);

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
}



void loop() {
  //get temps
  sensor1.requestTemperatures();
  float tempBoiler = sensor1.getTempCByIndex(0);
  float tempSun = sensor1.getTempCByIndex(1);
  float tempWood = sensor1.getTempCByIndex(2);

  int prevACHeatingIsAllowed = acHeatingIsAllowed; 
  //handle web request
  handleWebRequest(tempBoiler, tempWood, tempSun);

  //heat with the sun start
  if (!heatingWithSun && tempSun >= sunWorkingTempThreshold && (tempSun - tempBoiler > boilerSunActivationTempDiff && tempBoiler < targetTempInTheBoiler)) { // add histeresis
    heatingWithSun = true;
    digitalWrite(rightRelayPin, LOW);
  }

  //heat with the sun stop
  if (heatingWithSun && (tempSun  - tempBoiler <= boilerSunDeActivationTempDiff || tempBoiler >= targetTempInTheBoiler)) { // add histeresis
    heatingWithSun = false;
    digitalWrite(rightRelayPin, HIGH);
  }
  
  //heat with wood start
  if (!heatingWithWood && tempWood >= woodStoveWorkingTempThreshold && (tempWood - tempBoiler > boilerWoodActivationTempDiff && tempBoiler < targetTempInTheBoiler)) { // add histeresis
    heatingWithWood = true;
    digitalWrite(leftRelayPin, LOW);
  }

    //heat with wood stop
  if (heatingWithWood && (tempWood - tempBoiler <= boilerWoodDeActivationTempDiff || tempBoiler > targetTempInTheBoiler)) { // add histeresis
    heatingWithWood = false;
    digitalWrite(leftRelayPin, HIGH);
  }

  //heat with electricity
  if (acHeatingIsAllowed != 0
    && !heatingWithWood 
    && !heatingWithSun 
    && tempSun < sunWorkingTempThreshold 
    && tempWood < woodStoveWorkingTempThreshold 
    && !heatingWithElectricity 
    && tempBoiler < targetTempInTheBoiler - electricityActivationTempDiff) {

    heatingWithElectricity = true;
    digitalWrite(boilerACHeaterRelayPin, HIGH);
  }

//-------------------------heat with electricity stop-------------------------------------
  if (heatingWithElectricity && 
    (tempBoiler >= targetTempInTheBoiler || heatingWithWood || heatingWithSun
    || tempSun >= sunWorkingTempThreshold
    || tempWood >= woodStoveWorkingTempThreshold)) {

    heatingWithElectricity = false;
    digitalWrite(boilerACHeaterRelayPin, LOW);
  }

      //heat with electricity stop
  if (prevACHeatingIsAllowed != 0 && acHeatingIsAllowed == 0 && heatingWithElectricity) {
    heatingWithElectricity = false;
    digitalWrite(boilerACHeaterRelayPin, LOW);
  }
//-------------------------------------------------------------------------------------------
  //protect from frost ON
  if (!heatingWithElectricity && !frostProtection && tempBoiler <= frostProtectionStartTemp) {
    frostProtection = true;
    digitalWrite(boilerACHeaterRelayPin, HIGH);
  }

  if (heatingWithElectricity && frostProtection) {
    frostProtection = false; //do not turn off the ac heater
  }

  //protect from frost OFF
  if (frostProtection && tempBoiler > frostProtectionStopTemp) {
    frostProtection = false;
    digitalWrite(boilerACHeaterRelayPin, LOW);
  }

  //heat on timer mainly during the night on cheap electricity


  //display alert if temp diff between boiler or wood stove or sun collector is bigger than 10 and 20 respectivly

  //display results 
  printCurrentTemps(tempBoiler, tempWood, tempSun);

  loopCounter++;
  if (loopCounter >= 60) {
    loopCounter = 0;
  }
  //log something
  delay(1000);
}

void printCurrentTemps(float tempBoiler,float tempWood, float tempSun) {
  lcd.clear();
    // set cursor to first column, first row
  lcd.setCursor(0, 0);
  // print message
  String strBoiler = String(tempBoiler, 1);
  String strWood = String(tempWood, 1);
  String strSun = String(tempSun, 1);
  lcd.print(strBoiler + " " + strWood + " " + strSun);
  // set cursor to first column, second row
  lcd.setCursor(0,1);
  String woodOn = digitalRead(leftRelayPin) == LOW ? "WOOD " : "";
  String sunOn = digitalRead(rightRelayPin) == LOW ? "SUN " : "";
  String elOn = digitalRead(boilerACHeaterRelayPin) == HIGH ? (frostProtection ? "FROST " : "AC " ): "";
  String secondLine = woodOn  + sunOn  + elOn;
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

void handleWebRequest(float tempBoiler, float tempWood, float tempSun) {
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
            doc["tempBoiler"] = tempBoiler;
            doc["tempWood"] = tempWood;
            doc["tempSun"] = tempSun;
            doc["woodHeatingValve"] = digitalRead(leftRelayPin) == LOW ? "ON" : "OFF";
            doc["sunHeatingPump"] = digitalRead(rightRelayPin) == LOW ? "ON" : "OFF";
            doc["acHeatingElement"] = digitalRead(boilerACHeaterRelayPin) == HIGH ? "ON" : "OFF";
            configDoc["targetTempInTheBoiler"] = targetTempInTheBoiler;
            configDoc["boilerSunActivationTempDiff"] = boilerSunActivationTempDiff;
            configDoc["boilerSunDeActivationTempDiff"] = boilerSunDeActivationTempDiff;
            configDoc["boilerWoodActivationTempDiff"] = boilerWoodActivationTempDiff;
            configDoc["boilerWoodDeActivationTempDiff"] = boilerWoodDeActivationTempDiff;
            configDoc["electricityActivationTempDiff"] = electricityActivationTempDiff;
            configDoc["woodStoveWorkingTempThreshold"] = woodStoveWorkingTempThreshold;
            configDoc["sunWorkingTempThreshold"] = sunWorkingTempThreshold;
            configDoc["frostProtectionStartTemp"] = frostProtectionStartTemp;
            configDoc["frostProtectionStopTemp"] = frostProtectionStopTemp;
            configDoc["acHeatingIsAllowed"] = acHeatingIsAllowed;
            configDoc["accessPointEnabled"] = accessPointEnabled;
            configDoc["ssid"] = ssid;
            configDoc["password"] = password;
            configDoc["localDateTime"] = RTC.getDateTimeString();
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

  targetTempInTheBoiler = configDoc["targetTempInTheBoiler"];
  boilerSunActivationTempDiff = configDoc["boilerSunActivationTempDiff"];
  boilerSunDeActivationTempDiff = configDoc["boilerSunDeActivationTempDiff"];
  boilerWoodActivationTempDiff = configDoc["boilerWoodActivationTempDiff"];
  boilerWoodDeActivationTempDiff = configDoc["boilerWoodDeActivationTempDiff"];
  electricityActivationTempDiff = configDoc["electricityActivationTempDiff"];
  woodStoveWorkingTempThreshold = configDoc["woodStoveWorkingTempThreshold"];
  sunWorkingTempThreshold = configDoc["sunWorkingTempThreshold"];
  frostProtectionStartTemp = configDoc["frostProtectionStartTemp"];
  frostProtectionStopTemp = configDoc["frostProtectionStopTemp"];
  acHeatingIsAllowed = configDoc["acHeatingIsAllowed"] ? configDoc["acHeatingIsAllowed"] : 1;
  accessPointEnabled = configDoc["accessPointEnabled"] ? configDoc["accessPointEnabled"] : 1;
  
  const char* ssidChar = configDoc["ssid"];
  ssid = ssidChar ? String(ssidChar) : defaultSsid;
  
  const char* passwordChar = configDoc["password"];
  password = passwordChar ? String(passwordChar) : defaultPassword;
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
