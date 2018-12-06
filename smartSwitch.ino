#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include "hw_timer.h"
#include "html_pages.h"

// WIFI SETTINGS
const char* ssid = "TP-Link_89B4";
const char* password = "saxena@54321";

// MQTT Settings
const char* mqtt_server = "192.168.0.111";
const int mqtt_port = 1883;
const char* mqtt_user = "openhabian";
const char* mqtt_password = "openhabian";
const char* mqtt_state_topic = "wemos/dimmer/state";
const char* mqtt_brightness_topic = "wemos/dimmer/brightness";
const char* mqtt_fade_topic = "wemos/dimmer/fade";
const char* pir_state_topic = "wemos/pir";

// PIN SETTINGS
const byte switchPin = 5;
const byte zcPin = 12;
const byte outPin = 13;
const byte pirPin = 0;

// OTHER SETTINGS
const byte mqttDebug = 1;

byte fade = 0;
byte state = 1;
byte tarBrightness = 255;
byte curBrightness = 0;
byte zcState = 0; // 0 = ready; 1 = processing;

ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

void setup(void) {
  pinMode(zcPin, INPUT_PULLUP);
  pinMode(switchPin, INPUT_PULLUP);
  pinMode(outPin, OUTPUT);
  //if (pirPin) 
  //pinMode(pirPin, INPUT_PULLUP); //From original code. Didn't work for my variant of code.
  pinMode(pirPin, INPUT);
  digitalWrite(pirPin, LOW);

  digitalWrite(outPin, 0);
  
  Serial.begin(115200);
  Serial.println("");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.print("\nConnected: ");
  Serial.println(WiFi.localIP());

  server.on("/", []() {
    // server args for state
    if (server.arg("s") != "") {
      if (server.arg("s") == "1" || server.arg("s") == "on" || server.arg("s") == "true") {
        updateState(1);
      }
      else if (server.arg("s") == "t") {
        updateState(!state);
      }
      else {
        updateState(0);
      }
    }
    
    // server args for brightness
    if (server.arg("b") != "") {
      updateBrightness((byte) server.arg("b").toInt());
    }
    
    // server args for fade
    if (server.arg("f") != "") {
      if (server.arg("f") == "1" || server.arg("f") == "on" || server.arg("f") == "true") {
        updateFade(1);
      }
      else if (server.arg("f") == "t") {
        updateFade(!fade);
      }
      else {
        updateFade(0);
      }
    }
    
    // json output
    String s = "{\n   \"s\":";
    s += state;
    s += ",\n   \"b\":";
    s += tarBrightness;
    s += ",\n   \"f\":";
    s += fade;
    s += "\n}";
    
    server.send(200, "text/plain", s);
  });

  server.on("/webupdate", []() {
    server.send(200, "text/html", updateHTTP);
  });
  
  server.onFileUpload([]() {
    if(server.uri() != "/update") return;
    detachInterrupt(zcPin);
    detachInterrupt(switchPin);
    hw_timer_set_func(0);
    digitalWrite(outPin, 0);
    HTTPUpload& upload = server.upload();
    if(upload.status == UPLOAD_FILE_START){
      state = 0;
      //Serial.setDebugOutput(true);
      Serial.printf("Update: %s\n", upload.filename.c_str());
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if(!Update.begin(maxSketchSpace)){//start with max available size
        Update.printError(Serial);
      }
    } else if(upload.status == UPLOAD_FILE_WRITE){
      if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
        Update.printError(Serial);
      }
    } else if(upload.status == UPLOAD_FILE_END){
      if(Update.end(true)){ //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      //Serial.setDebugOutput(false);
    }
    yield();
  });
  
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError())?"UPDATE FAIL":"UPDATE SUCCESS");
    ESP.restart();
  });
  
  server.begin();
  Serial.println("HTTP server: Started");
  
  setupMQTTClient();

  hw_timer_init(NMI_SOURCE, 0);
  hw_timer_set_func(dimTimerISR);
  
  //if (pirPin) 
  //attachInterrupt(pirPin, pirDetect, CHANGE);  //implemented PIR in loop instead of interrupt
  attachInterrupt(zcPin, zcDetectISR, RISING);
}
  
void setupMQTTClient() {
  int connectResult;
  
  if (mqtt_server != "") {
    Serial.print("MQTT client: ");
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(mqttCallback);
    
    if (mqtt_user == "") {
      connectResult = mqttClient.connect("ESP" + ESP.getChipId());
    }
    else {
      connectResult = mqttClient.connect("ESP" + ESP.getChipId(), mqtt_user, mqtt_password);
    }
    
    if (connectResult) {
      Serial.println("Connected");
    }
    else {
      Serial.print("Failed (");
      Serial.print(mqttClient.state());
      Serial.println(")");
    }
    
    if (mqttClient.connected()) {
      Serial.print("MQTT topic '");
      Serial.print(mqtt_state_topic);
      if (mqttClient.subscribe(mqtt_state_topic)) {
        Serial.println("': Subscribed");
      }
      else {
        Serial.print("': Failed");
      }
      
      Serial.print("MQTT topic '");
      Serial.print(mqtt_brightness_topic);
      if (mqttClient.subscribe(mqtt_brightness_topic)) {
        Serial.println("': Subscribed");
      }
      else {
        Serial.print("': Failed");
      }
      
      Serial.print("MQTT topic '"); 
      Serial.print(mqtt_fade_topic);
      if (mqttClient.subscribe(mqtt_fade_topic)) {
        Serial.println("': Subscribed");
      }
      else {
        Serial.print("': Failed");
      }
    }
  }
}

//MQTT Reconnection code
long lastReconnectAttempt = 0;

