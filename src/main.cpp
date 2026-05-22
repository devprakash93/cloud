/*
   ESP32 Relay Control — Hybrid Local (WebSocket) & Cloud (Firebase)
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// Fallbacks for Wi-Fi credentials
#ifndef WIFI_SSID
#define WIFI_SSID     "WindowWatt"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "Windowatt"
#endif

// Fallbacks for Firebase config
#ifndef FIREBASE_API_KEY
#define FIREBASE_API_KEY "AIzaSyBM_mziuGxRkAvKNI-vhewBJ8puDUYwoM8"
#endif
#ifndef FIREBASE_DATABASE_URL
#define FIREBASE_DATABASE_URL "https://iot-billing-system-8f450-default-rtdb.asia-southeast1.firebasedatabase.app"
#endif
#ifndef FIREBASE_USER_EMAIL
#define FIREBASE_USER_EMAIL    "windowwatt@gmail.com"
#endif
#ifndef FIREBASE_USER_PASSWORD
#define FIREBASE_USER_PASSWORD "Windowwatt@25"
#endif
#ifndef DEVICE_ID
#define DEVICE_ID "esp32relay"
#endif

//  SHIFT REGISTER PINS
#define DATA_PIN   23
#define CLOCK_PIN  18
#define LATCH_PIN   5
#define OE_PIN      4

byte registerData[3] = {0, 0, 0};

// Server & WebSocket
AsyncWebServer server(80);
AsyncWebSocket  ws("/ws");

// ===== Relay States =====
bool relayState[4] = {false, false, false, false};

// Firebase Client Variables
String idToken = "";
String refreshToken = "";
unsigned long tokenExpiresAt = 0;
String lastCommand = "";

WiFiClientSecure client;

//   EMBEDDED HTML PAGE
const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1.0"/>
  <title>ESP32 Relay Control</title>
  <link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Exo+2:wght@300;600;800&display=swap" rel="stylesheet"/>
  <style>
    *,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
    :root{
      --bg:#0a0c10;--panel:#0f1318;--border:#1e2530;
      --accent:#00e5ff;--accent2:#ff6b35;
      --on-glow:#00ff9d;--off-dim:#1a2030;
      --text:#c8d8e8;--text-dim:#4a5a70;
      --font-mono:'Share Tech Mono',monospace;
      --font-main:'Exo 2',sans-serif;
    }
    body{
      background:var(--bg);color:var(--text);
      font-family:var(--font-main);min-height:100vh;
      display:flex;flex-direction:column;align-items:center;
      padding:20px 16px 40px;overflow-x:hidden;
    }
    body::before{
      content:'';position:fixed;inset:0;
      background-image:
        linear-gradient(rgba(0,229,255,.03) 1px,transparent 1px),
        linear-gradient(90deg,rgba(0,229,255,.03) 1px,transparent 1px);
      background-size:40px 40px;pointer-events:none;z-index:0;
    }

    /* ---- HEADER ---- */
    header{width:100%;max-width:680px;text-align:center;position:relative;z-index:1;margin-bottom:32px;}
    .logo-bar{display:flex;align-items:center;justify-content:center;gap:12px;margin-bottom:6px;}
    .logo-icon{
      width:36px;height:36px;border:2px solid var(--accent);border-radius:6px;
      display:grid;place-items:center;box-shadow:0 0 14px var(--accent);
    }
    .logo-icon svg{width:20px;height:20px;fill:var(--accent);}
    h1{font-size:clamp(1.4rem,4vw,2rem);font-weight:800;letter-spacing:.08em;text-transform:uppercase;color:#fff;}
    h1 span{color:var(--accent);}
    .subtitle{font-family:var(--font-mono);font-size:.72rem;color:var(--text-dim);letter-spacing:.12em;margin-top:4px;}
    
    /* connection row */
    .conn-row{display:flex;align-items:center;justify-content:center;gap:10px;margin-top:14px;flex-wrap:wrap;}
    #conn-status{
      font-family:var(--font-mono);font-size:.78rem;padding:4px 14px;border-radius:20px;
      background:rgba(255,107,53,.12);border:1px solid rgba(255,107,53,.4);
      color:var(--accent2);transition:all .4s;
    }
    #conn-status.connected{background:rgba(0,255,157,.08);border-color:rgba(0,255,157,.4);color:var(--on-glow);}

    /* ---- RELAY GRID ---- */
    .relay-grid{
      display:grid;grid-template-columns:repeat(2,1fr);gap:16px;
      width:100%;max-width:680px;position:relative;z-index:1;
    }
    .relay-card{
      background:var(--panel);border:1px solid var(--border);border-radius:14px;
      padding:22px 20px 18px;display:flex;flex-direction:column;gap:14px;
      position:relative;overflow:hidden;transition:border-color .3s,box-shadow .3s;
    }
    .relay-card.on{
      border-color:rgba(0,255,157,.35);
      box-shadow:0 0 24px rgba(0,255,157,.08),inset 0 0 30px rgba(0,255,157,.03);
    }
    .relay-card::before{
      content:'';position:absolute;top:0;right:0;width:60px;height:60px;
      background:conic-gradient(from 225deg,transparent 0%,var(--accent) 0% 12%,transparent 12%);
      opacity:.18;
    }
    .relay-card.on::before{
      background:conic-gradient(from 225deg,transparent 0%,var(--on-glow) 0% 12%,transparent 12%);
      opacity:.3;
    }
    .card-header{display:flex;align-items:flex-start;justify-content:space-between;}
    .relay-label{font-size:.68rem;font-family:var(--font-mono);letter-spacing:.14em;color:var(--text-dim);text-transform:uppercase;}
    .relay-name{font-size:1.2rem;font-weight:600;color:#fff;margin-top:2px;}
    .led{
      width:14px;height:14px;border-radius:50%;background:var(--off-dim);
      border:1px solid #2a3545;margin-top:3px;flex-shrink:0;transition:background .3s,box-shadow .3s;
    }
    .relay-card.on .led{
      background:var(--on-glow);border-color:var(--on-glow);
      box-shadow:0 0 8px var(--on-glow),0 0 16px rgba(0,255,157,.4);
    }
    .status-badge{
      font-family:var(--font-mono);font-size:.7rem;padding:3px 10px;border-radius:20px;
      background:rgba(26,32,48,.8);border:1px solid var(--border);
      color:var(--text-dim);width:fit-content;transition:all .3s;
    }
    .relay-card.on .status-badge{background:rgba(0,255,157,.08);border-color:rgba(0,255,157,.3);color:var(--on-glow);}
    .toggle-btn{
      width:100%;padding:11px;border-radius:9px;border:none;
      font-family:var(--font-main);font-size:.88rem;font-weight:600;
      letter-spacing:.06em;text-transform:uppercase;cursor:pointer;
      transition:all .25s;position:relative;overflow:hidden;
    }
    .toggle-btn::after{
      content:'';position:absolute;inset:0;
      background:linear-gradient(180deg,rgba(255,255,255,.07) 0%,transparent 100%);
      pointer-events:none;
    }
    .relay-card:not(.on) .toggle-btn{
      background:linear-gradient(135deg,#1a4a3a,#0d3028);
      color:var(--on-glow);border:1px solid rgba(0,255,157,.25);
    }
    .relay-card:not(.on) .toggle-btn:hover{
      background:linear-gradient(135deg,#1f5a47,#113a32);
      box-shadow:0 0 16px rgba(0,255,157,.2);
    }
    .relay-card.on .toggle-btn{
      background:linear-gradient(135deg,#4a1a10,#301008);
      color:#ff6b6b;border:1px solid rgba(255,107,53,.25);
    }
    .relay-card.on .toggle-btn:hover{
      background:linear-gradient(135deg,#5a2018,#3a1410);
      box-shadow:0 0 16px rgba(255,80,50,.2);
    }

    /* ---- MASTER PANEL ---- */
    .master-panel{
      width:100%;max-width:680px;margin-top:16px;
      background:var(--panel);border:1px solid var(--border);
      border-radius:14px;padding:18px 20px;position:relative;z-index:1;
    }
    .master-title{
      font-family:var(--font-mono);font-size:.68rem;letter-spacing:.14em;
      color:var(--text-dim);text-transform:uppercase;margin-bottom:14px;
    }
    .master-btns{display:grid;grid-template-columns:1fr 1fr;gap:12px;}
    .master-btn{
      padding:13px;border-radius:10px;border:none;
      font-family:var(--font-main);font-size:.92rem;font-weight:800;
      letter-spacing:.08em;text-transform:uppercase;cursor:pointer;transition:all .25s;
    }
    .all-on{background:linear-gradient(135deg,#004d35,#006644);color:var(--on-glow);border:1px solid rgba(0,255,157,.3);}
    .all-on:hover{background:linear-gradient(135deg,#005c3f,#007a50);box-shadow:0 4px 20px rgba(0,255,157,.25);}
    .all-off{background:linear-gradient(135deg,#4a1510,#5a1a12);color:#ff7055;border:1px solid rgba(255,80,50,.3);}
    .all-off:hover{background:linear-gradient(135deg,#5a1c15,#6a2018);box-shadow:0 4px 20px rgba(255,80,50,.2);}

    /* ---- LOG CONSOLE ---- */
    .log-panel{
      width:100%;max-width:680px;margin-top:16px;
      background:#080b0f;border:1px solid var(--border);
      border-radius:14px;padding:14px 16px;position:relative;z-index:1;
    }
    .log-title{
      font-family:var(--font-mono);font-size:.65rem;letter-spacing:.14em;
      color:var(--text-dim);text-transform:uppercase;margin-bottom:10px;
      display:flex;align-items:center;gap:8px;
    }
    .log-title::before{
      content:'';display:inline-block;width:6px;height:6px;
      border-radius:50%;background:var(--accent);animation:blink 1.4s infinite;
    }
    @keyframes blink{0%,100%{opacity:1}50%{opacity:.2}}
    #log-output{
      font-family:var(--font-mono);font-size:.75rem;color:#6a8898;
      height:90px;overflow-y:auto;display:flex;flex-direction:column-reverse;gap:3px;
    }
    #log-output .log-line{transition:color .3s;}
    #log-output .log-line.new{color:var(--accent);}

    /* ---- IP INFO BOX ---- */
    .ip-box{
      font-family:var(--font-mono);font-size:.75rem;
      background:rgba(0,229,255,.05);border:1px solid rgba(0,229,255,.2);
      border-radius:8px;padding:8px 14px;color:var(--accent);
      margin-top:10px;text-align:center;letter-spacing:.06em;
    }

    @media(max-width:420px){
      .relay-grid{grid-template-columns:1fr 1fr;gap:10px;}
      .relay-card{padding:16px 14px 14px;}
    }
  </style>
</head>
<body>

<header>
  <div class="logo-bar">
    <div class="logo-icon">
      <svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
        <path d="M13 3L4 14h7l-1 7 9-11h-7l1-7z"/>
      </svg>
    </div>
    <h1>ESP32 <span>Relay</span> Control</h1>
  </div>
  <div class="subtitle">3 &times; 74HC595 &nbsp;|&nbsp; 4-Channel Industrial Panel</div>
  <div class="conn-row">
    <span id="conn-status">&#9679; CONNECTING...</span>
  </div>
</header>

<div class="relay-grid">
  <div class="relay-card" id="card-1">
    <div class="card-header">
      <div>
        <div class="relay-label">Channel 01 &middot; IC1 Q0</div>
        <div class="relay-name">Relay 1</div>
      </div>
      <div class="led"></div>
    </div>
    <div class="status-badge" id="badge-1">&#9679; INACTIVE</div>
    <button class="toggle-btn" onclick="toggle(1)">Turn ON</button>
  </div>

  <div class="relay-card" id="card-2">
    <div class="card-header">
      <div>
        <div class="relay-label">Channel 02 &middot; IC2 Q0</div>
        <div class="relay-name">Relay 2</div>
      </div>
      <div class="led"></div>
    </div>
    <div class="status-badge" id="badge-2">&#9679; INACTIVE</div>
    <button class="toggle-btn" onclick="toggle(2)">Turn ON</button>
  </div>

  <div class="relay-card" id="card-3">
    <div class="card-header">
      <div>
        <div class="relay-label">Channel 03 &middot; IC3 Q0</div>
        <div class="relay-name">Relay 3</div>
      </div>
      <div class="led"></div>
    </div>
    <div class="status-badge" id="badge-3">&#9679; INACTIVE</div>
    <button class="toggle-btn" onclick="toggle(3)">Turn ON</button>
  </div>

  <div class="relay-card" id="card-4">
    <div class="card-header">
      <div>
        <div class="relay-label">Channel 04 &middot; IC3 Q1</div>
        <div class="relay-name">Relay 4</div>
      </div>
      <div class="led"></div>
    </div>
    <div class="status-badge" id="badge-4">&#9679; INACTIVE</div>
    <button class="toggle-btn" onclick="toggle(4)">Turn ON</button>
  </div>
</div>

<div class="master-panel">
  <div class="master-title">// Master Controls</div>
  <div class="master-btns">
    <button class="master-btn all-on"  onclick="sendCmd('9')">&#9889; All ON</button>
    <button class="master-btn all-off" onclick="sendCmd('0')">&#9632; All OFF</button>
  </div>
</div>

<div class="log-panel">
  <div class="log-title">System Log</div>
  <div id="log-output"></div>
</div>

<script>
  var ws;
  var states = [false, false, false, false];
  var reconnectTimer;

  function initWS() {
    var host = window.location.hostname;
    ws = new WebSocket('ws://' + host + '/ws');

    ws.onopen = function() {
      setStatus(true);
      addLog('Connected to ESP32');
      clearTimeout(reconnectTimer);
    };

    ws.onmessage = function(evt) {
      var msg = evt.data;
      if (msg.indexOf('STATE:') === 0) {
        var bits = msg.substring(6);
        for (var i = 0; i < 4; i++) {
          states[i] = (bits[i] === '1');
          updateCard(i + 1, states[i]);
        }
      } else if (msg.indexOf('LOG:') === 0) {
        addLog(msg.substring(4));
      }
    };

    ws.onclose = function() {
      setStatus(false);
      addLog('Disconnected - retrying in 3s...');
      reconnectTimer = setTimeout(initWS, 3000);
    };

    ws.onerror = function() {
      addLog('Connection error');
    };
  }

  function sendCmd(cmd) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(cmd);
    } else {
      addLog('Not connected!');
    }
  }

  function toggle(n) {
    var isOn = states[n - 1];
    var cmd = isOn ? String(n * 2) : String((n * 2) - 1);
    sendCmd(cmd);
  }

  function updateCard(n, isOn) {
    var card  = document.getElementById('card-' + n);
    var badge = document.getElementById('badge-' + n);
    var btn   = card.querySelector('.toggle-btn');
    if (isOn) {
      card.classList.add('on');
      badge.innerHTML  = '&#9679; ACTIVE';
      btn.textContent  = 'Turn OFF';
    } else {
      card.classList.remove('on');
      badge.innerHTML  = '&#9679; INACTIVE';
      btn.textContent  = 'Turn ON';
    }
  }

  function setStatus(connected) {
    var el = document.getElementById('conn-status');
    if (connected) {
      el.innerHTML = '&#9679; ONLINE';
      el.classList.add('connected');
    } else {
      el.innerHTML = '&#9679; OFFLINE';
      el.classList.remove('connected');
    }
  }

  function addLog(msg) {
    var el  = document.getElementById('log-output');
    var now = new Date().toLocaleTimeString('en-GB', {hour12:false});
    var div = document.createElement('div');
    div.className   = 'log-line new';
    div.textContent = '[' + now + ']  ' + msg;
    el.prepend(div);
    setTimeout(function(){ div.classList.remove('new'); }, 1200);
    while (el.children.length > 30) el.removeChild(el.lastChild);
  }

  addLog('Panel loaded - connecting...');
  initWS();
</script>
</body>
</html>
)rawhtml";

// =============================================
//   PATH GENERATOR
// =============================================

String devicePath(const char* child) {
  String p = String(FIREBASE_DATABASE_URL);

  if (p.endsWith("/")) {
    p.remove(p.length() - 1);
  }

  p += "/devices/";
  p += DEVICE_ID;

  if (child && child[0]) {
    p += "/";
    p += child;
  }

  return p;
}

String stateString() {
  String s = "";
  for (int i = 0; i < 4; i++) {
    s += relayState[i] ? '1' : '0';
  }
  return s;
}

// =============================================
//   SHIFT REGISTER FUNCTIONS (unchanged)
// =============================================

void setOutput(int outputNumber, bool state) {
  int reg    = outputNumber / 8;
  int bitPos = outputNumber % 8;
  if (state) { bitSet(registerData[reg],   bitPos); }
  else        { bitClear(registerData[reg], bitPos); }
}

void updateRegisters() {
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, registerData[2]);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, registerData[1]);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, registerData[0]);
  digitalWrite(LATCH_PIN, HIGH);
}

void clearAll() {
  registerData[0] = 0;
  registerData[1] = 0;
  registerData[2] = 0;
  updateRegisters();
}

// =============================================
//   COMMAND HANDLER
// =============================================

String handleCommand(char cmd) {
  String response = "";

  if      (cmd == '1') { setOutput(0,  LOW);  relayState[0] = true;  response = "Relay 1 ON";      }
  else if (cmd == '2') { setOutput(0,  HIGH); relayState[0] = false; response = "Relay 1 OFF";     }
  else if (cmd == '3') { setOutput(8,  LOW);  relayState[1] = true;  response = "Relay 2 ON";      }
  else if (cmd == '4') { setOutput(8,  HIGH); relayState[1] = false; response = "Relay 2 OFF";     }
  else if (cmd == '5') { setOutput(16, LOW);  relayState[2] = true;  response = "Relay 3 ON";      }
  else if (cmd == '6') { setOutput(16, HIGH); relayState[2] = false; response = "Relay 3 OFF";     }
  else if (cmd == '7') { setOutput(17, LOW);  relayState[3] = true;  response = "Relay 4 ON";      }
  else if (cmd == '8') { setOutput(17, HIGH); relayState[3] = false; response = "Relay 4 OFF";     }
  else if (cmd == '0') {
    setOutput(0, HIGH); setOutput(8, HIGH); setOutput(16, HIGH); setOutput(17, HIGH);
    relayState[0] = relayState[1] = relayState[2] = relayState[3] = false;
    response = "ALL RELAYS OFF";
  }
  else if (cmd == '9') {
    setOutput(0, LOW);  setOutput(8, LOW);  setOutput(16, LOW);  setOutput(17, LOW);
    relayState[0] = relayState[1] = relayState[2] = relayState[3] = true;
    response = "ALL RELAYS ON";
  }

  if (response.length() > 0) {
    updateRegisters();

    // Broadcast new relay states to ALL connected local browsers via WebSocket
    String stateMsg = "STATE:";
    stateMsg += relayState[0] ? "1" : "0";
    stateMsg += relayState[1] ? "1" : "0";
    stateMsg += relayState[2] ? "1" : "0";
    stateMsg += relayState[3] ? "1" : "0";
    ws.textAll(stateMsg);
    ws.textAll("LOG:" + response);
  }
  return response;
}

// =============================================
//   FIREBASE API REST FUNCTIONS
// =============================================

bool httpRequest(
  const String& method,
  const String& url,
  const String& body,
  String& out,
  int& httpCode
) {
  HTTPClient http;

  if (!http.begin(client, url)) {
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  if (method == "GET") {
    httpCode = http.GET();
  }
  else if (method == "PUT") {
    httpCode = http.PUT(body);
  }
  else if (method == "POST") {
    httpCode = http.POST(body);
  }
  else {
    http.end();
    return false;
  }

  out = http.getString();
  http.end();
  return true;
}

bool firebaseSignIn() {
  if (String(FIREBASE_API_KEY) == "") {
    Serial.println("Firebase API key missing. Skipping Auth.");
    return false;
  }

  String url =
    "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" +
    String(FIREBASE_API_KEY);

  JsonDocument req;
  req["email"] = FIREBASE_USER_EMAIL;
  req["password"] = FIREBASE_USER_PASSWORD;
  req["returnSecureToken"] = true;

  String body;
  serializeJson(req, body);

  String resp;
  int code = 0;

  if (!httpRequest("POST", url, body, resp, code)) {
    return false;
  }

  if (code != 200) {
    Serial.println("Firebase Auth Error: " + resp);
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    return false;
  }

  idToken = doc["idToken"].as<String>();
  return true;
}

bool firebasePut(const char* child, const String& value) {
  if (String(FIREBASE_DATABASE_URL) == "") return false;

  if (idToken == "") {
    if (!firebaseSignIn()) {
      return false;
    }
  }

  String url = devicePath(child) + ".json?auth=" + idToken;
  String resp;
  int code = 0;

  httpRequest("PUT", url, value, resp, code);
  return code == 200;
}

String firebaseGet(const char* child) {
  if (String(FIREBASE_DATABASE_URL) == "") return "";

  if (idToken == "") {
    if (!firebaseSignIn()) {
      return "";
    }
  }

  String url = devicePath(child) + ".json?auth=" + idToken;
  String resp;
  int code = 0;

  httpRequest("GET", url, "", resp, code);

  if (code != 200) {
    return "";
  }

  if (resp == "null") {
    return "";
  }

  if (resp.startsWith("\"") && resp.endsWith("\"")) {
    resp = resp.substring(1, resp.length() - 1);
  }

  return resp;
}

void publishState(String logMsg) {
  firebasePut("state", "\"" + stateString() + "\"");
  firebasePut("online", "true");
  firebasePut("lastSeen", "{\".sv\": \"timestamp\"}");

  if (logMsg.length() > 0) {
    firebasePut("log", "\"" + logMsg + "\"");
  }
}

// =============================================
//   WEBSOCKET EVENT HANDLER
// =============================================

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {

  if (type == WS_EVT_CONNECT) {
    Serial.printf("WS Client #%u connected\n", client->id());
    // Send current state to newly joined browser
    String stateMsg = "STATE:";
    stateMsg += relayState[0] ? "1" : "0";
    stateMsg += relayState[1] ? "1" : "0";
    stateMsg += relayState[2] ? "1" : "0";
    stateMsg += relayState[3] ? "1" : "0";
    client->text(stateMsg);
  }
  else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WS Client #%u disconnected\n", client->id());
  }
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len &&
        info->opcode == WS_TEXT) {
      char cmd = (char)data[0];
      Serial.print("Local WebSocket Command: "); Serial.println(cmd);
      String resp = handleCommand(cmd);
      if (resp.length() > 0) {
        // Sync local toggle action state and log to Firebase
        publishState(resp);
      }
    }
  }
}

// =============================================
//   CONNECTION UTILITY
// =============================================

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi to ");
  Serial.println(WIFI_SSID);

  unsigned long startAttempt = millis();
  // Try connecting for up to 15 seconds
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi Connected!");
    Serial.print("Local IP Address: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connection attempt timed out. Will retry in loop watchdog.");
  }
}

// =============================================
//   SETUP
// =============================================

void setup() {
  Serial.begin(115200);

  // Shift register pins
  pinMode(DATA_PIN,  OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(OE_PIN,    OUTPUT);
  digitalWrite(OE_PIN, LOW);
  clearAll();

  // Disable certificate validation for Firebase secure connection
  client.setInsecure();

  // Connect to Wi-Fi network
  connectWiFi();

  // Setup local web socket handlers
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Serve embedded webpage at root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });

  // Start direct local WebServer
  server.begin();
  Serial.println("HTTP Server started on Port 80");

  // Sign in and set initial status on Firebase Cloud
  if (WiFi.status() == WL_CONNECTED) {
    if (firebaseSignIn()) {
      Serial.println("Firebase Connected!");
      publishState("ESP32 ONLINE (HYBRID)");
      firebasePut("command", "\"\"");
    } else {
      Serial.println("Firebase login failed on boot.");
    }
  }
}

// =============================================
//   LOOP
// =============================================

void loop() {
  // Free up WebSocket memory
  ws.cleanupClients();

  static unsigned long lastPoll = 0;
  static unsigned long lastHeartbeat = 0;
  static unsigned long lastWiFiCheck = 0;
  unsigned long now = millis();

  // 1. Wi-Fi Watchdog — check and reconnect every 10 seconds
  if (now - lastWiFiCheck > 10000) {
    lastWiFiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection lost! Reconnecting...");
      WiFi.disconnect();
      connectWiFi();
      if (WiFi.status() == WL_CONNECTED) {
        firebaseSignIn();
      }
    }
  }

  // 2. Poll Firebase cloud commands every 500ms if connected
  if (WiFi.status() == WL_CONNECTED && now - lastPoll > 500) {
    lastPoll = now;

    String cmd = firebaseGet("command");

    // Execute if a command of length 1 is found and is different from the last one
    if (cmd.length() == 1 && cmd != lastCommand) {
      lastCommand = cmd;
      Serial.print("Cloud Firebase Command: "); Serial.println(cmd);
      String resp = handleCommand(cmd[0]);

      if (resp.length() > 0) {
        Serial.println("Executed: " + resp);
        publishState(resp);
      }

      // Reset the command node in Firebase back to empty
      firebasePut("command", "\"\"");
      lastCommand = "";
    }
  }

  // 3. Heartbeat — update Firebase lastSeen timestamp every 15 seconds
  if (WiFi.status() == WL_CONNECTED && now - lastHeartbeat > 15000) {
    lastHeartbeat = now;
    firebasePut("lastSeen", "{\".sv\": \"timestamp\"}");
  }

  delay(10);
}