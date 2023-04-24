#include <stdio.h>
#include <string.h>
#include <Wire.h>
#include <IPAddress.h>
#include <PubSubClient.h>
#include <water.h>

#if defined(ARDUINO_ESP8266_GENERIC) || defined(ARDUINO_ESP8266_WEMOS_D1MINI)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#elif defined(ARDUINO_FEATHER_ESP32)
#include <WiFi.h>
#include <esp_task_wdt.h>
#elif defined(ARDUINO_RASPBERRY_PI_PICO_W)
#include <WiFi.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/binary_info.h"
#endif

#define WDT_TIMEOUT 10  //10 seconds Watch Dog Timer (WDT)

const char ssid[] = "ATT9LCV8fL_2.4";
const char password[] = "6jhz7ai7pqy5";

IPAddress MQTT_BrokerIP(192, 168, 1, 249);
const char *mqttServer = "raspberrypi.local";
const int mqttPort = 1883;

WiFiClient espTankClient;
PubSubClient client(MQTT_BrokerIP, mqttPort, espTankClient);

int WDT_Interval = 0;
unsigned int masterCounter = 0;

#define FLOWSENSOR 13
#define float1 4
#define float2 5
#define float3 12
#define float4 14

unsigned long timerOTA ;

long currentMillis = 0;
long previousMillis = 0;
long millisecond = 0;
int interval = 2000;
volatile byte pulseCount;
byte pulse1Sec = 0;

void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

const int oneWireBus = 2;
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

void setup() {
  Serial.begin(115200);

  Serial.println("Booting");
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  Serial.println("");
  Serial.print("WiFi connected -- ");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  /*
   * Adding OTA Support
   */
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  
  client.setServer(MQTT_BrokerIP, mqttPort);

  while (!client.connected()) {
    Serial.printf("Connecting to MQTT.....");
    if (client.connect(TANK_CLIENTID)) {
      Serial.printf("connected\n");
    } else {
      Serial.printf("failed with ");
      Serial.printf("client state %d\n", client.state());
      delay(2000);
    }
  }

  //client.subscribe("ESP Control");

#if defined(ARDUINO_FEATHER_ESP32)
  Serial.printf("Configuring WDT...");
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);
  Serial.printf("Complete\n");
  Wire.begin();
#elif defined(ARDUINO_ESP8266_GENERIC) || defined(ARDUINO_ESP8266_WEMOS_D1MINI)
  //Wire.begin(12, 14); //only if you are using I2C
  pinMode(FLOWSENSOR, INPUT_PULLUP);
  pinMode( float1, INPUT_PULLUP);
  pinMode( float2, INPUT_PULLUP);
  pinMode( float3, INPUT_PULLUP);
  pinMode( float4, INPUT_PULLUP);
  sensors.begin();// Start the DS18B20 sensor
  attachInterrupt(digitalPinToInterrupt(FLOWSENSOR), pulseCounter, FALLING);
#elif defined(ARDUINO_RASPBERRY_PI_PICO_W)
  i2c_init(i2c_default, 100 * 1000);
  gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
  Wire.begin();
#endif

  pulseCount = 0;
  previousMillis = 0;

  timerOTA = millis();
}
void loop() {
  ArduinoOTA.handle() ;
  if (millis() - timerOTA > 500) {
     updateWatchdog();
     updateMasterCounter();
     updateFlowData();
     updateTemperatureData();
     readAnalogInput();
     readDigitalInput();
     processMqttClient();
     publishFlowData();
     printFlowData();
     //delay(500);
     timerOTA = millis() ;
  }
}

void updateWatchdog() {
  if (WDT_Interval++ > WDT_TIMEOUT) { WDT_Interval = 0; }
  #if defined (ARDUINO_FEATHER_ESP32)
  esp_task_wdt_reset();
  #endif
}

void updateMasterCounter() {
  ++masterCounter;
  if (masterCounter > 28800) {  //Force a reboot every 8 hours
    while (1) {};
  }
  tank_data_payload[12] = masterCounter;
}

void updateFlowData() {
  currentMillis = millis();
  if (((currentMillis - previousMillis) > interval) && pulseCount > 0 ) {
    pulse1Sec = pulseCount;
    millisecond = millis() - previousMillis ;
    tank_data_payload[0] = pulse1Sec ;
    tank_data_payload[1] = millisecond ;
    tank_data_payload[2] = 1;
    previousMillis = millis();
  } else {
    tank_data_payload[2] = 0 ;
  }
  pulseCount = 0;
}
 
void updateTemperatureData() {
  sensors.requestTemperatures(); 
  float temperatureF = sensors.getTempFByIndex(0);
  tank_data_payload[17] = *((int *)&temperatureF);
}

void readAnalogInput() {
  tank_data_payload[3] = analogRead(A0);
}

void readDigitalInput() {
  int float100 = 0;
  int float75 = 0;
  int float50 = 0;
  int float25 = 0 ;
  
  float100 = !digitalRead(float1) ;
  float75  = !digitalRead(float3) ;
  float50  = !digitalRead(float2) ;
  float25  = !digitalRead(float4) ;
  tank_data_payload[4] = float100;
  tank_data_payload[5] = float75;
  tank_data_payload[6] = float50;
  tank_data_payload[7] = float25;
}

void processMqttClient() {
  client.loop();
}

void publishFlowData() {
  client.publish(TANK_CLIENT, (byte *)tank_data_payload, TANK_LEN*4);
}

void printFlowData() {
  Serial.printf("Tank (Well#3) Data: ");
  for (int i = 0; i <= 16; ++i) {
    Serial.printf("%x ", tank_data_payload[i]);
  }
  Serial.printf("%f ", *((float *)&tank_data_payload[17]));
  for (int i = 19; i <= 20; ++i) {
    Serial.printf("%x ", tank_data_payload[i]);
  }
  Serial.printf("\n");
}