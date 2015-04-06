#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
 
const char* ssid = "SSID";
const char* password = "PASSWORD";

const byte switchPin = 14;
const byte zcPin = 12;
const byte outPin = 13;

byte fade = 0;
byte state = 0;
byte tarBrightness = 255;
byte curBrightness = 0;
byte zcState = 0; // 0 = ready; 1 = processing; 2 = double trigger fix

ESP8266WebServer server(80);

void setup(void)
{
  pinMode(switchPin, INPUT_PULLUP);
  pinMode(zcPin, INPUT_PULLUP);
  pinMode(outPin, OUTPUT);

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
 
  server.on("/", [](){
    if (server.arg("s") != "") {
      if (server.arg("s") == "1" || server.arg("s") == "on" || server.arg("s") == "true") {
        state = 1;
      }
      else if (server.arg("s") == "t") {
        state = !state;
      }
      else {
        state = 0;
      }
    }
    
    if (server.arg("b") != "") {
      tarBrightness = (byte) server.arg("b").toInt();
    }
    
    if (server.arg("f") != "") {
      if (server.arg("f") == "1" || server.arg("f") == "on" || server.arg("f") == "true"){
        fade = 1;
      }
      else {
        fade = 0;
      }
    }
    
    String s = "{\n   \"s\":";
    s += state;
    s += ",\n   \"b\":";
    s += tarBrightness;
    s += ",\n   \"f\":";
    s += fade;
    s += "\n}";
    
    server.send(200, "text/plain", s);
  });
  
  server.begin();
  Serial.println("HTTP server started");
  
  attachInterrupt(switchPin, switchDetect, CHANGE);
  attachInterrupt(zcPin, zcDetect, RISING);
}

void loop(void)
{
  server.handleClient();
}

void switchDetect()
{
  detachInterrupt(switchPin);

  state = !state;
  
  // debounce
  delayMicroseconds(500); // delay(5) causes wdt reset (bug?)
  
  attachInterrupt(switchPin, switchDetect, CHANGE);
}

void zcDetect()
{
  //detachInterrupt(zcPin);
  if (zcState == 0) {
    zcState = 1;
  
    if (curBrightness < 255 && curBrightness > 0) {
      digitalWrite(outPin, 0);
      
      int dimDelay = 30 * (255 - curBrightness) + 400;    
      delayMicroseconds(dimDelay);
      
      digitalWrite(outPin, 1);
      delayMicroseconds(150);
    }
    
    if (fade == 1 && (curBrightness > tarBrightness || (state == 0 && curBrightness > 0))) {
      curBrightness -= 1;
    }
    else if (fade == 1 && curBrightness < tarBrightness && state == 1 && curBrightness < 255) {
      curBrightness += 1;
    }
    else if (fade == 0 && state == 1) {
      curBrightness = tarBrightness;
    }
    else if (fade == 0 && state == 0) {
      curBrightness = 0;
    }
    
    if (curBrightness == 0) {
      state = 0;
      digitalWrite(outPin, 0);
    }
    else if (curBrightness == 255) {
      state = 1;
      digitalWrite(outPin, 1);
    }
    
    zcState = 2;
  }
  else if (zcState == 2) {
    zcState = 0;
  }
  //attachInterrupt(zcPin, zcDetect, RISING); 
}




