/*
   Geiger.ino
   This code interacts with the Alibaba RadiationD-v1.1 (CAJOE) Geiger counter board
   and reports on Display readings in CPM (Counts Per Minute).
   To the Thingspeak CPH (Counts Per Hour) are reported
   Connect the output of the Geiger counter to pin inputPin.
   Install Thingspulse SSD1306 Library IFTTTWebhook and ThingSpeak
   Please notice you need to udate the https cerificatet signere certificate within IFTTTWebhook - doues not work with certificate manager.
   IFTTTWebhook does not work with parameter .…… i asume as results from api changes.
   Examle of my deice https://thingspeak.com/channels/1754536#
   Author Hans Carlos Hofmann
   Based on Andreas Spiess
   Based on initial work of Mark A. Heckler (@MkHeck, mark.heckler@gmail.com)
   License: MIT License
   Please use freely with attribution. Thank you!
   //--- a few tweaks by @twingall  13th July 2022 ---
   //Fix IFTTT connection issue i experienced; allow the actual uSv/Hr reading to be sent in the IFTTT notification
      //(you may need to edit the IFTTTwebhook.h file; update the fingerprint as of 9th July 2022: 61:62:75:FA:EA:5F:64:95:4A:F6:09:0F:59:C9:0D:E7:1E:6D:66:A3)
   //Made it WiFi agnostic; OLED should display even when wifi down. 
   //converted cpm to uSv/hr
*/
#define Version "V1.3.1"
#define CONS
#define WIFI
#define IFTT
#ifdef CONS
#define PRINT_DEBUG_MESSAGES
#endif
#ifdef WIFI
#include <WiFi.h>
#include <WiFiClientSecure.h>
#ifdef IFTT
#include "IFTTTWebhook.h"
#endif
#include <ThingSpeak.h>
#endif
#include <SSD1306.h>
#include <esp_task_wdt.h>
#define TUBE_FACTOR_SIEVERT 0.00812037037037 // this is for J305ß tube
#define WIFI_TIMEOUT_DEF 300 //changed from 30, for testing...
#define PERIOD_LOG 60                //Logging period 
#define PERIOD_THINKSPEAK 300        // in seconds, >60
#define WDT_TIMEOUT 10
#ifndef CREDENTIALS
// WLAN
#ifdef WIFI
#define mySSID "xxxxxx"
#define myPASSWORD "xxxxxx"

//IFTT
#ifdef IFTT
#define IFTTT_KEY "m4DeUPkeYXXXX" // replace m4DeUPkeYXXXX with your channel IFTTT key
#endif

// Thingspeak
#define SECRET_CH_ID 0000000      // replace 0000000 with your channel number
#define SECRET_WRITE_APIKEY "XYZ"   // replace XYZ with your channel write API Key
#endif

// IFTTT
#ifdef IFTT
#define EVENT_NAME "radiationHigh" // Name of your event name, set when you are creating the applet
#endif
#endif
//
//
IPAddress ip;
#ifdef WIFI
WiFiClient client;
WiFiClientSecure secure_client;
#endif

SSD1306  display(0x3c, 5, 4);
const int inputPin = 26;
int counts = 0;  // Tube events
int counts2 = 0;
int cpm = 0;                                             // CPM
float uSvh = cpm * TUBE_FACTOR_SIEVERT;
unsigned long lastCountTime;                            // Time measurement
unsigned long lastEntryThingspeak;
unsigned long startCountTime;                            // Time measurement
unsigned long startEntryThingspeak;

#ifdef WIFI
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;
#endif

void IRAM_ATTR ISR_impulse() { // Captures count of events from Geiger counter board
      counts++;
      counts2++;
}
void displayInit() {
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_24);
}
void displayInt(int dispInt, int x, int y) {
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(x, y, String(dispInt));
  display.setFont(ArialMT_Plain_24);
  display.display();
}
void displayFloat(float dispFloat, int x, int y) {
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(x, y, String(dispFloat));
  display.setFont(ArialMT_Plain_24);
  display.display();
}
void displayString(String dispString, int x, int y) {
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(x, y, dispString);
  display.setFont(ArialMT_Plain_24);
  display.display();
}

/****reset***/
void software_Reset() // Restarts program from beginning but does not reset the peripherals and registers
{
#ifdef CONS
  Serial.println("resetting by software");
#endif
  displayString("Myreset", 64, 15);
  delay(1000);
  esp_restart();
}
void IFTTT(int postValue) {
#ifdef WIFI
#ifdef IFTT
  IFTTTWebhook webhook(IFTTT_KEY, EVENT_NAME);
  if (!webhook.trigger(String(postValue).c_str())) {
#ifdef CONS
    Serial.println("Successfully sent to IFTTT");
  } else
  {
    Serial.println("IFTTT failed!");
#endif
  }
#endif
#endif
}

void postThingspeak( float value) {
  // Write to ThingSpeak. There are up to 8 fields in a channel, allowing you to store up to 8 different
  // pieces of information in a channel.  Here, we write to field 1.
#ifdef WIFI
  int x = ThingSpeak.writeField(myChannelNumber, 1, value, myWriteAPIKey);
#ifdef CONS
  if (x == 200) {
    Serial.println("Channel update successful");
  }
  else {
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }
#endif
#endif
}

