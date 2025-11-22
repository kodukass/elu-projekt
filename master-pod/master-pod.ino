#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <aWOT.h>
#include <ESP8266HTTPClient.h>

Application app;
WiFiServer server(80);

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
  {"192.168.4.6", "Lamp 5"},
};

int activeLamp = -1;

// ---------- Helpers ----------
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

// Send POST/GET to a lamp
bool sendToLamp(String ip, String path, String data=""){
  HTTPClient http;
  String url = "http://" + ip + path;
  if(data==""){
    http.begin(url);
    int code = http.GET();
    http.end();
    return code==200;
  } else {
    http.begin(url);
    http.addHeader("Content-Type","application/x-www-form-urlencoded");
    int code = http.POST(data);
    http.end();
    return code==200;
  }
}

// Stop all lamps
void stopAllLamps(){
  for(int i=0;i<5;i++){
    sendToLamp(lamps[i].ip,"/stop");
  }
}

// Pick random lamp
int pickRandomLamp(){
  return random(0,5);
}

// Fetch results from all lamps
void fetchResults(){
  HTTPClient http;
  for(int i=0;i<5;i++){
    String url = "http://" + lamps[i].ip + "/status";
    http.begin(url);
    int code = http.GET();
    if(code==200){
      String payload = http.getString();
      int nIdx = payload.indexOf("\"name\":\"");
      int tIdx = payload.indexOf("\"lastReactionMs\":");
      if(nIdx>=0 && tIdx>=0){
        int nEnd = payload.indexOf("\"", nIdx+8);
        String name = payload.substring(nIdx+8,nEnd);
        int tEnd = payload.indexOf(",", tIdx+17);
        if(tEnd==-1) tEnd = payload.indexOf("}", tIdx+17);
        unsigned long timeMs = payload.substring(tIdx+17,tEnd).toInt();
        if(timeMs>0) addResult(name,timeMs);
      }
    }
    http.end();
  }
  sortResults();
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
               "input[type=text]{padding:8px 10px;font-size:16px;border:1px solid #ccc;border-radius:6px}"
               "table{border-collapse:collapse;width:100%;max-width:640px;margin-top:10px}"
               "th,td{border:1px solid #ddd;padding:8px;text-align:left}"
               "th{background:#f7f7f7}"
               "a{color:#0d6efd;text-decoration:none}"
               ".pill{display:inline-block;padding:4px 10px;border-radius:999px;background:#eee;margin-left:8px}"
             "</style>");
  
  // --- Page header ---
  res.println("<h1>Põrandapodi – reaktsiooniaeg</h1>");
  
  // --- START button ---
  res.println("<form style='margin-top:10px' action='/start' method='post'>"
               "<button class='btn'>START</button></form>");
  
  // --- Leaderboard ---
  fetchResults(); // update from lamps
  res.println("<h3>Edetabel</h3>");
  if(resultCount==0) res.println("<p>Edetabel on tühi.</p>");
  else{
    res.println("<table><tr><th>#</th><th>Nimi</th><th>Aeg (ms)</th></tr>");
    for(int i=0;i<resultCount;i++){
      res.printf("<tr><td>%d</td><td>%s</td><td>%lu</td></tr>",
                 i+1, results[i].name.c_str(), results[i].timeMs);
    }
    res.println("</table>");
  }

  // --- Color presets ---
  res.println("<h3>Vali LED värvikomplekt</h3>");
  res.println("<div class='row' style='gap:10px'>"
               "<button class='btn' onclick=\"setColor('#ff9a38','#29bfff','#ff84df','#c2d662','#f5e529')\">Regular</button>"
               "<button class='btn' onclick=\"setColor('#642fc1','#d0be00','#ff206e','#004a59','#ffffff')\">Colorblind</button>"
               "</div>");

  // --- JS ---
  res.println("<script>"
               "async function setColor(...colors){"
                 "for(let i=0;i<5;i++){"
                   "const data = new URLSearchParams(); data.append('color',colors[i]);"
                   "await fetch('/setcolor',{method:'POST',body:data});"
                 "}"
                 "alert('Värvikomplekt muudetud!');"
               "}"
             "</script>");

  res.println("</html>");
}

