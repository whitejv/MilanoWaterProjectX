#include  <stdio.h>
#include <string.h>
#include <Wire.h>
#include <IPAddress.h>
#include <PubSubClient.h>
//#include <ArduinoJson.h>
#include <cJSON.h>
#include <water.h>

#if defined(ARDUINO_ESP8266_GENERIC) || defined(ARDUINO_ESP8266_WEMOS_D1MINI) || defined(ARDUINO_ESP8266_THING_DEV)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#endif

#define fwVersion 1113
/*
 * Revision Log
 *
 * 1111 - Original GenericSensor Code
 * 1112 - Fixed bug that resulted in bad flow meter pulse counts
 *        & changed Interrupt type to RISING
 * 1113 - Slowing Transmission to approx 1x sec instead of 2x sec
 *
 */

/*
GPIO00 - //Good - Config 3
GPIO01 - stops operation of board
GPIO02 - //Good also lights LED - Temp Sensor
GPIO03 - //Good - Config 2 (Also TX Pin If grounded board won’t load)
GPIO04 - //Good - Disc Input 1
GPIO05 - //Good - Disc Input 2
GPIO06 - doesn’t exist
GPIO12 - //Good - Config 1
GPIO13 - //Good - Flow Sensor
GPIO14 - //reserved SDA
GPIO15 - //reserved SCL
GPIO16 - //Good - Disc 3 (no pull-up)
*/

/* Declare all constants and global variables */

IPAddress prodMqttServerIP(192, 168, 1, 250);
IPAddress devMqttServerIP(192, 168, 1, 249);


int extendedSensor = 0;
int sensor = 0;
int InitiateReset = 0;
int ErrState = 0;
int ErrCount = 0;
const int ERRMAX = 10;
unsigned int masterCounter = 0;
const int discInput1 = DISCINPUT1;
const int discInput2 = DISCINPUT2;
int ioInput = 0;
long currentMillis = 0;
long previousMillis = 0;
long millisecond = 0;
int loopInterval = 1050; //changed from 500 to 1050 to slow loop
int flowInterval = 1000;
volatile byte pulseCount;
byte pulse1Sec = 0;
unsigned long timerOTA;
char messageName[50];
char messageNameJSON[50];
float temperatureF;

WiFiClient espFlowClient;

PubSubClient P_client(espFlowClient);
PubSubClient D_client(espFlowClient);
PubSubClient client;

const int oneWireBus = TEMPSENSOR;
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

/* Forward declaration of functions to solve circular dependencies */

void updateFlowData();
void updateTemperatureData();
void publishFlowData();
void publishJsonData();
void printFlowData();
void printClientState(int state);
void readAnalogInput();
void readDigitalInput();
void setupOTA();
void setupWiFi();
void connectToMQTTServer();
void checkConnectionAndLogState();

void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Booting");

  #if defined(ARDUINO_ESP8266_GENERIC) || defined(ARDUINO_ESP8266_WEMOS_D1MINI) || defined(ARDUINO_ESP8266_THING_DEV)
    pinMode(LED_BUILTIN, OUTPUT);
    const int configPin1 = CONFIGPIN1;
    const int configPin3 = CONFIGPIN2;
    const int configPin2 = 14 ; //CONFIGPIN3;
    pinMode(configPin1, INPUT_PULLUP);
    pinMode(configPin2, INPUT_PULLUP);
    pinMode(configPin3, INPUT_PULLUP);

    sensors.begin();// Start the DS18B20 sensor
    //pinMode(FLOWSENSOR, INPUT_PULLUP);
    //attachInterrupt(digitalPinToInterrupt(FLOWSENSOR), pulseCounter, FALLING);
    pinMode(FLOWSENSOR, INPUT);
    attachInterrupt(digitalPinToInterrupt(FLOWSENSOR), pulseCounter, RISING);
  #endif
    delay(3000); //give some time for things to get settled
    // Read the config pins and get configuation data
    //Serial.print(digitalRead(configPin1));
    //Serial.print(digitalRead(configPin2));
    sensor = digitalRead(configPin3) <<2 | digitalRead(configPin2)<<1 | digitalRead(configPin1) ;
    Serial.print(" #");
    Serial.print(sensor);

  /* 
  * Sensors 0-3 are standard sensors
  * Sensors 4-7 are extended sensors with additional data words
  */


    if (sensor >= 4){ 
      extendedSensor = 1;
      Serial.print(" Extended ") ;
    }
    Serial.print(" Sensor ID: ");
    Serial.println(flowSensorConfig[sensor].sensorName);

    
    strcpy(messageName, flowSensorConfig[sensor].messageid);
    strcpy(messageNameJSON, flowSensorConfig[sensor].jsonid);
  
    if ( extendedSensor == 1 ) {
       
    }                                      
    else {
      pinMode(discInput1, INPUT_PULLUP); //in normal mode the sensor board can support 2 GPIOs
      pinMode(discInput2, INPUT_PULLUP);
    }
    setupWiFi();
    setupOTA();
    connectToMQTTServer();

    if (client.setBufferSize(1024) == FALSE ) {
      Serial.println("Failed to allocate large MQTT send buffer - JSON messages may fail to send.");
    }

    genericSens_.generic.fw_version = fwVersion ;

}

