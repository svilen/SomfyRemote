/**
 * Somfy Remote
 * Auto closes Somfy blinds between 22 March (Spring) and 22 Oct at 7am
 * Measures temp with BME280 and if room temp is > 28C tries to close the blinds max 5 times a day
 * 
 * Wemos D1 mini module
 * https://bigl.es/tooling-tuesday-wemos-d1-mini-micropython/
 * 
 * Wemod D1 mini PIN layout
 * https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
 * 
 * Wiring description
 * 
 * D5/GPIO14 - Blink LED
 * D6/GPIO12 - PROG Button
 * D7/GPIO13 - 433Mhz TX Module
 * 
 * D1/GPIO5 - I2C SCL
 * D2/GPIO4 - I2C SDA
 * 
 * Possible buttons to be send to Somfy motor
 * 
 * String     / HEX
 * "My"       / 1       - The My button pressed
 * "Up"       / 2       - The Up button pressed
 * "MyUp"     / 3       - The My and Up button pressed at the same time
 * "Down"     / 4       - The Down button pressed
 * "MyDown"   / 5     - The My and Down button pressed at the same time
 * "UpDown"   / 6     - The Up and Down button pressed at the same time
 * "Prog"     / 8     - The Prog button pressed
 * "SunFlag"  / 9   - Enable sun and wind detector
 * "Flag"     / A   - Disable sun detector
 * 
 * @since   Apr 2 2021
 * @author  Svilen Spasov <svilen@svilen.com>
 * 
 */
#include <EEPROM.h>
#include <EEPROMRollingCodeStorage.h>
#include <SomfyRemote.h>

#define EMITTER_GPIO 13
#define EEPROM_ADDRESS 2
#define REMOTE 0x5184c8 // Change this for every

EEPROMRollingCodeStorage rollingCodeStorage(EEPROM_ADDRESS);
SomfyRemote somfyRemote(EMITTER_GPIO, REMOTE, &rollingCodeStorage);

// Date and time functions using a DS1307 RTC connected via I2C and Wire lib
#include "RTClib.h"

RTC_DS1307 rtc;

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

/* BME280 */
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme; // I2C
const byte BME_ADDRESS = 0x76;

/* NTPClient */
#if !( defined(ESP8266) ||  defined(ESP32) )
#error This code is intended to run on the ESP8266 or ESP32 platform! Please check your Tools->Board setting.
#endif

#include <NTPClient_Generic.h>

#if (ESP32)
#include <WiFi.h>
#elif (ESP8266)
#include <ESP8266WiFi.h>
#endif
#include <WiFiConnector.h>
#ifndef WIFI_SSID
  #define WIFI_SSID       "Arisa2G"             // your network SSID (name)
  #define WIFI_PASSPHRASE "fa$slk978743hkfa"    // your network password
#endif

WiFiConnector wifi(WIFI_SSID, WIFI_PASSPHRASE);

#include <WiFiUdp.h>

WiFiUDP ntpUDP;
#define TIME_ZONE_OFFSET_HRS (3)
NTPClient timeClient(ntpUDP);

/* SOMFY BLINDS */
// #define DEBUG true; // debug bit

const int CLOSE_BLINDS_HOUR = 7;                // close blinds at 7am every morning
const int CLOSE_BLINDS_TEMP = 33;               // close blinds room temp
const int CLOSE_BLINDS_MAX_COUNT_PER_DAY = 5;   // close blinds max per day
#define EEPROM_ADDRESS_BLINDS_DATE 1
#define EEPROM_ADDRESS_BLINDS_COUNT 0
const int BLINK_LED_ID = 14;

/* PROG BUTTON */
#include <Arduino.h>
#include <AceButton.h>
using namespace ace_button;
const int PROG_BUTTON_ID = 12;
AceButton progButton(PROG_BUTTON_ID);

void handleProgButtonEvent(AceButton*, uint8_t eventType, uint8_t);

/* Timer for Async call of closeBlinds */
#include <arduino-timer.h>
auto timer = timer_create_default();

