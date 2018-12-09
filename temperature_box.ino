#include <DNSServer.h>
#include <IotWebConf.h>

#include <ESP8266HTTPClient.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Wire.h>

/**
 * IoT Web Config settings.
 */
#define CONFIG_PIN 4
#define STATUS_PIN LED_BUILTIN

const char thingName[] = "The IoT Guru box";
const char wifiInitialApPassword[] = "theiotgurubox";

DNSServer dnsServer;
WebServer server(80);

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword);

/**
 * Parameters settings.
 */
#define CONFIG_VERSION "temp-box-01"

char nodeKeyParamValue[64];
char fieldNameParamValue[64];
char durationParamValue[16];

void configSaved();
boolean formValidator();

IotWebConfParameter nodeKeyParam = IotWebConfParameter("Node key", "nodeKeyParam", nodeKeyParamValue, 64);
IotWebConfParameter fieldNameParam = IotWebConfParameter("Field name", "fieldNameParam", fieldNameParamValue, 16);
IotWebConfParameter durationParam = IotWebConfParameter("Interval (s)", "durationParam", durationParamValue, 16, "number", "15..300", NULL, "min='15' max='300' step='1'");

/**
 * DS18B20 configuration.
 */
#define DS18B20_PIN 0
#define MAX_OW_DEVICES 1

DeviceAddress deviceAddresses[MAX_OW_DEVICES];
OneWire oneWireBus(DS18B20_PIN);
DallasTemperature sensors;
volatile unsigned int devices;

volatile unsigned long sensorLastSent = 0;
volatile unsigned long sensorSendDuration = 60000;

/**
 * The IoT Guru Base URL.
 */
String IOT_BASE_URL = "http://api.iotguru.live";

/**
 * Setup.
 */
void setup() 
{
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting up...");

  iotWebConf.setStatusPin(STATUS_PIN);
  iotWebConf.setConfigPin(CONFIG_PIN);
  iotWebConf.addParameter(&nodeKeyParam);
  iotWebConf.addParameter(&fieldNameParam);
  iotWebConf.addParameter(&durationParam);
  iotWebConf.getApTimeoutParameter()->visible = true;
  iotWebConf.init();

  server.on("/", handleRoot);
  server.on("/config", []{ iotWebConf.handleConfig(); });
  server.onNotFound([](){ iotWebConf.handleNotFound(); });

  oneWireBus = OneWire(DS18B20_PIN);
  sensors = DallasTemperature(&oneWireBus);
  sensors.begin();

  devices = discoverOneWireDevices(deviceAddresses, MAX_OW_DEVICES);

  Serial.println("Ready.");
}


void loop() 
{
  iotWebConf.doLoop();

  if (iotWebConf.getState() != IOTWEBCONF_STATE_ONLINE) return;
  
  if (sensorLastSent == 0 || sensorLastSent + sensorSendDuration < millis()) {
      sensorLastSent = millis();
  } else {
      return;
  }

  sensors.requestTemperatures();
  for (int i = 0; i < devices; i++) {
      sendTemperature(deviceAddresses[i]);
  }
}

void handleRoot()
{
  if (iotWebConf.handleCaptivePortal()) return;

  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>The IoT Guru Box</title></head>";
  s += "<body><ul>";
  s += "<li>Node key: ";
  s += nodeKeyParamValue;
  s += "<li>Field name: ";
  s += fieldNameParamValue;
  s += "<li>Interval: ";
  s += atoi(durationParamValue);
  s += "</ul>";
  s += "To configure this box, go to <a href='config'>configure page</a> to change values.";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}

/**
 * Send temperature of the specified DS18B20 device.
 */
void sendTemperature(DeviceAddress address) {
    float temp = getTemperature(address);

    HTTPClient http;
    http.useHTTP10(true);
    http.setTimeout(8000);

    String NODE_KEY = String(nodeKeyParamValue);
    String FIELD_NAME = String(fieldNameParamValue);
    String url = String(IOT_BASE_URL) + "/measurement/create/" + NODE_KEY + "/" + FIELD_NAME + "/" + String(temp);
    Serial.println("URL: " + url);
    http.begin(url);
    Serial.println("Sent 'temperature': " + String(http.GET()));
    http.end();
}

/**
 * Returns with the temperature of the Dallas unit.
 *
 * @param address the address
 * @return the temperature
 */
float getTemperature(DeviceAddress address) {
    int counter = 0;
    float temp = 0;
    do {
        temp = sensors.getTempC(address);
        delay((counter++) * 10);
    } while ((temp == 85.0 || temp == (-127.0)) && counter < 10);
    Serial.println("Temperature of '" + addressToString(address) + "': " + String(temp));

    return temp;
}

/**
 * Discover DS18B20 devices on the one wire bus.
 * 
 * @param deviceAddresses the array of device adresses
 * @param maDevices the number of devices to search
 * @return the number of devices found
 */
int discoverOneWireDevices(DeviceAddress deviceAddresses[], int maxDevices) {
    Serial.println("Looking for 1-Wire devices...");

    byte addr[8];
    int count = 0;
    while (count < maxDevices && oneWireBus.search(addr)) {
        for(int i = 0; i < 8; i++) {
            deviceAddresses[count][i] = addr[i];
        }
        if (OneWire::crc8(addr, 7) != addr[7]) {
            Serial.println("CRC is not valid, aborting...");
            return 0;
        }

        Serial.println("Found 1-Wire device with address: " + addressToString(addr));
        count++;
    }

    Serial.println("Found " + String(count) + " devices.");
    Serial.println("");

    oneWireBus.reset_search();

    return count;
}

/**
 * Convert DS1820's address to String.
 */
String addressToString(DeviceAddress address) {
    String result = "";
    for (int i = 0; i < 8; i++) {
        if (address[i] < 16) {
            result += "0";
        }

        result += String(address[i], HEX);
    }

    return result;
}

