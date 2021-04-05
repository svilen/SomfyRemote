# SomfyRemote

Somfy Remote is a simple emulate a Somfy RTS remote.

It will auto close your Somfy motorized blinds every day at given hour or if room temperature is above 32C (this can be adjusted in the sketch).

The device will have 1 button "PROG" so you can pair it to your Somfy blinds and use Wemos D1 mini ESP8266 module.

### Hardware Requirements

- Wemos D1 mini (ESP8266 module)
- RTC Clock module (I2C type)
- 433Mhz transmitter with changed crystal for 433.29Mhz (Somfy frequency)
- BME280 Sensor (I2C type)
- 1 LED + 100R resistor
- Tactile button + 10k resistor

###  Wiring description

| Wemos PIN | Arduino GPIO | Description |
| ------ | ------ | ------ |
| D5 | GPIO14 | Blink LED |
| D6 | GPIO12 | PROG Button |
| D7 | GPIO13 | 433Mhz TX Module |
| D1 | GPIO5 | I2C SCL |
| D2 | GPIO4 | I2C SDA |

### Installation

Libraries can be installed using the Library-Manager. Open the Library-Manager in Arduino IDE via Tools->Manage Libraries...
Search for:

- "Somfy_Remote_Lib" and install the Somfy Remote Lib library
- "RTClib"
- "Adafruit BME280"
- "NTPClient Generic"
- "AceButton"
- "arduino timer"