void setup() {
  // initialize digital pin as an output.
  pinMode(BLINK_LED_ID, OUTPUT);
  digitalWrite(BLINK_LED_ID, LOW);    // turn the LED off by making the voltage LOW
  
  // initialize PROG Button pin as input
  pinMode(PROG_BUTTON_ID, INPUT);
  // We use the AceButton::init() method here instead of using the constructor
  // to show an alternative. Using init() allows the configuration of the
  // hardware pin and the button to be placed closer to each other.
  progButton.init(PROG_BUTTON_ID, LOW);
  // Configure the ButtonConfig with the event handler, and enable the LongPress
  // and RepeatPress events which are turned off by default.
  ButtonConfig* buttonConfig = progButton.getButtonConfig();
  buttonConfig->setEventHandler(handleProgButtonEvent);
  buttonConfig->setFeature(ButtonConfig::kFeatureClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
  buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);
  
  Serial.begin(115200);

  somfyRemote.setup();

#if defined(ESP32)
  if (!EEPROM.begin(4)) {
    Serial.println("failed to initialise EEPROM");
    delay(1000);
  }
#elif defined(ESP8266)
  EEPROM.begin(4);
#endif
  // clean EEPROM
  EEPROM.write(EEPROM_ADDRESS_BLINDS_COUNT, 0);
  EEPROM.write(EEPROM_ADDRESS_BLINDS_DATE, 0);

        
  // BME
  if (! bme.begin(BME_ADDRESS)) {
      #ifdef DEBUG
      Serial.println("Could not find a valid BME280 sensor, check wiring!");
      #endif
  }
  delay(500);
         
  // WiFi
  initWifiHardware();
  initWifi();
  wifi.connect();

  // RTC
  if (! rtc.begin()) {
    #ifdef DEBUG
    Serial.println("Couldn't find RTC");
    #endif
  }

  if (! rtc.isrunning()) {
    #ifdef DEBUG
    Serial.println("RTC is NOT running, let's set the time!");
    #endif
    
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    
    int currentYear, currentMonth, currentDay, currentHour, currentMinutes, currentSeconds;
    getNTPDateTime(currentYear, currentMonth, currentDay, currentHour, currentMinutes, currentSeconds);
    #ifdef DEBUG
    Serial.println("year: " + String(currentYear) + " month: " + String(currentMonth) + " day: " + String(currentDay));
    #endif
    rtc.adjust(DateTime(currentYear, currentMonth, currentDay, currentHour, currentMinutes, currentSeconds));
  }

  // Setup close blinds
  timer.every(1000, closeBlindsToPredefinedPosition);
}

void loop() {
  // check if PROG Button was pressed
  progButton.check();
  
  // manage Wifi
  wifi.loop();

  // tick the timer
  timer.tick();
}

void initWifiHardware()
{
  WiFi.disconnect(true);
  delay(1000);
  #ifdef DEBUG
  Serial.println("Wifi will be started in 500ms..");
  #endif
}

void initWifi() {
  wifi.init();
  wifi.on_connected([&](const void* message)
  {
    #ifdef DEBUG
    Serial.print("Wifi connected with IP: ");
    Serial.println(WiFi.localIP());
    #endif
  });

  wifi.on_connecting([&](const void* message)
  {
    #ifdef DEBUG
    Serial.print("Connecting to ");
    Serial.println(wifi.get("ssid"));
    #endif
    delay(200);
  });
}

void getNTPDateTime(int & currentYear, int & currentMonth, int & currentDay, int & currentHour, int & currentMinutes, int & currentSeconds) {
  timeClient.begin();

  timeClient.setTimeOffset(3600 * TIME_ZONE_OFFSET_HRS);
  // default 60000 => 60s. Set to once per hour
  timeClient.setUpdateInterval(SECS_IN_HR);
  while (!timeClient.updated()) {
    delay ( 500 );
    #ifdef DEBUG
    Serial.print ( "." );
    #endif
    timeClient.forceUpdate();
  }

  #ifdef DEBUG
  Serial.println("Using NTP Server " + timeClient.getPoolServerName());
  #endif

  currentYear = timeClient.getYear();
  currentMonth = timeClient.getMonth();
  currentDay = timeClient.getDay();
  currentHour = timeClient.getHours();
  currentMinutes = timeClient.getMinutes();
  currentSeconds = timeClient.getSeconds();
  
  timeClient.end();
}

/**
 * Serial print variable types
 * This is the concept of polymorphism where multiple functions with different parameter types are created but with the same function name.
 * During run time, the function matching the right number of arguments and argument type(s) will get called.
 */
void types(String a) { Serial.println("it's a String"); }
void types(int a) { Serial.println("it's an int"); }
void types(char *a) { Serial.println("it's a char*"); }
void types(float a) { Serial.println("it's a float"); }
void types(bool a) { Serial.println("it's a bool"); }