void printStack()
{
#ifdef CONS
  char *SpStart = NULL;
  char *StackPtrAtStart = (char *)&SpStart;
  UBaseType_t watermarkStart = uxTaskGetStackHighWaterMark(NULL);
  char *StackPtrEnd = StackPtrAtStart - watermarkStart;
  Serial.printf("=== Stack info === ");
  Serial.printf("Free Stack is:  %d \r\n", (uint32_t)StackPtrAtStart - (uint32_t)StackPtrEnd);
#endif
}
void setup() {
  esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL); //add current thread to WDT watch
  
#ifdef CONS
  Serial.begin(115200);
  Serial.print("This is ") ; Serial.println(Version) ;
#endif 

  if (PERIOD_LOG > PERIOD_THINKSPEAK) {
#ifdef CONS
    Serial.println("PERIOD_THINKSPEAK has to be bigger than PERIODE_LOG");
#endif
    while (1);
  }
  displayInit();
#ifdef WIFI
  ThingSpeak.begin(client);  // Initialize ThingSpeak
#endif
  displayString("Welcome", 64, 0);
  displayString(Version, 64, 30);
  printStack();
#ifdef WIFI
#ifdef CONS
  Serial.println("Connecting to Wi-Fi");
#endif 
  WiFi.begin(mySSID, myPASSWORD);
  int wifi_loops = 0;
  int wifi_timeout = WIFI_TIMEOUT_DEF;
  while (WiFi.status() != WL_CONNECTED) {
    wifi_loops++;
#ifdef CONS
    Serial.print(".");
#endif
    delay(500);
    if (wifi_loops > wifi_timeout) software_Reset();
  }
#ifdef CONS
  Serial.println();
  Serial.println("Wi-Fi Connected");
#endif
#endif
  display.clear();
  displayString("Measuring", 64, 15);
  pinMode(inputPin, INPUT);                            // Set pin for capturing Tube events
#ifdef CONS
  Serial.println("Defined Input Pin");
#endif
  attachInterrupt(digitalPinToInterrupt(inputPin), ISR_impulse, FALLING);     // Define interrupt on falling edge
  Serial.println("Irq installed");
  startEntryThingspeak = lastEntryThingspeak = millis();
  startCountTime = lastCountTime = millis();
#ifdef CONS
  Serial.println("Initialized");
#endif  
}
int active = 0 ;
void loop() {
  esp_task_wdt_reset();
  if (millis() - lastCountTime > (PERIOD_LOG * 1000)) {
#ifdef CONS
    Serial.print("Counts: "); Serial.println(counts);
#endif
    cpm = (60000 * counts) / (millis() - startCountTime) ;
    uSvh = cpm * TUBE_FACTOR_SIEVERT;
    counts = 0 ;
    startCountTime = millis() ;
    lastCountTime += PERIOD_LOG * 1000;
    display.clear();
    displayString("Rad: uSv/hr", 64, 0);
    displayFloat(uSvh, 64, 30);//changed from cpm to μSvh
    if (uSvh >= 1.63) {
      if (active) {
        active = 0 ;
        display.clear();
        displayString("!! uSv/h !!", 64, 0);
        displayFloat(uSvh, 64, 30); //changed from cpm to μSvh

        int    HTTP_PORT   = 80;
        String HTTP_METHOD = "POST"; // or "GET"
        char   HOST_NAME[] = "maker.ifttt.com"; // hostname of web server:
        String PATH_NAME   = "/trigger/radiationHigh/with/key/m4DeUPkeYXXXX"; //!!replace 'radiationHigh' and 'm4DeUPkeYXXXX' with EVENT_NAME and IFTTT_KEY

          if(client.connect(HOST_NAME, HTTP_PORT)) {
            Serial.println("Connected to server");
          } else {
            Serial.println("connection failed");
          }
          // send HTTP request header

          client.println(HTTP_METHOD + " " + PATH_NAME + "?value1="+ String(uSvh) + "&value2=0&value3=0" + " HTTP/1.1"); //can add in more variables in place of '0')
          Serial.println(HTTP_METHOD + " " + PATH_NAME + "?value1="+ String(uSvh)+ "&value2=0&value3=0"+ " HTTP/1.1"); //debug: wanted to see how serial monitor printed this
          client.println("Host: " + String(HOST_NAME));
          client.println("Connection: close");
          client.println(); // 
      } ;
    }
    else if (uSvh < 0.813)
    {
      active = 1 ;
    } ;
#ifdef CONS
    Serial.print("uSv/hr: "); Serial.println(uSvh);
#endif
    printStack();
  }

  if (millis() - lastEntryThingspeak > (PERIOD_THINKSPEAK * 1000)) {
#ifdef CONS
    Serial.print("Counts2: "); Serial.println(counts2);
#endif
    int averageCPH = (int)(((float)3600000 * (float)counts2) / (float)(millis() - startEntryThingspeak));
    float averageMicroSvHr = averageCPH * (float)0.0001353395; //safe average background radiation is 1.5-3.5 mSv/yr =>> 1500-3500uSv/yr =>> 0.17 - 0.4 uSv/hr
#ifdef CONS
    Serial.print("Average uSv/hr: "); Serial.println(averageMicroSvHr);
#endif
    postThingspeak(averageMicroSvHr);
    lastEntryThingspeak += PERIOD_THINKSPEAK * 1000;
    startEntryThingspeak = millis();
    counts2=0;
  } ;
  delay(50);
  }
