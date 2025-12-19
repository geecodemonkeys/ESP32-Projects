#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include "time.h"

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

const char* ntpServer = "europe.pool.ntp.org";
const long  gmtOffset_sec = 2 * 3600;
const int   daylightOffset_sec = 3600;

// GPIO where the DS18B20 is connected to
const int oneWireBus = 12;     

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);

// Replace with your network credentials
const char* ssid     = "fi2";
const char* password = "mamatatko";

// Variable to store the HTTP request
String header;
WiFiServer server(80);
//---------ip adress populated when initialised---
IPAddress IP;
IPAddress nullIP(0,0,0,0);


// Set your Static IP address
IPAddress local_IP(192, 168, 8, 100);
// Set your Gateway IP address
IPAddress gateway(192, 168, 8, 1);

IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);   //optional
IPAddress secondaryDNS(8, 8, 4, 4); //optional

int loopCounter = 0;

float t1, t2;
float maxT1 = -100.0;
float maxT2 = -100.0;
float minT1 = 100.0;
float minT2 = 100.0;

void setup() {
  // Start the Serial Monitor
  Serial.begin(115200);
  // Start the DS18B20 sensor
  sensors.begin();
  connectToWifi();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  server.begin();
}

void loop() {
  sensors.requestTemperatures(); 
  t1 = sensors.getTempCByIndex(0);
  t2 = sensors.getTempCByIndex(1);
  Serial.println(t2);

  maxT1 = max(maxT1, t1);
  maxT2 = max(maxT2, t2);
  minT1 = min(minT1, t1);
  minT2 = min(minT2, t2);

  int sec = getSecond();
  int minute = getMinute();
  int hour = getHour();

  if (hour == 0 && minute == 0 && sec >= 35) {
    ESP.restart();
  }

  handleWebRequest();

  loopCounter++;
  if (loopCounter >= 60) {
    loopCounter = 0;
    Serial.println("Wifi Status: " + String(WiFi.status()) + " IP: " + WiFi.localIP().toString());
    //Check if wifi is connected, hotspot can restart at any time
    if ((WiFi.status() != WL_CONNECTED || WiFi.localIP() == nullIP)) {
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
  delay(10000);
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
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:application/json");
            client.println("Access-Control-Allow-Origin: *");
            client.println("Connection: close");
            client.println();

            JsonDocument doc, configDoc;
            doc["t1"] = t1;
            doc["t2"] = t2;
            doc["maxT1"] = maxT1;
            doc["maxT2"] = maxT2;
            doc["minT1"] = minT1;
            doc["minT2"] = minT2;
            
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

int getSecond() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    return -1;
  }
  return timeinfo.tm_sec;
}

int getHour() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    return -1;
  }
  return timeinfo.tm_hour;
}

int getMinute() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    return -1;
  }
  return timeinfo.tm_min;
}

void connectToWifi() {
    // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }
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

