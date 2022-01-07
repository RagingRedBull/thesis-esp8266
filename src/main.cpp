#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <DHTesp.h>
#include <EEPROM.h>
#include <secrets.h>

//CONSTANTS
const int ANALOG_IN = A0;
const int PIN_DHT_11 = D1;
const int PIN_DHT_22 = D2;
const int TOTAL_SENSORS = 8;
const String ENDPOINT_URL = ENDPOINT_IP;
const char *AP_SSID = STASSID;
const char *AP_PW = STAPSK;
//GLOBAL
bool availableSensors[8];
ESP8266WebServer webServer(80);
WiFiClient client;
HTTPClient http;

void initializeSensorsOnStartup()
{
  // for (int i = 0; i < TOTAL_SENSORS; i++)
  // {
  //   availableSensors[i] = false;
  // }
  availableSensors[0] = false;
  availableSensors[1] = false;
  availableSensors[2] = false;
  availableSensors[3] = false;
  availableSensors[4] = false;
  availableSensors[5] = false;
  availableSensors[6] = false;
}

/*
  DESERIALIZE ON JSON DATA FOR SETUP
*/

bool updateSensorList(String payload)
{
  StaticJsonDocument<512> doc;
  JsonArray jsonSensorIdList;
  DeserializationError jsonError;
  jsonError = deserializeJson(doc, payload);
  if (jsonError)
  {
    Serial.println("INVALID SERVER RESPONSE");
    Serial.println(jsonError.f_str());
    return false;
  }
  for (JsonObject sensorList_item : doc["sensorSet"].as<JsonArray>())
  {
    int sensorList_item_id = sensorList_item["sensorId"];
    bool sensorList_item_toEnable = sensorList_item["toEnable"];
    availableSensors[sensorList_item_id - 1] = sensorList_item_toEnable;
  }
  return true;
}

void registerUnitInfoToServer()
{
  String jsonPayload;
  StaticJsonDocument<96> doc;
  doc["macAddress"] = WiFi.macAddress();
  doc["ipV4"] = WiFi.localIP();
  serializeJson(doc, jsonPayload);
  http.begin(client, ENDPOINT_URL + "/detector/new");
  Serial.println(ENDPOINT_URL + "/detector/new");
  int httpCode = http.POST(jsonPayload);
  if (httpCode == HTTP_CODE_CREATED)
  {
    Serial.println(http.getString());
  }
  http.end();
}
bool getUnitInfoViaServer()
{
  bool isSuccessful = false;
  Serial.println();
  http.begin(client, ENDPOINT_URL + "/detector/" + WiFi.macAddress()); //HTTP
  Serial.println(ENDPOINT_URL + "/detector/" + WiFi.macAddress());
  int httpCode = http.GET();

  Serial.printf("[HTTP] GET... code: %d\n", httpCode);
  // file found at server
  Serial.println("Attempting to get Detector Unit Info of " + WiFi.macAddress());
  if (httpCode == HTTP_CODE_OK)
  {
    const String &payload = http.getString();
    Serial.println("received payload:\n<<");
    Serial.println(payload);
    Serial.println(">>");
    isSuccessful = true;
    updateSensorList(payload);
  }
  http.end();
  return isSuccessful;
}
void initializeWifi()
{
  WiFi.begin(AP_SSID, AP_PW);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  if (!getUnitInfoViaServer())
  {
    Serial.println("Registering Unit");
    registerUnitInfoToServer();
  }
}

void handleInvalidUrl()
{
  webServer.send(404, "text/plain", "Unidentified endpoint");
}

/*
  WEB SERVER FUNCTIONS
*/

/*
  UPDATES SENSOR LIST
*/

void handleSensorUpdateRequest()
{

  if (webServer.hasArg("plain") == false)
  { //Check if body received

    webServer.send(200, "text/plain", "Body not received");
    return;
  }

  if (updateSensorList(webServer.arg("plain")))
  {
    webServer.send(200, "text/plain", "UPDATED");
  }
  else
  {
    webServer.send(500, "text/plain", "FAILED");
  }
}

void initializeWebServer()
{
  webServer.begin();
  webServer.on("/update", HTTPMethod::HTTP_PUT, handleSensorUpdateRequest);
  webServer.onNotFound(handleInvalidUrl);
}
void setup()
{
  Serial.begin(9600);
  pinMode(A0, INPUT);
  initializeSensorsOnStartup();
  initializeWifi();
  initializeWebServer();
  delay(20000); // allow the MQ-6 to warm up
}

/*
  
*/
String generateDetectorLogsPayload()
{
  DynamicJsonDocument jsonDoc(2048);
  JsonArray sensorLogs;
  String jsonPayload;
  jsonDoc["macAddress"] = WiFi.macAddress();
  sensorLogs = jsonDoc.createNestedArray("sensorLogSet");
  for (int i = 0; i < TOTAL_SENSORS; i++)
  {
    if (availableSensors[i] == true)
    {
      JsonObject sensorData = sensorLogs.createNestedObject();
      if (i < 2)
      {
        DHTesp dht;
        if (i == 0)
        {
          dht.setup(PIN_DHT_11, DHTesp::DHT11);
        }
        else
        {
          dht.setup(PIN_DHT_22, DHTesp::DHT22);
        }
        delay(dht.getMinimumSamplingPeriod());
        sensorData["type"] = "DHT";
        if (i == 0)
        {
          sensorData["name"] = "DHT-11";
        }
        else
        {
          sensorData["name"] = "DHT-22";
        }
        sensorData["temperature"] = dht.getTemperature();
        sensorData["humidity"] = dht.getHumidity();
      }
      else if (i <= 5 && i >= 2)
      {
        String mqName;
        sensorData["type"] = "MQ";
        if (i == 2)
        {
          mqName = "MQ-2";
        }
        else if (i == 3)
        {
          mqName = "MQ-5";
        }
        else if (i == 4)
        {
          mqName = "MQ-7";
        }
        else
        {
          mqName = "MQ-135";
        }
        sensorData["name"] = mqName;
        sensorData["mqValue"] = analogRead(A0);
      }
      else if (i == 6)
      {
      }
    }
  }
  serializeJsonPretty(jsonDoc, jsonPayload);
  return jsonPayload;
}

void sendDataToServer()
{
  String jsonPayload = generateDetectorLogsPayload();
  Serial.print("[HTTP] begin...\n");
  Serial.println("Contacting:" + ENDPOINT_URL + "/log/upload");
  // configure traged server and url
  http.begin(client, ENDPOINT_URL + "/log/upload"); //HTTP
  http.addHeader("Content-Type", "application/json");

  Serial.print("[HTTP] POST...\n");
  // start connection and send HTTP header and body
  int httpCode = http.POST(jsonPayload);

  // HTTP header has been send and Server response header has been handled
  Serial.printf("[HTTP] POST... code: %d\n", httpCode);

  // file found at server
  if (httpCode == HTTP_CODE_OK)
  {
    const String &payload = http.getString();
    Serial.println(jsonPayload);
    Serial.println("received payload:\n<<");
    Serial.println(payload);
    Serial.println(">>");
  }
  http.end();
}

void handleWebServer()
{
}
bool hasActiveSensor()
{
  bool hasActiveSensor = false;
  for (int i = 0; i < TOTAL_SENSORS; i++)
  {
    if (availableSensors[i] == true)
    {
      hasActiveSensor = true;
    }
  }
  return hasActiveSensor;
}
void loop()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    if (hasActiveSensor())
    {
      sendDataToServer();
    }
    webServer.handleClient();
  }
  delay(3000);
}