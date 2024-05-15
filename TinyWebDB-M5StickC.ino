// Sample Arduino Json Web Client
// Downloads and parse http://jsonplaceholder.typicode.com/users/1
//
// Copyright Benoit Blanchon 2014-2017
// MIT License
//
// Arduino JSON library
// https://bblanchon.github.io/ArduinoJson/
// If you like this project, please add a star!

#include <ArduinoJson.h>
#include <Arduino.h>
#include <time.h>            
#define JST     3600*9

#include <M5StickC.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>

#define WIFI_SSID "uislab003" // ①
#define WIFI_PASSWORD "nihao12345"

WiFiMulti WiFiMulti;
int count = 1;  // ③

#define USE_SERIAL Serial

WiFiClient client;

const char* resource = "http://sensor.db.uc4.net/"; // http resource
const unsigned long BAUD_RATE = 9600;      // serial connection speed
const unsigned long HTTP_TIMEOUT = 10000;  // max respone time from server
const size_t MAX_CONTENT_SIZE = 512;       // max size of the HTTP response

HTTPClient http;

float accX = 0.0F;
float accY = 0.0F;
float accZ = 0.0F;

void setup() {

  USE_SERIAL.begin(115200);

  M5.begin(); 
  M5.IMU.Init();
  M5.Lcd.setRotation(3);
  M5.Lcd.setCursor(0, 0, 2);
  WiFiMulti.addAP(WIFI_SSID,WIFI_PASSWORD);
  M5.Lcd.print("Connecting");
  while(WiFiMulti.run() != WL_CONNECTED) {
    M5.Lcd.print(".");
    delay(1000);
  }
  M5.Lcd.println("");
  M5.Lcd.println("Connected to");
  M5.Lcd.println(WiFi.localIP());

  USE_SERIAL.print("Connected to ");
  USE_SERIAL.println(WiFi.localIP());

  delay(500);

  // configTime( JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp"); // esp8266/arduino ver 2.6.3まで有効
  configTzTime("JST-9", "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");   // 2.7.0以降, esp32コンパチ
  while (1500000000 > time(nullptr)) {
    delay(10);   // waiting time settle
  };
  
}


void loop() {
  M5.update();  // ⑤
  if (M5.BtnA.wasPressed() ) {  // ⑥
    M5.Lcd.println("Pushed");
    sensor_TinyWebDB();
    count ++;  // ⑧
  }
  
  delay(100);
}

void sensor_TinyWebDB() {    
    int httpCode;
    char  tag[32];
    char  value[256];

    // read values from the sensor
    M5.IMU.getAccelData(&accX,&accY,&accZ);

    const size_t bufferSize = JSON_ARRAY_SIZE(7) + JSON_OBJECT_SIZE(7);
    DynamicJsonBuffer jsonBuffer(bufferSize);
    
    JsonObject& root = jsonBuffer.createObject();
    root["Ver"] = "1.0.0";
    root["sensor"] = "IMU";
    root["localIP"] = WiFi.localIP().toString();
    root["temperature"] = String(accX);
    root["pressure_hpa"] = String("On");
    root["battery_Vcc"] = String(M5.Axp.GetBatVoltage());

    time_t now = time(nullptr);
    root["localTime"] = String(now + JST);  // configTzTime not work

    root.printTo(value);

    uint64_t chipid=ESP.getEfuseMac();   //The chip ID is essentially its MAC address(length: 6 bytes).
    USE_SERIAL.printf("[TinyWebDB] %sn", value);
    USE_SERIAL.printf("ESP32 Chip id = %06X\n", (uint16_t)(chipid >> 32));
    sprintf(tag, "Switch-%06x", (uint16_t)(chipid >> 32));
    httpCode = TinyWebDBStoreValue(tag, value);
    // httpCode will be negative on error
    if(httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        USE_SERIAL.printf("[HTTP] POST... code: %d\n", httpCode);

        if(httpCode == HTTP_CODE_OK) {
            TinyWebDBValueStored();
        }
    } else {
        USE_SERIAL.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        TinyWebDBWebServiceError(http.errorToString(httpCode).c_str());
    }

    http.end();

    delay(10000);
}