// Main functions
bool closeBlindsToPredefinedPosition(void *) {
  // read a byte from the current address of the EEPROM
  byte lastCloseBlindsDate = EEPROM.read(EEPROM_ADDRESS_BLINDS_DATE);
  byte closeBlindsCount = EEPROM.read(EEPROM_ADDRESS_BLINDS_COUNT);

  double bmeTemp = bme.readTemperature();

  #ifdef DEBUG
  Serial.println("BME Temp: " + String(bmeTemp));
  Serial.println("count: " + String(closeBlindsCount));
  Serial.println("date: " + String(lastCloseBlindsDate));
  #endif
  
  DateTime now = rtc.now();     // Get the RTC info
  // call logic only if we have current date from the RTC clock
  if ( now.year() > 2020 ) {
    // if date is between 22 March and 22 Oct close blinds every morning at 7am
    // or if room temp is higher thank 28C and we have not closed blinds more than 5 time today
    if ( 
        (
          now.month() > 3 || (now.month() == 3 && now.day() >= 22 )
        )
        && (
          now.month() < 11 || (now.month() == 10 && now.day() <= 23 )
        )
    ) {
      
      if ( now.hour() == CLOSE_BLINDS_HOUR && lastCloseBlindsDate != now.day() ) {
        #ifdef DEBUG
        Serial.println("CLOSE_BLINDS_HOUR hit ... closing blinds");
        #endif
        
        EEPROM.write(EEPROM_ADDRESS_BLINDS_COUNT, 0);
        
        // save current date so we do not close blinds more than once a day
        EEPROM.write(EEPROM_ADDRESS_BLINDS_DATE, now.day());
        
        pressSomfyButton("My");
      }
      else if ( bmeTemp >= CLOSE_BLINDS_TEMP && closeBlindsCount < CLOSE_BLINDS_MAX_COUNT_PER_DAY ) {
        #ifdef DEBUG
        Serial.println("bmeTemp too high ... closing blinds");
        #endif
        
        EEPROM.write(EEPROM_ADDRESS_BLINDS_COUNT, (closeBlindsCount+1));
        
        pressSomfyButton("My");
      }
    }
  }

  return true;
}

/**
 * Send command to Somfy. Possible options - string or HEX
 * 
 * String     / HEX
 * "My"       / 1       - The My button pressed
 * "Up"       / 2       - The Up button pressed
 * "MyUp"     / 3       - The My and Up button pressed at the same time
 * "Down"     / 4       - The Down button pressed
 * "MyDown"   / 5       - The My and Down button pressed at the same time
 * "UpDown"   / 6       - The Up and Down button pressed at the same time
 * "Prog"     / 8       - The Prog button pressed
 * "SunFlag"  / 9       - Enable sun and wind detector
 * "Flag"     / A       - Disable sun detector
 */
void pressSomfyButton(String button) {
  const Command command = getSomfyCommand(button);
  somfyRemote.sendCommand(command);

  #ifdef DEBUG
  Serial.println("button pressed: "+ button);
  #endif
  
  // blink LED
  blinkLED();
}

void blinkLED() {
  digitalWrite(BLINK_LED_ID, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(10);                       // wait for a second
  digitalWrite(BLINK_LED_ID, LOW);    // turn the LED off by making the voltage LOW
}

void handleProgButtonEvent(AceButton* /* button */, uint8_t eventType, uint8_t buttonState) {
  #ifdef DEBUG
  // Print out a message for all events.
  Serial.print(F("handleEvent(): eventType: "));
  Serial.print(eventType);
  Serial.print(F("; buttonState: "));
  Serial.println(buttonState);
  #endif
  
  switch (eventType) {
    /*case AceButton::kEventClicked:
      pressSomfyButton("Up");
      break;
    case AceButton::kEventDoubleClicked:
      pressSomfyButton("Down");
      break;*/
    case AceButton::kEventLongPressed:
      pressSomfyButton("Prog");
      break;
  }
}

void printValues() {
    Serial.print("Temperature = ");
    Serial.print(bme.readTemperature());
    Serial.println(" *C");

    Serial.print("Pressure = ");

    Serial.print(bme.readPressure() / 100.0F);
    Serial.println(" hPa");

    Serial.print("Approx. Altitude = ");
    Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
    Serial.println(" m");

    Serial.print("Humidity = ");
    Serial.print(bme.readHumidity());
    Serial.println(" %");

    Serial.println();
}
