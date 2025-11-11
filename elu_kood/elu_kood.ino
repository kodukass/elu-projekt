#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <aWOT.h>
#include <cstring> // strlen, strncpy, memset, snprintf

// 1) WiFi seaded AP
const char* WIFI_NIMI   = "PÕRANDAPOD_WIFI";
const char* WIFI_PAROOL = "salajane123";

// 2) PIN-id
#define RED_PIN   D5
#define GREEN_PIN D4
#define BLUE_PIN  D3
#define LDR_PIN   A0     // NodeMCU A0 (0..1023)

// --- LED värv ---
int activeR = 1023, activeG = 1023, activeB = 1023;
int current_r = -1, current_g = -1, current_b = -1;

// --- LDR seaded ---
int LDR_OFF_THRESHOLD = 420; // vaikeseadistus
int LDR_ON_THRESHOLD  = 520; // vaikeseadistus

// --- Reaktsiooniaeg / trial ---
bool trialArmed     = false;
bool trialDone      = false;
unsigned long trialStartMs = 0;
unsigned long lastReactionMs = 0;

// --- Katsete logi ---
static const int MAX_TIMES = 200;
float timesSec[MAX_TIMES];
int timesCount = 0;

// --- Katse tegija nimi ---
char participantName[32] = "Nimetu";

// Server
WiFiServer server(80);
Application app;

// ---------- Abi ----------
void setLedColor(int r, int g, int b) {
  if (r != current_r || g != current_g || b != current_b) {
    analogWrite(RED_PIN, r);
    analogWrite(GREEN_PIN, g);
    analogWrite(BLUE_PIN, b);
    current_r = r; current_g = g; current_b = b;
  }
}

int readLDRRaw() {
  return analogRead(LDR_PIN);
}

int ldrLevel() {
  return readLDRRaw(); // lihtsustatud, ilma invertita
}

void pushTime(float sec) {
  if (timesCount < MAX_TIMES) timesSec[timesCount++] = sec;
  else {
    for (int i = 1; i < MAX_TIMES; ++i) timesSec[i-1] = timesSec[i];
    timesSec[MAX_TIMES-1] = sec;
  }
}

void resetTimes() { timesCount = 0; }

void hexToRgb(const char* hex, int &r, int &g, int &b) {
  if (hex[0] == '#') hex++; // skip #
  char buf[3]; buf[2] = 0;

  buf[0] = hex[0]; buf[1] = hex[1]; r = strtol(buf, NULL, 16);
  buf[0] = hex[2]; buf[1] = hex[3]; g = strtol(buf, NULL, 16);
  buf[0] = hex[4]; buf[1] = hex[5]; b = strtol(buf, NULL, 16);

  // Scale 0-255 to 0-1023 for analogWrite
  r = r * 1023 / 255;
  g = g * 1023 / 255;
  b = b * 1023 / 255;
}