void loop() {

  ArduinoOTA.handle();

  if (millis() - timerOTA > loopInterval) {

     ESP.wdtFeed();

     updateFlowData();
     updateTemperatureData();
     readAnalogInput() ;
     readDigitalInput() ;
     publishFlowData();
     publishJsonData();
     printFlowData();
     timerOTA = millis();
     
     client.loop();
     
     ++masterCounter;
     if (masterCounter > 28800) {  //Force a reboot every 8 hours
       while (1) {};
     }
     genericSens_.generic.cycle_count = masterCounter;
  }
  checkConnectionAndLogState();

}

/* Implementation of the functions */
void setupWiFi() {
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
  Serial.print("ESP Board MAC Address:  ");
  Serial.println(WiFi.macAddress());
}

void setupOTA(){
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
}

void connectToMQTTServer(){
  unsigned long connectAttemptStart = millis();
  bool connected = false;
  //Try connecting to the production MQTT server first

  P_client.setServer(prodMqttServerIP, PROD_MQTT_PORT);

  // Connect to the MQTT server
 
  while (!P_client.connected() && millis() - connectAttemptStart < 5000) { // Adjust the timeout as needed
    Serial.print("Connecting to Production MQTT Server: ...");
    connected = P_client.connect(flowSensorConfig[sensor].clientid);
    if (connected) {
      client = P_client; // Assign the connected production client to the global client object
      Serial.println("connected\n");
    } else {
      Serial.print("failed with client state: ");
      printClientState(P_client.state());
      Serial.println() ;
      delay(2000);
    }
  }

  // If connection to the production server failed, try connecting to the development server
  if (!connected) {
    //PubSubClient client(devMqttServerIP, DEV_MQTT_PORT, espWellClient);
    D_client.setServer(devMqttServerIP, DEV_MQTT_PORT);
    while (!D_client.connected()) {
      Serial.print("Connecting to Development MQTT Server...");
      connected = D_client.connect(flowSensorConfig[sensor].clientid);
      if (connected) {
        client = D_client; // Assign the connected development client to the global client object
        Serial.println("connected\n");
      } else {
        Serial.print("failed with client state: ");
        printClientState(D_client.state());
        Serial.println();
        delay(2000);
      }
    }
  }
}

void updateFlowData() {
  currentMillis = millis();
  if (((currentMillis - previousMillis) > flowInterval) && pulseCount > 0 ) {
    pulse1Sec = pulseCount;
    pulseCount = 0;
    millisecond = millis() - previousMillis ;
    genericSens_.generic.pulse_count = pulse1Sec ;
    genericSens_.generic.milliseconds = millisecond ;
    genericSens_.generic.new_data_flag = 1;
    previousMillis = millis();
  } else {
    genericSens_.generic.new_data_flag = 0 ;
  }
}

