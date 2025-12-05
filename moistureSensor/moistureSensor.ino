// #include <SPI.h>
// #include <Wire.h>
// #include <Adafruit_GFX.h>
// #include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#define ANALOG_IN 0


// Replace with your network credentials
const char* ssid = "ESP32-Access-Point";
const char* password = "123456789";

String baseUrl = "http://192.168.4.1/humidity?";

String sensorName = "Idk";
// #define SCREEN_WIDTH 128 // OLED display width, in pixels
// #define SCREEN_HEIGHT 64 // OLED display height, in pixels
// #define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
// Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int AirValue = 670;    //you need to replace this value with Value_1
const int WaterValue = 300;  //you need to replace this value with Value_2
int soilMoistureValue = 0;
int soilmoisturepercent = 0;



void setup() {
  pinMode(ANALOG_IN, INPUT);
  Serial.begin(9600);  // open serial port, set the baud rate to 9600 bps
  // display.begin(SSD1306_SWITCHCAPVCC, 0x3C); //initialize with the I2C addr 0x3C (128x64)
  // display.clearDisplay();

  WiFi.begin(ssid, password);
  Serial.println("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());
}


void loop() {
  soilMoistureValue = analogRead(ANALOG_IN);  //put Sensor insert into soil
  delay(1000);
  int soilmoisturepercent = map(soilMoistureValue, AirValue, WaterValue, 0, 100);
  Serial.println(soilmoisturepercent);
  String url = baseUrl + "sensor=1&hum=" + String(soilmoisturepercent);
  httpGETRequest(url);
  //sendData(soilmoisturepercent);
  // if (soilmoisturepercent > 100) {
  //   Serial.println("100 %");

  //   display.setCursor(45, 0);  //oled display
  //   display.setTextSize(2);
  //   display.setTextColor(WHITE);
  //   display.println("Soil");
  //   display.setCursor(20, 15);
  //   display.setTextSize(2);
  //   display.setTextColor(WHITE);
  //   display.println("Moisture");

  //   display.setCursor(30, 40);  //oled display
  //   display.setTextSize(3);
  //   display.setTextColor(WHITE);
  //   display.println("100 %");
  //   display.display();

  //   delay(250);
  //   display.clearDisplay();
  // } else if (soilmoisturepercent < 0) {
  //   Serial.println("0 %");

  //   display.setCursor(45, 0);  //oled display
  //   display.setTextSize(2);
  //   display.setTextColor(WHITE);
  //   display.println("Soil");
  //   display.setCursor(20, 15);
  //   display.setTextSize(2);
  //   display.setTextColor(WHITE);
  //   display.println("Moisture");

  //   display.setCursor(30, 40);  //oled display
  //   display.setTextSize(3);
  //   display.setTextColor(WHITE);
  //   display.println("0 %");
  //   display.display();

  //   delay(250);
  //   display.clearDisplay();
  // } else if (soilmoisturepercent >= 0 && soilmoisturepercent <= 100) {
  //   Serial.print(soilmoisturepercent);
  //   Serial.println("%");

  //   display.setCursor(45, 0);  //oled display
  //   display.setTextSize(2);
  //   display.setTextColor(WHITE);
  //   display.println("Soil");
  //   display.setCursor(20, 15);
  //   display.setTextSize(2);
  //   display.setTextColor(WHITE);
  //   display.println("Moisture");

  //   display.setCursor(30, 40);  //oled display
  //   display.setTextSize(3);
  //   display.setTextColor(WHITE);
  //   display.println(soilmoisturepercent);
  //   display.setCursor(70, 40);
  //   display.setTextSize(3);
  //   display.println(" %");
  //   display.display();

  //   delay(250);
  //   display.clearDisplay();
  // }
}

String httpGETRequest(String url) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wifi not connected");
    return "";
  }
  WiFiClient client;
  HTTPClient http;

  // Your Domain name with URL path or IP address with path
  http.begin(client, url);

  // Send HTTP POST request
  int httpResponseCode = http.GET();

  String payload = "--";

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();
  return payload;
}

