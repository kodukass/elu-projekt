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
  res.println("<style>"
              "body{font-family:Arial;margin:24px} h1{margin:0 0 12px 0}"
              ".row{display:flex;gap:12px;flex-wrap:wrap;align-items:center}"
              "button{padding:10px 16px;font-size:16px;border:none;border-radius:8px;color:#fff;cursor:pointer}"
              ".btn{background:#0d6efd}"
              "input[type=text]{padding:8px 10px;font-size:16px;border:1px solid #ccc;border-radius:6px}"
              "table{border-collapse:collapse;width:100%;max-width:640px;margin-top:10px}"
              "th,td{border:1px solid #ddd;padding:8px;text-align:left}"
              "th{background:#f7f7f7}"
              "a{color:#0d6efd;text-decoration:none}"
              ".pill{display:inline-block;padding:4px 10px;border-radius:999px;background:#eee;margin-left:8px}"
              "</style>");

  res.println("<h1>Põrandapodi – reaktsiooniaeg</h1>");

  // Nime sisestamine
  res.println("<form class='row' action='/name' method='post'>"
              "<label>Katse tegija nimi:</label>"
              "<input type='text' name='name' placeholder='Sisesta nimi' value='");
  res.print(participantName);
  res.println("'> <button class='btn' type='submit'>Salvesta nimi</button></form>");

  // START nupp
  res.println("<form style='margin-top:10px' action='/start' method='post'>"
              "<button class='btn'>START</button></form>");

  // Olek
  res.print("<p style='margin-top:6px'>Olek: <b>");
  if (trialArmed && !trialDone) res.print("katse käib – lamp põleb, ootan anduri puudet");
  else if (trialDone)           res.print("katse lõpetatud – lamp kustu");
  else                          res.print("valmis uueks katseks – lamp kustu");
  res.println("</b></p>");

  // Viimane aeg
  char buf[24];
  snprintf(buf, sizeof(buf), "%.2f", lastReactionMs / 1000.0f);
  res.println("<h3>Viimane aeg</h3>");
  res.print("<p><b>"); res.print(buf); res.println(" s</b></p>");

  // Kõik ajad tabelina
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

  // Nupud: CSV ja reset
  res.println("<div class='row' style='margin-top:10px'>"
              "<a href='/csv' class='btn' style='background:#198754;display:inline-block;padding:10px 16px;color:#fff;border-radius:8px'>Laadi alla CSV</a>"
              "<form action='/reset' method='post' onsubmit='return confirm(\"Kustutan kõik ajad?\")'>"
              "<button class='btn' style='background:#dc3545'>Reset</button></form>"
              "<a href='/cfg'>⚙️ Anduri seaded</a>"
              "</div>");

  // Live anduri info
  int raw = readLDRRaw(), lvl = ldrLevel();
  res.println("<hr><h3>Andur</h3>");
  res.print("<p>RAW: <b id='raw'>"); res.print(raw);
  res.print("</b> | LVL: <b id='lvl'>"); res.print(lvl);
  res.print("</b> <span class='pill'>OFF lävi: "); res.print(LDR_OFF_THRESHOLD);
  res.print("</span> <span class='pill'>ON lävi: "); res.print(LDR_ON_THRESHOLD);
  res.println("</span></p>");

  // Live uuendus
  res.println("<script>"
              "async function tick(){try{const r=await fetch('/status');const j=await r.json();"
              "document.getElementById('raw').textContent=j.raw;"
              "document.getElementById('lvl').textContent=j.lvl;"
              "}catch(e){}} setInterval(tick,1000); tick();"
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
               ".row{display:flex;gap:12px;align-items:center}"
               ".pill{display:inline-block;padding:4px 10px;border-radius:999px;background:#eee;margin-left:8px}"
               "a{color:#0d6efd;text-decoration:none}</style>");

  res.println("<h2>⚙️ Valgusanduri seaded</h2>");
  
  // --- Vorm ---
  res.println("<form action='/set' method='post'>");
  res.print("<label>OFF lävi</label>");
  res.print("<input type='range' id='off' name='off' min='0' max='1023' value='");
  res.print(LDR_OFF_THRESHOLD);
  res.println("' oninput='offVal.value=this.value'>");
  res.print("<div class='row'>Väärtus: <output id='offVal'>");
  res.print(LDR_OFF_THRESHOLD);
  res.println("</output></div>");

  res.print("<label>ON lävi</label>");
  res.print("<input type='range' id='on' name='on' min='0' max='1023' value='");
  res.print(LDR_ON_THRESHOLD);
  res.println("' oninput='onVal.value=this.value'>");
  res.print("<div class='row'>Väärtus: <output id='onVal'>");
  res.print(LDR_ON_THRESHOLD);
  res.println("</output></div>");

  res.println("<div class='row'>"
               "<button type='submit'>Salvesta</button>"
               "<button type='button' id='resetDefaults' style='background:#6c757d'>Taasta vaikeseaded</button>"
               "</div>");
  res.println("</form>");

  // --- JS: vaikeseaded ja test POST ---
  res.println("<script>"
               "const DEFAULTS={off:520,on:620};"
               "document.getElementById('resetDefaults').addEventListener('click', async ()=>{"
                 "document.getElementById('off').value=DEFAULTS.off;"
                 "document.getElementById('on').value=DEFAULTS.on;"
                 "document.getElementById('offVal').value=DEFAULTS.off;"
                 "document.getElementById('onVal').value=DEFAULTS.on;"
                 "const d=new URLSearchParams();"
                 "d.append('off',DEFAULTS.off);"
                 "d.append('on',DEFAULTS.on);"
                 "await fetch('/set',{method:'POST',body:d,headers:{'Content-Type':'application/x-www-form-urlencoded'}});"
                 "alert('Vaikeseaded taastatud ja salvestatud!');"
               "});"
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

  int offv = LDR_OFF_THRESHOLD;
  int onv  = LDR_ON_THRESHOLD;

  readPostInt(req, "off", offv);
  readPostInt(req, "on", onv);

  // Piira vahemikku
  if (offv < 0) offv = 0; if (offv > 1023) offv = 1023;
  if (onv < 0)  onv = 0;  if (onv > 1023)  onv = 1023;

  LDR_OFF_THRESHOLD = offv;
  LDR_ON_THRESHOLD  = onv;

  Serial.printf("Uued väärtused: OFF=%d, ON=%d\n",
                LDR_OFF_THRESHOLD, LDR_ON_THRESHOLD);

  res.status(200);
  res.set("Content-Type", "text/plain; charset=utf-8");
  res.println("OK");
  res.end();
}



void doReset(Request &req, Response &res) {
  resetTimes();
  lastReactionMs = 0;
  trialArmed = false; trialDone=false;
  setLedColor(0,0,0);
  Serial.println("Kõik ajad kustutatud.");
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
