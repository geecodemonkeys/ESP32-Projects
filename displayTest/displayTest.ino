
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
// #include <Adafruit_SSD1306.h>
#include <Adafruit_SH1106.h>

// Declaration for SSD1306 display connected using software SPI (default case):
#define OLED_MOSI   12
#define OLED_CLK   13
#define OLED_DC    9
#define OLED_CS    10
#define OLED_RESET 6

#define SCREEN_WIDTH 128 // OLED display width, in pixels   

#define SCREEN_HEIGHT 64 // OLED display height, in pixels

//  Adafruit_SH1106 dis(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
 Adafruit_SH1106 dis(OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
// const int ledPin = 13;


#define OLED_RESET 4
// Adafruit_SH1106 dis(OLED_RESET);

void setup()
{
  Serial.begin(9600);
  // dis.begin(SH1106_SWITCHCAPVCC, 0x3C);
  dis.begin();
  Serial.println("Uraaaa");
  dis.drawPixel(30,30,WHITE);


  dis.display();
  delay(2000);
  dis.clearDisplay();
}

void loop()
{
  dis.setTextSize(2);
  dis.setTextColor(WHITE);
  dis.setCursor(0,0);
  dis.println("Hello");
  dis.setTextSize(2);
  dis.setTextColor(WHITE);
  dis.println("World!");
  dis.display();
  delay(2000);
}

// void setup() {
//   display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // Initialize the OLED display
//   display.clearDisplay();
//   display.setTextSize(2); // Set font size
//   display.setTextColor(WHITE);
//   display.setCursor(0, 10);
//   display.println("Hello World!");
//   display.display();
// }

// void loop() {
//   // You can add more text or graphics here, for example:
//   display.clearDisplay();
//   display.setCursor(0, 0);
//   display.println("Time:");
//   display.setCursor(0, 20);
//   display.print(millis() / 1000); // Display milliseconds since startup
//   display.display();
//   delay(1000);
// }