void updateTemperatureData() {
  sensors.requestTemperatures(); 
  temperatureF = sensors.getTempFByIndex(0);
  genericSens_.generic.temp = (int)temperatureF;
  memcpy(&genericSens_.generic.temp_w1,  &temperatureF, sizeof(temperatureF));
}

void readAnalogInput() {
  const int analogInPin = A0; 
  genericSens_.generic.adc_sensor = analogRead(analogInPin);
  //Serial.println(flow_data_payload[3]);
}


void readDigitalInput() {

  // Read the config pins and get configuation data
  //Serial.print(digitalRead(discInput1));
  //Serial.print(digitalRead(discInput2));
  ioInput = digitalRead(discInput2)<<1 | digitalRead(discInput1) ;
  genericSens_.generic.gpio_sensor = ioInput ;
  //Serial.print("IO Input: ");
  //Serial.println(ioInput);
}

void publishFlowData() {
  client.publish(flowSensorConfig[sensor].messageid, (byte *)genericSens_.data_payload, flowSensorConfig[sensor].messagelen*4);
}

void publishJsonData() {

    int i;
    
    // Create a new cJSON object
    cJSON *jsonDoc = cJSON_CreateObject();

    //Serial.printf("message length   %d", flowSensorConfig[sensor].messagelen);
    for (i = 0; i < flowSensorConfig[sensor].messagelen; i++) {
        // Add data to the cJSON object
        cJSON_AddNumberToObject(jsonDoc, genericsens_ClientData_var_name[i], genericSens_.data_payload[i]);
    }

    // Serialize the cJSON object to a string
    char *jsonBuffer = cJSON_Print(jsonDoc);
    if (jsonBuffer != NULL) {
        size_t n = strlen(jsonBuffer);
        //Serial.printf(flowSensorConfig[sensor].jsonid);
        //Serial.printf("   %d", n);
        //Serial.printf("\n");
        //Serial.printf(jsonBuffer);
        //Serial.printf("\n");
        
        // Publish the JSON data
        if (client.publish(flowSensorConfig[sensor].jsonid, jsonBuffer, n) == FALSE) {
          Serial.printf("JSON Message Failed to Publish");
          Serial.printf("\n");
        }
        // Free the serialized data buffer
        free(jsonBuffer);
    }

    // Delete the cJSON object
    cJSON_Delete(jsonDoc);
}
void printFlowData() {
  Serial.printf(messageName);
  //Serial.printf("message length   %d",flowSensorConfig[sensor].messagelen); 
  for (int i = 0; i<=flowSensorConfig[sensor].messagelen-1; ++i) {
    Serial.printf(" %x", genericSens_.data_payload[i]);
  }
  Serial.printf("\n");
}
void printClientState(int state) {
  switch (state) {
    case -4:
      Serial.println("MQTT_CONNECTION_TIMEOUT");
      break;
    case -3:
      Serial.println("MQTT_CONNECTION_LOST");
      break;
    case -2:
      Serial.println("MQTT_CONNECT_FAILED");
      break;
    case -1:
      Serial.println("MQTT_DISCONNECTED");
      break;
    case  0:
      Serial.println("MQTT_CONNECTED");
      break;
    case  1:
      Serial.println("MQTT_CONNECT_BAD_PROTOCOL");
      break;
    case  2:
      Serial.println("MQTT_CONNECT_BAD_CLIENT_ID");
      break;
    case  3:
      Serial.println("MQTT_CONNECT_UNAVAILABLE");
      break;
    case  4:
      Serial.println("MQTT_CONNECT_BAD_CREDENTIALS");
      break;
    case  5:
      Serial.println("MQTT_CONNECT_UNAUTHORIZED");
      break;
  }
}
void checkConnectionAndLogState(){
    if (client.connected() == FALSE) {
    ErrState = client.state() ;
    ++ErrCount;
    Serial.printf(messageName);
    Serial.print("-- Disconnected from MQTT:");
    Serial.print("Error Count:  ");
    Serial.print(ErrCount);
    Serial.print("Error Code:  ");
    Serial.println(ErrState);
  }

  if ( ErrCount > ERRMAX ) {
    //Initiate Reset
    Serial.println("Initiate board reset!!") ;
    while(1);
  }
}