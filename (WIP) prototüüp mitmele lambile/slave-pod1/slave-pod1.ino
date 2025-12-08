#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>

WiFiUDP udp;
const int UDP_PORT = 4210;

const int PIN_R = D5;
const int PIN_G = D4;
const int PIN_B = D3;

WiFiServer httpServer(80);

#define LAMP_ID 1 //--------------------------------------igal lambil erinev----------------------//

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

void reportToMaster() {
    WiFiClient client;
    if (client.connect("192.168.4.1", 80)) {
        client.print("GET /online?id=");
        client.print(LAMP_ID);
        client.println(" HTTP/1.1");
        client.println("Host: 192.168.4.1");
        client.println("Connection: close");
        client.println();
    }
}

void reportOnline() {
    HTTPClient http;
    WiFiClient client;

    String url = "http://192.168.4.1/online?id=" + String(LAMP_ID);

    if (http.begin(client, url)) {
        http.GET();
        http.end();
    }
}

unsigned long lastPing = 0;

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

  // IMPORTANT: start HTTP server!
  httpServer.begin();                 // <<< ADD THIS

  udp.begin(UDP_PORT);
  Serial.println("Listening for master commands on UDP port 4210");
}


void processCommand(String cmd) {
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
    }
  } else if(path == "/set") {
    int pOff = payload.indexOf("off=");
    int pOn = payload.indexOf("on=");
    if(pOff != -1) lamp.LDR_OFF_THRESHOLD = payload.substring(pOff+4).toInt();
    if(pOn  != -1) lamp.LDR_ON_THRESHOLD  = payload.substring(pOn+3).toInt();
    Serial.printf("Thresholds set: OFF=%d, ON=%d\n", lamp.LDR_OFF_THRESHOLD, lamp.LDR_ON_THRESHOLD);
  }
}

void checkLDR() {
    if(lamp.running) {
        int val = readLDR();
        if(val > lamp.LDR_ON_THRESHOLD) {
            lamp.lastReactionMs = millis();
            lamp.running = false; // stop lamp
            Serial.printf("Reaction detected! Time: %lu ms\n", lamp.lastReactionMs);
        }
    }
}

void updateLED() {
    // Single authoritative place for LED
    if(lamp.running) setLED(lamp.color[0], lamp.color[1], lamp.color[2]);
    else setLED(0,0,0);
}

void checkUDP() { 
  int packetSize = udp.parsePacket(); 
  if(packetSize) { 
    char buf[128] = {0}; 
    int len = udp.read(buf, sizeof(buf)-1); 
    if(len>0) 
      buf[len] = 0; 
      String cmd = String(buf); 
      processCommand(cmd); 
      } 
  }



void handleHTTP() {
    WiFiClient client = httpServer.available();
    if (!client) return;

    String req = client.readStringUntil('\r');
    client.flush();

    // ---- PING endpoint (master uses this!) ----
    if (req.indexOf("GET /ping") >= 0) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/plain");
        client.println("Connection: close");
        client.println();
        client.println("pong");
    }

    // ---- ONLINE confirmation (optional, but good) ----
    else if (req.indexOf("GET /online") >= 0) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/plain");
        client.println("Connection: close");
        client.println();
        client.println("OK");
    }

    // ---- STATUS ----
    else if (req.indexOf("GET /status") >= 0) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println();
        client.printf("{\"name\":\"Slave Lamp %d\",\"lastReactionMs\":%lu}\n",
                      LAMP_ID, lamp.lastReactionMs);
    }

    // ---- DEFAULT ----
    else {
        client.println("HTTP/1.1 404 Not Found");
        client.println("Connection: close");
        client.println();
    }

    client.stop();
}

void loop() {
  checkUDP();
  checkLDR();
  handleHTTP();
  updateLED();      // sets physical LED correctly

  if (millis() - lastPing > 3000) {
        lastPing = millis();
        reportOnline();
    }

}