void get_TinyWebDB(const char* tag0) {    
    int httpCode;
    char  tag[32];
    char  value[128];

    httpCode = TinyWebDBGetValue(tag0);

    // httpCode will be negative on error
    if(httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        USE_SERIAL.printf("[HTTP] GET... code: %d\n", httpCode);

        if(httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            const char * msg = payload.c_str();
            USE_SERIAL.println(payload);
            if (TinyWebDBreadReponseContent(tag, value, msg)){
                TinyWebDBGotValue(tag, value);
            }
        }
    } else {
        USE_SERIAL.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
        TinyWebDBWebServiceError(http.errorToString(httpCode).c_str());
    }

    http.end();

    delay(10000);
}

int TinyWebDBWebServiceError(const char* message)
{
}

// ----------------------------------------------------------------------------------------
// Wp TinyWebDB API
// Action        URL                      Post Parameters  Response
// Get Value     {ServiceURL}/getvalue    tag              JSON: ["VALUE","{tag}", {value}]
// ----------------------------------------------------------------------------------------
int TinyWebDBGetValue(const char* tag)
{
    char url[64];

    sprintf(url, "%s%s?tag=%s", resource, "getvalue/", tag);

    USE_SERIAL.printf("[HTTP] %s\n", url);
    // configure targed server and url
    http.begin(url);
    
    USE_SERIAL.print("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode = http.GET();

    return httpCode;
}

int TinyWebDBGotValue(const char* tag, const char* value)
{
    USE_SERIAL.printf("[TinyWebDB] %s\n", tag);
    USE_SERIAL.printf("[TinyWebDB] %s\n", value);

    return 0;   
}

// ----------------------------------------------------------------------------------------
// Wp TinyWebDB API
// Action        URL                      Post Parameters  Response
// Store A Value {ServiceURL}/storeavalue tag,value        JSON: ["STORED", "{tag}", {value}]
// ----------------------------------------------------------------------------------------
int TinyWebDBStoreValue(const char* tag, const char* value)
{
    char url[64];
  
    sprintf(url, "%s%s", resource, "storeavalue");
    USE_SERIAL.printf("[HTTP] %s\n", url);

    // POST パラメータ作る
    char params[256];
    sprintf(params, "tag=%s&value=%s", tag, value);
    USE_SERIAL.printf("[HTTP] POST %s\n", params);

    // configure targed server and url
    http.begin(url);

    // start connection and send HTTP header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.POST(params);
    String payload = http.getString();                  //Get the response payload
    Serial.println(payload);    //Print request response payload

    http.end();
    return httpCode;
}

int TinyWebDBValueStored()
{
  
    return 0;   
}


// Parse the JSON from the input string and extract the interesting values
// Here is the JSON we need to parse
// [
//   "VALUE",
//   "LED1",
//   "on",
// ]
bool TinyWebDBreadReponseContent(char* tag, char* value, const char* payload) {
  // Compute optimal size of the JSON buffer according to what we need to parse.
  // See https://bblanchon.github.io/ArduinoJson/assistant/
  const size_t BUFFER_SIZE =
      JSON_OBJECT_SIZE(3)    // the root object has 3 elements
      + MAX_CONTENT_SIZE;    // additional space for strings

  // Allocate a temporary memory pool
  DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);

  // JsonObject& root = jsonBuffer.parseObject(payload);
  JsonArray& root = jsonBuffer.parseArray(payload);
  JsonArray& root_ = root;

  if (!root.success()) {
    Serial.println("JSON parsing failed!");
    return false;
  }

  // Here were copy the strings we're interested in
  strcpy(tag, root_[1]);   // "led1"
  strcpy(value, root_[2]); // "on"

  return true;
}


// Pause for a 1 minute
void wait() {
  Serial.println("Wait 60 seconds");
  delay(60000);
}
