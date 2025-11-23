#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <aWOT.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
WiFiUDP udp;


Application app;
WiFiServer server(80);
const int UDP_PORT = 4210;

void redirectHome(Response &res);

// Master lamp hardware pins
const int LDR_PIN = A0;           // Light sensor pin
const int LED_R_PIN = D5;         // Example: Red channel
const int LED_G_PIN = D4;         // Green channel
const int LED_B_PIN = D3;         // Blue channel

// Small action queue so handlers are instantaneous
enum ActionType { ACT_STOP, ACT_START, ACT_SET_COLOR, ACT_SET_THRESHOLD, ACT_FETCH_RESULTS };
struct Action {
  ActionType type;
  int lamp;      // lamp index for per-lamp actions, -1 for broadcast
  String payload; // extra data (e.g., "color=#ff00ff" or "off=100&on=300")
};

const int MAX_PENDING = 64;
Action pending[MAX_PENDING];
int pendingHead = 0;
int pendingTail = 0;

bool enqueueAction(const Action &a) {
  int next = (pendingTail + 1) % MAX_PENDING;
  if (next == pendingHead) return false; // full
  pending[pendingTail] = a;
  pendingTail = next;
  return true;
}
bool hasPending() { return pendingHead != pendingTail; }
bool dequeueAction(Action &a) {
  if (!hasPending()) return false;
  a = pending[pendingHead];
  pendingHead = (pendingHead + 1) % MAX_PENDING;
  return true;
}

// ------------------------------------------

volatile bool startTrialFlag = false;

struct Result {
  String name;
  unsigned long timeMs;
};

Result results[200];
int resultCount = 0;

struct Lamp {
  String ip;
  String name;
  int LDR_OFF_THRESHOLD = 420;
  int LDR_ON_THRESHOLD  = 520;
};

Lamp lamps[5] = {
  {"192.168.4.2", "Lamp 1"},
  {"192.168.4.3", "Lamp 2"},
  {"192.168.4.4", "Lamp 3"},
  {"192.168.4.5", "Lamp 4"},
  {"192.168.4.1", "Lamp 5"},
};

int activeLamp = -1;

// --- Lamp state for master itself ---
bool lampRunning = false;
int lampColor[3] = {255,255,255};   // RGB
unsigned long lastReaction = 0;

// ---------- Forward declarations ----------
void performFetchResults(); // called from loop via action

void redirectHome(Response &res){
    res.status(303);
    res.set("Location", "/");
    res.set("Content-Type", "text/plain; charset=utf-8");
    res.println("OK");
    res.end();
}

// ---------- Helpers ----------
bool readPostInt(Request &req, const char *name, int &value) {
    char buf[16] = {0};
    if (req.form((char*)name, strlen(name), buf, sizeof(buf))) {
        value = atoi(buf);
        return true;
    }
    return false;
}

void addResult(String name, unsigned long timeMs){
  if(resultCount < 200){
    results[resultCount].name = name;
    results[resultCount].timeMs = timeMs;
    resultCount++;
  }
}

void sortResults(){
  for(int i=0;i<resultCount-1;i++){
    for(int j=i+1;j<resultCount;j++){
      if(results[j].timeMs < results[i].timeMs){
        Result tmp = results[i];
        results[i] = results[j];
        results[j] = tmp;
      }
    }
  }
}

// Quick lamp-online check (fast TCP connect)
bool isLampOnline(const String &ip) {
  if (ip.length() < 7) return false;
  WiFiClient c;
  bool ok = c.connect(ip.c_str(), 80);
  c.stop();
  return ok;
}

// Stop all lamps
void stopAllLamps(){
    for(int i=0;i<5;i++){
        // Send stop command without waiting for HTTP GET to complete
        WiFiClient client;
        HTTPClient http;

        String url = "http://" + lamps[i].ip + "/stop";

        if(http.begin(client, url)){
            http.setTimeout(300); // 0.3s max per lamp
            http.GET();           // fire-and-forget
            http.end();
        }
    }
}


