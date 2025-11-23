#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>

WiFiUDP udp;
const int UDP_PORT = 4210;

const int PIN_R = D5;
const int PIN_G = D4;
const int PIN_B = D3;

void setLED(int r, int g, int b) {
    analogWrite(PIN_R, r);
    analogWrite(PIN_G, g);
    analogWrite(PIN_B, b);
}

const int PIN_LDR = A0;

int readLDR() {
    return analogRead(PIN_LDR); // 0-1023
}

struct LampState {
  bool running = false;
  int color[3] = {255,255,255}; // RGB
  int LDR_OFF_THRESHOLD = 420;
  int LDR_ON_THRESHOLD = 520;
  unsigned long lastReactionMs = 0;
};

LampState lamp;

const char* ssid = "PÕRANDAPOD_MASTER"; // master AP
const char* password = "salajane123";

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while(WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP: "); Serial.println(WiFi.localIP());

  udp.begin(UDP_PORT);
  Serial.println("Listening for master commands on UDP port 4210");
}

void processCommand(String cmd) {
  // Expected format: path|payload
  int sep = cmd.indexOf('|');
  String path = cmd.substring(0, sep);
  String payload = (sep > 0) ? cmd.substring(sep + 1) : "";

  if(path == "/start") {
    lamp.running = true;
    Serial.println("Lamp started!");
  } else if(path == "/stop") {
    lamp.running = false;
    Serial.println("Lamp stopped!");
  } else if(path == "/setcolor") {
    if(payload.startsWith("color=")) {
        long val = strtol(payload.substring(6).c_str(), NULL, 16);
        lamp.color[0] = (val >> 16) & 0xFF;
        lamp.color[1] = (val >> 8) & 0xFF;
        lamp.color[2] = val & 0xFF;
        Serial.printf("Color set: R=%d G=%d B=%d\n", lamp.color[0], lamp.color[1], lamp.color[2]);
        setLED(lamp.color[0], lamp.color[1], lamp.color[2]); // immediately update LED
    }
  } else if(path == "/set") {
    int pOff = payload.indexOf("off=");
    int pOn = payload.indexOf("on=");
    if(pOff != -1) lamp.LDR_OFF_THRESHOLD = payload.substring(pOff+4).toInt();
    if(pOn  != -1) lamp.LDR_ON_THRESHOLD  = payload.substring(pOn+3).toInt();
    Serial.printf("Thresholds set: OFF=%d, ON=%d\n", lamp.LDR_OFF_THRESHOLD, lamp.LDR_ON_THRESHOLD);
  }
}

void checkUDP() {
  int packetSize = udp.parsePacket();
  if(packetSize) {
    char buf[128] = {0};
    int len = udp.read(buf, sizeof(buf)-1);
    if(len>0) buf[len] = 0;
    String cmd = String(buf);
    processCommand(cmd);
  }
}

void checkLDR() {
    if(lamp.running) {
        int val = readLDR(); // read analog value
        if(val > lamp.LDR_ON_THRESHOLD) {
            lamp.lastReactionMs = millis();
            lamp.running = false;
            Serial.printf("Reaction detected! Time: %lu ms\n", lamp.lastReactionMs);
        }
        // Update LED while lamp is running
        setLED(lamp.color[0], lamp.color[1], lamp.color[2]);
    } else {
        // turn off LED when lamp is stopped
        setLED(0,0,0);
    }
}


void handleHTTP() {
  WiFiClient client = WiFiServer(80).available();
  if(!client) return;

  String req = client.readStringUntil('\r');
  client.flush();

  if(req.indexOf("/status") >= 0) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.printf("{\"name\":\"Slave Lamp\",\"lastReactionMs\":%lu}\n", lamp.lastReactionMs);
  } else {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("OK");
  }
  client.stop();
}

WiFiServer httpServer(80);

void loop() {
  checkUDP();
  checkLDR();

  WiFiClient client = httpServer.available();
if (client) {
    // read the request and process it
    // Example: parse the incoming line
    String req = client.readStringUntil('\r');
    // do something with 'req'
    client.flush();
    client.stop();
}

}