boolean reconnect() {
  Serial.print("MQTT connection lost. Reconnecting...");
  setupMQTTClient();
  return mqttClient.connected();
} 

void loop(void)
{ 
  // handle http:
  server.handleClient();
   
  // handle MQTT:
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  else {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  }
  
  // handle WiFi
  if ( WiFi.status() == WL_DISCONNECTED )
  {
    Serial.print("\nWiFi connection lost. Reconnecting ");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nReconnected to WiFi: ");
  Serial.println(WiFi.localIP());
  }  

   pirSensor();
}

// handling PIR

unsigned long previousMillis = 0;
const long interval = 10000;
int pirState = LOW;
boolean lockLow = true;
boolean takeLowTime;
long unsigned int lowIn;

void pirSensor()
{
  unsigned long currentMillis = millis();
  if (mqttClient.connected()) {
  if (digitalRead(pirPin) == HIGH) {
    if(lockLow){
    if(pirState == LOW)
    {
      Serial.print('R');
      mqttClient.publish(pir_state_topic, "ON");
      pirState = HIGH;
    }
    lockLow = false;
    delay(50);
  }
  takeLowTime = true;
  }
  if(digitalRead(pirPin) == LOW)
  {
      if(takeLowTime)
      {
        lowIn = millis();
        takeLowTime = false;
      }
      if(!lockLow && millis() - lowIn > interval)
      {
        if (pirState == HIGH)
        {
          Serial.print('F');
          mqttClient.publish(pir_state_topic, "OFF");
          pirState = LOW;
        }
        lockLow = true;
        delay(50);
     }
   }   
}
}

void updateState(bool newState) {
  state = newState;
  
  if (mqttClient.connected()) {
    String payload = (state)?"ON":"OFF";
    
    if (mqttDebug) {
      Serial.print("MQTT out: ");
      Serial.print(mqtt_state_topic);
      Serial.print(" = ");
      Serial.println(payload);
    }
    
    mqttClient.publish(mqtt_state_topic, payload.c_str(), true);
  }
}

void updateFade(bool newFade) {
  fade = newFade;
  
  if (mqttClient.connected()) {
    String payload = (fade)?"ON":"OFF";
    
    if (mqttDebug) {
      Serial.print("MQTT out: ");
      Serial.print(mqtt_fade_topic);
      Serial.print(" = ");
      Serial.println(payload);
    }
    
    mqttClient.publish(mqtt_fade_topic, (fade) ?"ON":"OFF", true);
  }
}

void updateBrightness(int newBrightness) {
  tarBrightness = newBrightness;
  
  if (mqttClient.connected()) {
    String payload = String(tarBrightness);
    
    if (mqttDebug) {
      Serial.print("MQTT out: ");
      Serial.print(mqtt_brightness_topic);
      Serial.print(" = ");
      Serial.println(payload);
    }

    mqttClient.publish(mqtt_brightness_topic, payload.c_str(), true);
  }
}

//Original code part to implement PIR as interrupt
/*void pirDetect() {
  if (mqttClient.connected()) {
    if (digitalRead(pirPin)) {
      Serial.print('R');
      mqttClient.publish(pir_state_topic, "ON");
      
    }
    else {
        Serial.print('F');
        mqttClient.publish(pir_state_topic, "OFF");
    }
  }
}*/

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char c_payload[length];
  memcpy(c_payload, payload, length);
  c_payload[length] = '\0';
  
  String s_topic = String(topic);
  String s_payload = String(c_payload);
  
  if (mqttDebug) {
    Serial.print("MQTT in: ");
    Serial.print(s_topic);
    Serial.print(" = ");
    Serial.print(s_payload);
  }

  if (s_topic == mqtt_state_topic) {
    if (mqttDebug) { Serial.println(""); }
    
    if (s_payload == "ON") {
      if (state != 1) { updateState(1); }
    }
    else if (s_payload == "OFF") {
      if (state != 0) { updateState(0); }
    }
  }
  else if (s_topic == mqtt_fade_topic) {
    if (mqttDebug) { Serial.println(""); }
     
    if (s_payload == "ON") {
      if (fade != 1) { updateFade(1); }
    }
    else if (s_payload == "OFF") {
      if (fade != 0) { updateFade(0); }
    }
  }
  else if (s_topic == mqtt_brightness_topic) {
    if (mqttDebug) { Serial.println(""); }
    
    if (s_payload.toInt() != tarBrightness) { updateBrightness((byte) s_payload.toInt()); }
  }
  else {
    if (mqttDebug) { Serial.println(" [unknown message]"); }
  }
}


void dimTimerISR() {
    if (fade == 1) {
      if (curBrightness > tarBrightness || (state == 0 && curBrightness > 0)) {
        --curBrightness;
      }
      else if (curBrightness < tarBrightness && state == 1 && curBrightness < 255) {
        ++curBrightness;
      }
    }
    else {
      if (state == 1) {
        curBrightness = tarBrightness;
      }
      else {
        curBrightness = 0;
      }
    }
    
    if (curBrightness == 0) {
      state = 0;
      digitalWrite(outPin, 0);
    }
    else if (curBrightness == 255) {
      state = 1;
      digitalWrite(outPin, 1);
    }
    else {
      digitalWrite(outPin, 1);
    }
    
    zcState = 0;
}

void zcDetectISR() {
  if (zcState == 0) {
    zcState = 1;
  
    if (curBrightness < 255 && curBrightness > 0) {
      digitalWrite(outPin, 0);
      
      int dimDelay = 37 * (255 - curBrightness) + 500;
      hw_timer_arm(dimDelay);
    }
  }
}