int pickRandomOnlineLamp() {
    int onlineIndices[6];   // extra slot for master
    int onlineCount = 0;

    for(int i=0;i<5;i++){
        WiFiClient client;
        HTTPClient http;
        String url = "http://" + lamps[i].ip + "/status";

        if(http.begin(client, url)){
            http.setTimeout(300);
            int code = http.GET();
            http.end();
            if(code == 200){
                onlineIndices[onlineCount++] = i; 
            }
        }
    }

    // Include master lamp itself
    onlineIndices[onlineCount++] = 5; // index 5 = master

    if(onlineCount == 0) return -1;

    int idx = random(0, onlineCount);
    return onlineIndices[idx];
}





bool sendToLamp(String ip, String path, String value) {
    IPAddress addr;
    addr.fromString(ip);

    String msg = path + "|" + value;
    udp.beginPacket(addr, UDP_PORT);
    udp.print(msg);
    udp.endPacket();

    // If we’re sending to master itself, call handler directly
    if(ip == WiFi.softAPIP().toString()) {
        if(path == "/start") lampRunning = true;
        else if(path == "/stop") lampRunning = false;
        else if(path == "/setcolor") {
            long val = strtol(value.c_str(), NULL, 16);
            lampColor[0] = (val >> 16) & 0xFF;
            lampColor[1] = (val >> 8) & 0xFF;
            lampColor[2] = val & 0xFF;
        }
    }

    return true;
}


// ------------------------------
// Action processing (runs in loop)
void processMasterLamp() {
    // Update LED color
    if(lampRunning) {
        analogWrite(LED_R_PIN, lampColor[0]);
        analogWrite(LED_G_PIN, lampColor[1]);
        analogWrite(LED_B_PIN, lampColor[2]);
    } else {
        analogWrite(LED_R_PIN, 0);
        analogWrite(LED_G_PIN, 0);
        analogWrite(LED_B_PIN, 0);
    }

    // Check LDR threshold
    if(lampRunning) {
        int sensorValue = analogRead(LDR_PIN);
        if(sensorValue > lamps[0].LDR_ON_THRESHOLD) {  // assuming master lamp is index 0
            unsigned long reactionTime = millis();  // you may want a start timestamp
            addResult("Master Lamp", reactionTime);
            lampRunning = false;  // stop lamp
        }
    }
}