// ---------- POST abifunktsioonid ----------
bool readPostInt(Request &req, const char* key, int &out) {
  char tmp[24];
  strncpy(tmp, key, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0';

  char buf[16];
  memset(buf, 0, sizeof(buf));
  bool ok = req.form(tmp, sizeof(tmp), buf, sizeof(buf));
  if (!ok) {
    Serial.printf("Param %s puudub\n", key);
    return false;
  }
  out = atoi(buf);
  Serial.printf("Param %s = %s (int %d)\n", key, buf, out);
  return true;
}

bool readPostString(Request &req, const char* key, char* dst, int dstLen) {
  char name[16];                       // buffer võtme jaoks
  strncpy(name, key, sizeof(name)-1);
  name[sizeof(name)-1] = '\0';

  char value[64]; memset(value, 0, sizeof(value));

  bool ok = req.form(name, sizeof(name), value, sizeof(value));
  if (!ok) return false;

  strncpy(dst, value, dstLen-1);
  dst[dstLen-1] = '\0';
  Serial.printf("Param %s = %s (string)\n", key, dst);
  return true;
}


// ---------- HTML ----------
void naitaEsilehte(Request &req, Response &res) {
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
               ".btn-threshold{background:#0d6efd}"
               ".btn-color{background:#198754}" 
               "input[type=text]{padding:8px 10px;font-size:16px;border:1px solid #ccc;border-radius:6px}"
               "table{border-collapse:collapse;width:100%;max-width:640px;margin-top:10px}"
               "th,td{border:1px solid #ddd;padding:8px;text-align:left}"
               "th{background:#f7f7f7}"
               "a{color:#0d6efd;text-decoration:none}"
               ".pill{display:inline-block;padding:4px 10px;border-radius:999px;background:#eee;margin-left:8px}"
             "</style>");
  
  // --- Page header ---
  res.println("<h1>Põrandapodi – reaktsiooniaeg</h1>");
  
  // --- Name form ---
  res.println("<form class='row' action='/name' method='post'>"
               "<label>Katse tegija nimi:</label>"
               "<input type='text' name='name' placeholder='Sisesta nimi' value='");
  res.print(participantName);
  res.println("'> <button class='btn' type='submit'>Salvesta nimi</button></form>");
  
  // --- START button ---
  res.println("<form style='margin-top:10px' action='/start' method='post'>"
               "<button class='btn'>START</button></form>");
  
  // --- Trial status ---
  res.print("<p style='margin-top:6px'>Olek: <b>");
  if (trialArmed && !trialDone) res.print("katse käib – lamp põleb, ootan anduri puudet");
  else if (trialDone)           res.print("katse lõpetatud – lamp kustu");
  else                          res.print("valmis uueks katseks – lamp kustu");
  res.println("</b></p>");
  
  // --- Last reaction ---
  char buf[24];
  snprintf(buf, sizeof(buf), "%.2f", lastReactionMs / 1000.0f);
  res.println("<h3>Viimane aeg</h3>");
  res.print("<p><b>"); res.print(buf); res.println(" s</b></p>");
  
  // --- All measured times ---
  res.println("<h3>Kõik mõõdetud ajad</h3>");
  if (timesCount == 0) res.println("<p>Veel pole ühtegi tulemust.</p>");
  else {
    res.println("<table><tr><th>#</th><th>Aeg (s)</th></tr>");
    for (int i=0; i<timesCount; ++i) {
      char tbuf[24]; snprintf(tbuf,sizeof(tbuf),"%.2f",timesSec[i]);
      res.print("<tr><td>"); res.print(i+1);
      res.print("</td><td>"); res.print(tbuf); res.println("</td></tr>");
    }
    res.println("</table>");
  }
  
  // --- CSV / Reset / Config links ---
  res.println("<div class='row' style='margin-top:10px'>"
               "<a href='/csv' class='btn' style='background:#198754;display:inline-block;padding:10px 16px;color:#fff;border-radius:8px'>Laadi alla CSV</a>"
               "<form action='/reset' method='post' onsubmit='return confirm(\"Kustutan kõik ajad?\")'>"
               "<button class='btn' style='background:#dc3545'>Reset</button></form>"
               "<a href='/cfg'>⚙️ Anduri seaded</a>"
               "</div>");
  
  // --- LED color buttons ---
  res.println("<h3>Vali LED värvikomplekt</h3>");
  res.println("<div class='row' style='gap:10px'>"
               "<button class='btn-color' onclick=\"setColorPair('#d04a59','#ff206e')\">Komplekt 1</button>"
               "<button class='btn-color' onclick=\"setColorPair('#ff9a38','#29bfff')\">Komplekt 2</button>"
               "</div>");
  
  // --- LDR live info ---
  int raw = readLDRRaw(), lvl = ldrLevel();
  res.println("<hr><h3>Andur</h3>");
  res.print("<p>RAW: <b id='raw'>"); res.print(raw);
  res.print("</b> | LVL: <b id='lvl'>"); res.print(lvl);
  res.print("</b> <span class='pill'>OFF lävi: "); res.print(LDR_OFF_THRESHOLD);
  res.print("</span> <span class='pill'>ON lävi: "); res.print(LDR_ON_THRESHOLD);
  res.println("</span></p>");
  
  // --- JavaScript: LDR tick + LED color sets ---
  res.println("<script>"
               "async function tick(){"
                 "try{"
                   "const r=await fetch('/status');"
                   "const j=await r.json();"
                   "document.getElementById('raw').textContent=j.raw;"
                   "document.getElementById('lvl').textContent=j.lvl;"
                 "}catch(e){}"
               "}"
               "setInterval(tick,1000); tick();"
               
               "function hexToRgb(hex){"
                 "if(hex[0]=='#') hex=hex.substr(1);"
                 "return [parseInt(hex.substr(0,2),16),parseInt(hex.substr(2,2),16),parseInt(hex.substr(4,2),16)];"
               "}"
               
               "async function setColorPair(hex1,hex2){"
                 "const rgb1=hexToRgb(hex1);"
                 "const rgb2=hexToRgb(hex2);"
                 "const data1=new URLSearchParams(); data1.append('color', hex1);"
                 "const data2=new URLSearchParams(); data2.append('color', hex2);"
                 "await fetch('/setcolor',{method:'POST', body:data1});"
                 "await fetch('/setcolor',{method:'POST', body:data2});"
                 "alert('LED värvikomplekt muudetud!');"
               "}"
             "</script>");
  
  res.println("</html>");
}

