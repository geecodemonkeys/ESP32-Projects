#include <WiFi.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include "time.h"
#include "max6675.h"


const char* ntpServer = "europe.pool.ntp.org";
const long  gmtOffset_sec = 2 * 3600;
const int   daylightOffset_sec = 3600;


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


const int thermoDO = 19;
const int thermoCS = 5;
const int thermoCLK = 18;

MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);


//-------------------wifi----------------------
// Replace with your network credentials
String ssid     = "fi2";
String password = "mamatatko";
int accessPointEnabled = 0;

const String defaultSsid     = "ControlerZaKamina";
const String defaultPassword = "mamatatko";

// Variable to store the HTTP request
String header;
WiFiServer server(80);
//---------ip adress populated when initialised---
IPAddress IP;
IPAddress nullIP(0,0,0,0);
//---------------------------------------------
int loopCounter = 0;
//---------------------------------------------

float temp1, temp2, termoCouple;

void setup(){
  Serial.begin(9600);

  // initialize LCD
  lcd.init();
  // turn on LCD backlight                      
  lcd.backlight();

  //Temp sensors
  sensor1.begin();

  if (accessPointEnabled == 1) {
    //Wifi
    createHotspot();
  } else {
    connectToWifi();
  }

  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  //start webserver
  server.begin();
}



void loop() {
  //get temps
  sensor1.requestTemperatures();
  temp1 = sensor1.getTempCByIndex(0);
  temp2 = sensor1.getTempCByIndex(1);
  termoCouple = thermocouple.readCelsius();
  Serial.println(termoCouple);
  handleWebRequest();
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
      }
    }
  }
  delay(1000);
}

void printStatus() {
  lcd.clear();
    // set cursor to first column, first row
  lcd.setCursor(0, 0);
  String t1 = String(temp1, 0);
  String t2 = String(temp2, 0);
  String tc = String(termoCouple, 0);
  String time = String(getHour()) + ":" + String(getMinute());
  String firstLine = tc + " " + t1 + " " + t2 + " " + time;
  lcd.print(firstLine);


  // set cursor to first column, second row
  lcd.setCursor(0,1);
  
  String secondLine = WiFi.localIP().toString();
  lcd.print(secondLine);
}


void handleWebRequest() {
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
              //TODO
            }
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:application/json");
            client.println("Access-Control-Allow-Origin: *");
            client.println("Connection: close");
            client.println();

            JsonDocument doc;
            doc["temp1"] = temp1;
            doc["temp2"] = temp2;
            doc["termoCouple"] = termoCouple;
            
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

int getHour() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    return 0;
  }
  return timeinfo.tm_hour;
}

int getMinute() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    return 0;
  }
  return timeinfo.tm_min;
}

int getSecond() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    0;
  }
  return timeinfo.tm_sec;
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