void processOneAction() {
  Action a;
  if (!dequeueAction(a)) return;

  switch (a.type) {
    case ACT_STOP:
      if (a.lamp >= 0 && a.lamp < 5) {
        if (isLampOnline(lamps[a.lamp].ip)) {
          sendToLamp(lamps[a.lamp].ip, "/stop", "");
        }
      } else {
        // broadcast stop
        for(int i=0;i<5;i++){
          if (isLampOnline(lamps[i].ip)) sendToLamp(lamps[i].ip, "/stop", "");
        }
      }
      break;

    case ACT_START:
      if (a.lamp >= 0 && a.lamp < 5) {
        if (isLampOnline(lamps[a.lamp].ip)) {
          sendToLamp(lamps[a.lamp].ip, "/start", "");
        }
      }
      break;

    case ACT_SET_COLOR:
    if (a.lamp >= 0 && a.lamp < 5) {
        if(a.lamp == 0) { // master lamp
            long val = strtol(a.payload.substring(6).c_str(), NULL, 16); // payload = "color=#RRGGBB"
            lampColor[0] = (val >> 16) & 0xFF;
            lampColor[1] = (val >> 8) & 0xFF;
            lampColor[2] = val & 0xFF;
            Serial.printf("Master lamp color updated: R=%d G=%d B=%d\n", lampColor[0], lampColor[1], lampColor[2]);
        } else if (isLampOnline(lamps[a.lamp].ip)) {
            sendToLamp(lamps[a.lamp].ip, "/setcolor", a.payload);
        }
    } else {
        // broadcast: payload is "color=#xxxxxx"
        for(int i=0;i<5;i++){
            if(i==0) {
                long val = strtol(a.payload.substring(6).c_str(), NULL, 16);
                lampColor[0] = (val >> 16) & 0xFF;
                lampColor[1] = (val >> 8) & 0xFF;
                lampColor[2] = val & 0xFF;
            } else if(isLampOnline(lamps[i].ip)) sendToLamp(lamps[i].ip, "/setcolor", a.payload);
        }
        Serial.println("Broadcast color updated, including master lamp.");
    }
    break;

    case ACT_SET_THRESHOLD:
      if (a.lamp >= 0 && a.lamp < 5) {
        // parse payload
        int off = lamps[a.lamp].LDR_OFF_THRESHOLD;
        int onv = lamps[a.lamp].LDR_ON_THRESHOLD;
        int pOff = a.payload.indexOf("off=");
        if (pOff != -1) off = a.payload.substring(pOff+4).toInt();
        int pOn = a.payload.indexOf("on=");
        if (pOn != -1) onv = a.payload.substring(pOn+3).toInt();
        lamps[a.lamp].LDR_OFF_THRESHOLD = off;
        lamps[a.lamp].LDR_ON_THRESHOLD = onv;

        // If master lamp, apply immediately
        if(a.lamp == 0) {
            Serial.printf("Master lamp thresholds updated: OFF=%d ON=%d\n", off, onv);
        } else if (isLampOnline(lamps[a.lamp].ip)) {
            sendToLamp(lamps[a.lamp].ip, "/set", a.payload);
        }
    }
    break;

    case ACT_FETCH_RESULTS:
      performFetchResults();
      break;
  }
}

// Perform the actual network fetch to populate results[] (blocking, but runs in loop via action)
void performFetchResults() {
  resultCount = 0; // refresh
  for (int i = 0; i < 5; ++i) {
    if (!isLampOnline(lamps[i].ip)) continue;
    WiFiClient client;
    HTTPClient http;
    String url = "http://" + lamps[i].ip + "/status";
    if (!http.begin(client, url)) { http.end(); continue; }
    http.setTimeout(250);
    int code = http.GET();
    if (code == 200) {
      String payload = http.getString();
      int nIdx = payload.indexOf("\"name\":\"");
      int tIdx = payload.indexOf("\"lastReactionMs\":");
      if (nIdx>=0 && tIdx>=0) {
        int nEnd = payload.indexOf("\"", nIdx+8);
        String name = payload.substring(nIdx+8, nEnd);
        int tEnd = payload.indexOf(",", tIdx+17);
        if (tEnd==-1) tEnd = payload.indexOf("}", tIdx+17);
        unsigned long timeMs = payload.substring(tIdx+17, tEnd).toInt();
        if (timeMs > 0) addResult(name, timeMs);
      }
    }
    http.end();
  }
  sortResults();
}

// ---------- Utility: hex->RGB ----------
void hexToRgb(const char *hex, int &r, int &g, int &b) {
    if (hex[0] == '#') hex++;
    long value = strtol(hex, NULL, 16);
    r = (value >> 16) & 0xFF;
    g = (value >> 8) & 0xFF;
    b = value & 0xFF;
}

