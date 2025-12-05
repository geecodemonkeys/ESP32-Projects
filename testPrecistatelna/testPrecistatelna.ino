#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

const char* timeApiUrl = "https://timeapi.io/api/time/current/zone?timeZone=Europe%2FSofia";

// JSON buffer size (adjust if your JSON response is larger)
// A good way to determine this is to print the raw JSON and calculate its size.
// For this API, 256-512 bytes should be sufficient.
const int JSON_DOC_SIZE = 512;



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

void setup(){
  Serial.begin(9600);

  if (accessPointEnabled == 1) {
    //Wifi
    createHotspot();
  } else {
    connectToWifi();
  }

  //start webserver
  server.begin();

  getTime();
}



void loop() {

  Serial.println("URAAAAAA");


  //handle web request
  handleWebRequest();

  
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
      Serial.println("IP address: ");
      Serial.println(IP);
    }
  }
  delay(10000);
}

void getTime() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  HTTPClient http;
  WiFiClientSecure client; 
  // This line disables SSL certificate validation.
  // Use ONLY for development/testing. For production, consider adding proper certificate validation.
  client.setInsecure(); 
  Serial.println("\nMaking HTTP request to Time API...");

  http.begin(client, timeApiUrl); // Specify the URL
  http.setTimeout(8000);      // Set response timeout to 8 seconds

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    if (httpResponseCode == HTTP_CODE_OK) { // HTTP_CODE_OK == 200
      String payload = http.getString();
      Serial.println("Received payload:");
      Serial.println(payload);

      // Deserialize JSON
      StaticJsonDocument<JSON_DOC_SIZE> doc; // Create a JSON document buffer
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
      } else {
        // Extract data
        const char* dateTime = doc["dateTime"]; // e.g., "2025-06-02T13:45:30.1234567"
        const char* time = doc["time"];         // e.g., "13:45:30"
        const char* date = doc["date"];         // e.g., "06/02/2025"
        const char* timeZone = doc["timeZone"]; // e.g., "Europe/Sofia"

        Serial.println("\n--- Parsed Time Data ---");
        Serial.print("Full Date/Time: "); Serial.println(dateTime);
        Serial.print("Time (HH:MM:SS): "); Serial.println(time);
        Serial.print("Date (MM/DD/YYYY): "); Serial.println(date);
        Serial.print("Time Zone: "); Serial.println(timeZone);
        Serial.println("------------------------");
      }
    }
  } else {
    Serial.print("Error on HTTP request: ");
    Serial.println(httpResponseCode);
    Serial.println(http.errorToString(httpResponseCode)); // Print detailed error
  }
  http.end(); // Free resources
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
              
            }
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:application/json");
            client.println("Access-Control-Allow-Origin: *");
            client.println("Connection: close");
            client.println();

            JsonDocument doc, configDoc;
            doc["time"] = "time";
            
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