// ---------- Anduri seaded ----------
void naitaSeadeid(Request &req, Response &res) {
  res.set("Content-Type", "text/html; charset=utf-8");
  res.println("<!doctype html><html lang='et'><meta charset='utf-8'>");
  res.println("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  res.println("<title>Anduri seaded</title>");
  res.println("<style>"
               "body{font-family:Arial;margin:20px;max-width:640px}"
               "label{display:block;margin-top:12px;font-weight:bold}"
               "input[type=range]{width:100%}"
               "button{padding:10px 16px;border:none;border-radius:8px;background:#0d6efd;color:#fff;cursor:pointer;margin-top:12px}"
               ".btn-threshold{background:#0d6efd;color:#fff;}"
               ".row{display:flex;gap:12px;align-items:center}"
               ".pill{display:inline-block;padding:4px 10px;border-radius:999px;background:#eee;margin-left:8px}"
               "a{color:#0d6efd;text-decoration:none}</style>");

  res.println("<h2>⚙️ Valgusanduri seaded</h2>");
  
  // --- Vorm ---
  res.println("<h2>⚙️ Valgusanduri seaded</h2>");

// Buttons with hard-coded threshold values
res.println("<div class='row' style='gap:10px'>"
             "<button onclick='setThreshold(100)' class='btn-threshold'>100</button>"
             "<button onclick='setThreshold(150)' class='btn-threshold'>150</button>"
             "<button onclick='setThreshold(200)' class='btn-threshold'>200</button>"
             "<button onclick='setThreshold(250)' class='btn-threshold'>250</button>"
             "</div>");

// JS function to POST the selected value
res.println("<script>"
             "async function setThreshold(val){"
               "const data = new URLSearchParams();"
               "data.append('value', val);"
               "await fetch('/set', {method:'POST', body:data});"
               "alert('LDR threshold set to ' + val);"
             "}"
           "</script>");
  
  res.println("</html>");
}


// ---------- JSON / CSV ----------
void statusJson(Request &req, Response &res) {
  res.set("Content-Type", "application/json; charset=utf-8");
  res.print("{\"raw\":"); res.print(readLDRRaw());
  res.print(",\"lvl\":"); res.print(ldrLevel());
  res.print(",\"off\":"); res.print(LDR_OFF_THRESHOLD);
  res.print(",\"on\":"); res.print(LDR_ON_THRESHOLD);
  res.print(",\"trialArmed\":"); res.print(trialArmed ? "true":"false");
  res.print(",\"trialDone\":");  res.print(trialDone ? "true":"false");
  res.print(",\"lastReactionMs\":"); res.print(lastReactionMs);
  res.print(",\"count\":"); res.print(timesCount);
  res.println("}");
}

void exportCsv(Request &req, Response &res) {
  res.set("Content-Type", "text/csv; charset=utf-8");
  res.set("Content-Disposition", "attachment; filename=reaktsiooniajad.csv");
  res.print("name,index,time_s\r\n");
  for (int i=0; i<timesCount; ++i) {
    char tbuf[24]; snprintf(tbuf,sizeof(tbuf),"%.2f",timesSec[i]);
    res.print(participantName); res.print(",");
    res.print(i+1); res.print(",");
    res.print(tbuf); res.print("\r\n");
  }
}

// ---------- Handlerid ----------
void redirectHome(Response &res) {
  res.status(303);
  res.set("Location", "/");
  res.set("Content-Type", "text/plain; charset=utf-8");
  res.println("OK"); res.end();
}

void startTrial(Request &req, Response &res) {
  trialArmed = true; trialDone = false; trialStartMs = millis();
  activeR=1023; activeG=1023; activeB=1023; setLedColor(activeR,activeG,activeB);
  Serial.println("START: katse armeeritud, lamp põleb.");
  redirectHome(res);
}

void saveName(Request &req, Response &res) {
  readPostString(req,"name",participantName,sizeof(participantName));
  Serial.print("Nimi salvestatud: "); Serial.println(participantName);
  redirectHome(res);
}

void salvestaSeaded(Request &req, Response &res) {
  Serial.println("--- SALVESTAN SEADED ---");

  int val = LDR_OFF_THRESHOLD; // default to current value
  if (readPostInt(req, "value", val)) {
    // Clamp value
    if (val < 0) val = 0;
    if (val > 1023) val = 1023;

    LDR_OFF_THRESHOLD = val;
    LDR_ON_THRESHOLD  = val + 100; // ON is 100 above OFF
    Serial.printf("Uued väärtused: OFF=%d, ON=%d\n", LDR_OFF_THRESHOLD, LDR_ON_THRESHOLD);
  }

  redirectHome(res);
}

void doReset(Request &req, Response &res) {
  resetTimes();
  lastReactionMs = 0;
  trialArmed = false; trialDone=false;
  setLedColor(0,0,0);
  Serial.println("Kõik ajad kustutatud.");
  redirectHome(res);
}

void setColorHandler(Request &req, Response &res) {
  char colorHex[8]; // expect #RRGGBB
  if(readPostString(req, "color", colorHex, sizeof(colorHex))) {
    int r, g, b;
    hexToRgb(colorHex, r, g, b);
    activeR = r; activeG = g; activeB = b;
    setLedColor(activeR, activeG, activeB);
    Serial.printf("LED color set to %s -> R=%d G=%d B=%d\n", colorHex, r, g, b);
  }
  redirectHome(res);
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(RED_PIN,OUTPUT); pinMode(GREEN_PIN,OUTPUT); pinMode(BLUE_PIN,OUTPUT);
  analogWriteRange(1023);
  pinMode(LDR_PIN,INPUT);

  Serial.println("Käivitan Wi-Fi AP...");
  int tulemus = WiFi.softAP(WIFI_NIMI,WIFI_PAROOL);
  Serial.print("AP staatus: "); Serial.println(tulemus?"OK":"VIGA");
  Serial.print("Mine aadressile: http://"); Serial.println(WiFi.softAPIP());

  // Marsruudid
  app.get("/",naitaEsilehte);
  app.get("/cfg",naitaSeadeid);
  app.get("/status",statusJson);
  app.get("/csv",exportCsv);

  app.post("/set",salvestaSeaded);
  app.post("/start",startTrial);
  app.post("/name",saveName);
  app.post("/reset",doReset);
  app.post("/setcolor", setColorHandler);

  server.begin();
  Serial.println("Veebiserver kuulab pordil 80.");
}

void loop() {
  WiFiClient klient = server.available();
  if(klient){ app.process(&klient); klient.stop(); }

  if(trialArmed && !trialDone){
    setLedColor(activeR,activeG,activeB);
    int lvl=ldrLevel();
    if(lvl<LDR_OFF_THRESHOLD){
      lastReactionMs=millis()-trialStartMs;
      pushTime(lastReactionMs/1000.0f);
      trialDone=true; trialArmed=false;
      setLedColor(0,0,0);
      Serial.printf("KATSE LÕPP — aeg: %.2f s.\n", lastReactionMs/1000.0f);
    }
  } else setLedColor(0,0,0);

  delay(10);
}