// ---------- Web Pages ----------
void naitaEsilehte(Request &req, Response &res){
  res.set("Content-Type", "text/html; charset=utf-8");
  res.println("<!doctype html><html lang='et'><meta charset='utf-8'>");
  res.println("<meta name='viewport' content='width=device-width, initial-scale=1.2'>");
  res.println("<title>Põrandapodi – reaktsiooniaeg</title>");
  
  // --- Styles ---
  res.println("<style>"
               "body{font-family:Arial;margin:24px}"
               "h1{margin:0 0 12px 0}"
               ".row{display:flex;gap:12px;flex-wrap:wrap;align-items:center}"
               "button{padding:10px 16px;font-size:16px;border:none;border-radius:8px;color:#fff;cursor:pointer}"
               ".btn{background:#0d6efd}"
               ".btn-ghost{background:#6c757d}"
               "input[type=text]{padding:8px 10px;font-size:16px;border:1px solid #ccc;border-radius:6px}"
               "table{border-collapse:collapse;width:100%;max-width:640px;margin-top:10px}"
               "th,td{border:1px solid #ddd;padding:8px;text-align:left}"
               "th{background:#f7f7f7}"
               "a{color:#0d6efd;text-decoration:none}"
               ".pill{display:inline-block;padding:4px 10px;border-radius:999px;background:#eee;margin-left:8px}"
             "</style>");
  
  // --- Page header ---
  res.println("<h1>Põrandapodi – reaktsiooniaeg</h1>");
  
  // --- Controls row: START, SETTINGS, REFRESH ---
  res.println("<div class='row' style='margin-top:10px'>");
  res.println("<form action='/start' method='post' style='margin:0'><button class='btn'>START</button></form>");
  res.println("<form action='/cfg' method='get' style='margin:0'><button class='btn-ghost' style='margin-left:8px'>Settings</button></form>");
  res.println("<form action='/refresh' method='post' style='margin:0'><button class='btn' style='margin-left:8px;background:#198754'>Refresh leaderboard</button></form>");
  res.println("</div>");

  // --- Leaderboard (cached) ---
  res.println("<h3 style='margin-top:12px'>Edetabel</h3>");
  if(resultCount==0) res.println("<p>Edetabel on tühi. Klikka \"Refresh leaderboard\" et küsida kõigilt lampidelt viimased tulemused.</p>");
  else {
    res.println("<table><tr><th>#</th><th>Nimi</th><th>Aeg (ms)</th></tr>");
    for(int i=0;i<resultCount;i++){
      res.printf("<tr><td>%d</td><td>%s</td><td>%lu</td></tr>",
                 i+1, results[i].name.c_str(), results[i].timeMs);
    }
    res.println("</table>");
  }

  // --- Color presets (sends specific color to each lamp index) ---
  res.println("<h3 style='margin-top:12px'>Vali LED värvikomplekt</h3>");
  res.println("<div class='row' style='gap:10px'>"
               "<button class='btn' onclick=\"setColor(['#ff9a38','#29bfff','#ff84df','#c2d662','#f5e529'])\">Regular</button>"
               "<button class='btn' onclick=\"setColor(['#642fc1','#d0be00','#ff206e','#004a59','#ffffff'])\">Colorblind</button>"
               "</div>");

  // --- JS for presets: sends lamp index with each POST so handler knows which lamp -->
  res.println("<script>"
               "async function setColor(colors){"
                 "for(let i=0;i<5;i++){"
                   "const d = new URLSearchParams(); d.append('color', colors[i]);"
                   "await fetch('/setcolor?lamp='+i, {method:'POST', body: d});"
                 "}"
                 "alert('Värvikomplekt muudetud!');"
               "}"
             "</script>");

  res.println("</html>");
}

// ---------- Handlers ----------

// startTrial handler: enqueue non-blocking sequence (stop all, then start chosen lamp)
void startTrial(Request &req, Response &res){
  startTrialFlag = true; // set flag
  res.status(303); res.set("Location","/"); res.println("OK");
}

// setColorHandler used for single-lamp color posts (but main page uses /setcolor?lamp=i)
void setColorHandler(Request &req, Response &res){
    char keyColor[] = "color";
    char colorHex[16] = {0};
    if (req.form(keyColor, sizeof(keyColor), colorHex, sizeof(colorHex))) {
        int r,g,b;
        hexToRgb(colorHex,r,g,b);
        Serial.printf("Color set to %s → R=%d G=%d B=%d\n", colorHex,r,g,b);
    }
    redirectHome(res);
}