// ---------- Handlers ----------
void startTrial(Request &req, Response &res){
  stopAllLamps();
  activeLamp = pickRandomLamp();
  sendToLamp(lamps[activeLamp].ip,"/start");
  Serial.printf("Trial started on lamp %d\n", activeLamp);
  res.status(303); res.set("Location","/"); res.println("OK");
}

void setColorHandler(Request &req, Response &res){
  char colorHex[8];
  if(req.form("color", sizeof(colorHex), colorHex, sizeof(colorHex))){
    String color = String(colorHex);
    for(int i=0;i<5;i++){
      String data = "color=" + color;
      sendToLamp(lamps[i].ip,"/setcolor",data);
    }
  }
  res.status(303); res.set("Location","/"); res.println("OK");
}

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
             "</style>");
  
  res.println("<h1>Lampide seaded</h1>");

  for(int i=0;i<5;i++){
    res.printf("<h2>%s (%s)</h2>", lamps[i].name.c_str(), lamps[i].ip.c_str());

    res.println("<div class='row'>"
                 "<label>OFF threshold:</label>"
                 "<input type='text' id='off"+String(i)+"' value='"+String(lamps[i].LDR_OFF_THRESHOLD)+"'>"
                 "<label>ON threshold:</label>"
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
void setThresholdHandler(Request &req, Response &res){
  char lampKey[] = "lamp"; 
  int i = atoi(req.query(lampKey));

  if(i<0 || i>=5) { res.println("Invalid lamp"); return; }
  int offVal = lamps[i].LDR_OFF_THRESHOLD;
  int onVal  = lamps[i].LDR_ON_THRESHOLD;
  readPostInt(req,"off",offVal);
  readPostInt(req,"on",onVal);
  lamps[i].LDR_OFF_THRESHOLD = offVal;
  lamps[i].LDR_ON_THRESHOLD  = onVal;
  String data = "off="+String(offVal)+"&on="+String(onVal);
  sendToLamp(lamps[i].ip,"/set",data);
  redirectHome(res);
}

void startLampHandler(Request &req, Response &res){
  int i = atoi(req.query("lamp"));
  if(i>=0 && i<5) sendToLamp(lamps[i].ip,"/start");
  redirectHome(res);
}

void stopLampHandler(Request &req, Response &res){
  int i = atoi(req.query("lamp"));
  if(i>=0 && i<5) sendToLamp(lamps[i].ip,"/stop");
  redirectHome(res);
}

void setColorLampHandler(Request &req, Response &res){
  int i = atoi(req.query("lamp"));
  if(i<0 || i>=5) { redirectHome(res); return; }
  char key[] = "color";  // mutable char array
  if(req.form("color",sizeof(colorHex),colorHex,sizeof(colorHex))){
    sendToLamp(lamps[i].ip,"/setcolor","color="+String(colorHex));
  }
  redirectHome(res);
}


// ---------- Setup / Loop ----------
void setup(){
  Serial.begin(115200);
  delay(500);

  WiFi.softAP("PÕRANDAPOD_MASTER","salajane123");
  Serial.print("Master AP IP: "); Serial.println(WiFi.softAPIP());

  randomSeed(analogRead(A0));

  app.get("/", naitaEsilehte);
  app.post("/start", startTrial);
  app.post("/setcolor", setColorHandler);
  app.get("/cfg", naitaSeadeid);
  app.post("/setthreshold", setThresholdHandler);
  app.post("/setcolor", setColorLampHandler);
  app.post("/startlamp", startLampHandler);
  app.post("/stoplamp", stopLampHandler);


  server.begin();
  Serial.println("Master webserver listening...");
}

void loop(){
  WiFiClient client = server.available();
  if(client){ app.process(&client); client.stop(); }
}