// Settings page: per-lamp controls
void naitaSeadeid(Request &req, Response &res){
  res.set("Content-Type", "text/html; charset=utf-8");
  res.println("<!doctype html><html lang='et'><meta charset='utf-8'>");
  res.println("<meta name='viewport' content='width=device-width, initial-scale=1.2'>");
  res.println("<title>Lampide seaded</title>");
  res.println("<style>"
               "body{font-family:Arial;margin:20px;max-width:700px}"
               "h1,h2{margin:0 0 10px 0}"
               ".row{display:flex;gap:10px;align-items:center;margin-bottom:10px}"
               "button{padding:8px 14px;border:none;border-radius:6px;background:#0d6efd;color:#fff;cursor:pointer}"
               "input[type=text]{padding:6px 10px;border:1px solid #ccc;border-radius:6px;width:80px}"
               ".small{background:#6c757d}"
             "</style>");
  
  res.println("<h1>Lampide seaded</h1>");
  // back to home button
  res.println("<form action='/' method='get' style='margin-bottom:12px'><button class='small'>Back</button></form>");

  for(int i=0;i<5;i++){
    res.printf("<h2>%s (%s)</h2>", lamps[i].name.c_str(), lamps[i].ip.c_str());

    res.println("<div class='row'>"
                 "<label>OFF lävi:</label>"
                 "<input type='text' id='off"+String(i)+"' value='"+String(lamps[i].LDR_OFF_THRESHOLD)+"'>"
                 "<label>ON lävi:</label>"
                 "<input type='text' id='on"+String(i)+"' value='"+String(lamps[i].LDR_ON_THRESHOLD)+"'>"
                 "<button onclick='setThreshold("+String(i)+")'>Salvesta</button>"
                 "</div>");

    res.println("<div class='row'>"
                 "<label>LED värv (#RRGGBB):</label>"
                 "<input type='text' id='color"+String(i)+"' value='#ffffff'>"
                 "<button onclick='setColor("+String(i)+")'>Muuda värv</button>"
                 "</div>");

    res.println("<div class='row'>"
                 "<button onclick='startLamp("+String(i)+")'>START</button>"
                 "<button onclick='stopLamp("+String(i)+")'>STOP</button>"
                 "</div><hr>");
  }

  // --- JS ---
  res.println("<script>"
               "async function setThreshold(i){"
                 "let off = document.getElementById('off'+i).value;"
                 "let on = document.getElementById('on'+i).value;"
                 "const data = new URLSearchParams();"
                 "data.append('off',off); data.append('on',on);"
                 "await fetch('/setthreshold?lamp='+i,{method:'POST',body:data});"
                 "alert('Threshold saved!');"
               "}"
               "async function setColor(i){"
                 "let c = document.getElementById('color'+i).value;"
                 "const data = new URLSearchParams();"
                 "data.append('color',c);"
                 "await fetch('/setcolor?lamp='+i,{method:'POST',body:data});"
                 "alert('Color updated!');"
               "}"
               "async function startLamp(i){"
                 "await fetch('/startlamp?lamp='+i,{method:'POST'});"
                 "alert('Lamp '+(i+1)+' started');"
               "}"
               "async function stopLamp(i){"
                 "await fetch('/stoplamp?lamp='+i,{method:'POST'});"
                 "alert('Lamp '+(i+1)+' stopped');"
               "}"
             "</script>");
  
  res.println("</html>");
}

// --- Handlers for individual lamps ---
// set thresholds: enqueue ACT_SET_THRESHOLD for lamp i
void setThresholdHandler(Request &req, Response &res){
  char lampBuf[8] = {0};
  req.query("lamp", lampBuf, sizeof(lampBuf));
  int i = atoi(lampBuf);

  if(i < 0 || i >= 5) {
    res.println("Invalid lamp");
    return;
  }

  int offVal = lamps[i].LDR_OFF_THRESHOLD;
  int onVal  = lamps[i].LDR_ON_THRESHOLD;

  // read posted values
  readPostInt(req, "off", offVal);
  readPostInt(req, "on", onVal);

  String data = "off=" + String(offVal) + "&on=" + String(onVal);
  Action a; a.type = ACT_SET_THRESHOLD; a.lamp = i; a.payload = data;
  enqueueAction(a);

  redirectHome(res);
}

void startLampHandler(Request &req, Response &res){
  char lampBuf[8] = {0};
  req.query("lamp", lampBuf, sizeof(lampBuf));
  int i = atoi(lampBuf);

  if(i>=0 && i<5) {
    Action a; a.type = ACT_START; a.lamp = i; a.payload = "";
    enqueueAction(a);
  }
  redirectHome(res);
}

void stopLampHandler(Request &req, Response &res){
  char lampBuf[8] = {0};
  req.query("lamp", lampBuf, sizeof(lampBuf));
  int i = atoi(lampBuf);

  if(i>=0 && i<5) {
    Action a; a.type = ACT_STOP; a.lamp = i; a.payload = "";
    enqueueAction(a);
  }
  redirectHome(res);
}

// setColor per lamp (called with ?lamp=i)
void setColorLampHandler(Request &req, Response &res){
  char lampBuf[8] = {0};
  req.query("lamp", lampBuf, sizeof(lampBuf));
  int i = atoi(lampBuf);

  if(i < 0 || i >= 5) {
    redirectHome(res);
    return;
  }

  char colorHex[16] = {0};
  // mutable name
  char namebuf[] = "color";
  if (req.form(namebuf, strlen(namebuf), colorHex, sizeof(colorHex))) {
    Action a; a.type = ACT_SET_COLOR; a.lamp = i; a.payload = String("color=") + String(colorHex);
    enqueueAction(a);
  }

  redirectHome(res);
}

// Refresh leaderboard (enqueue fetch)
void refreshHandler(Request &req, Response &res){
  Action a; a.type = ACT_FETCH_RESULTS; a.lamp = -1; a.payload = "";
  enqueueAction(a);
  redirectHome(res);
}

// ---------- Setup / Loop ----------
void setup(){
  Serial.begin(115200);
  delay(500);

  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);

  // turn off LED initially
  analogWrite(LED_R_PIN, 0);
  analogWrite(LED_G_PIN, 0);
  analogWrite(LED_B_PIN, 0);

  udp.begin(4210);  // port for sending commands

  WiFi.softAP("PÕRANDAPOD_MASTER","salajane123");
  Serial.print("Master AP IP: "); Serial.println(WiFi.softAPIP());

  randomSeed(analogRead(A0));

  app.get("/", naitaEsilehte);
  app.post("/start", startTrial);
  app.post("/setcolor", setColorHandler);      // legacy/broadcast
  app.get("/cfg", naitaSeadeid);
  app.post("/setthreshold", setThresholdHandler);
  app.post("/setcolor", setColorLampHandler);  // per-lamp setcolor (uses ?lamp=)
  app.post("/startlamp", startLampHandler);
  app.post("/stoplamp", stopLampHandler);
  app.post("/refresh", refreshHandler);

  server.begin();
  Serial.println("Master webserver listening...");
}

void loop(){
    WiFiClient client = server.available();
    if(client){ 
        app.process(&client); 
        client.stop(); 
    }

    // Run slow ops non-blocking
    if(startTrialFlag){
        startTrialFlag = false;

        stopAllLamps();

        activeLamp = pickRandomOnlineLamp();
        if(activeLamp >= 0){
        if(activeLamp < 5)
            sendToLamp(lamps[activeLamp].ip, "/start", "");
        else
            lampRunning = true; // master lamp
        Serial.printf("Trial started on lamp %d\n", activeLamp);
      }

    }

    // Process master lamp (your existing code)
    processMasterLamp();
}

