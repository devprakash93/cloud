/*
   ESP32 Relay Control — Firebase Cloud
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.h.example to include/secrets.h"
#endif

#define DATA_PIN   23
#define CLOCK_PIN  18
#define LATCH_PIN   5
#define OE_PIN      4

byte registerData[3] = {0, 0, 0};
bool relayState[4] = {false, false, false, false};

String idToken;
String refreshToken;
unsigned long tokenExpiresAt = 0;
String lastCommand = "";

WiFiClientSecure client;

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

void setOutput(int outputNumber, bool state) {
  int reg = outputNumber / 8;
  int bitPos = outputNumber % 8;

  if (state)
    bitSet(registerData[reg], bitPos);
  else
    bitClear(registerData[reg], bitPos);
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

String handleCommand(char cmd) {

  String response = "";

  if (cmd == '1') {
    setOutput(0, LOW);
    relayState[0] = true;
    response = "Relay 1 ON";
  }

  else if (cmd == '2') {
    setOutput(0, HIGH);
    relayState[0] = false;
    response = "Relay 1 OFF";
  }

  else if (cmd == '3') {
    setOutput(8, LOW);
    relayState[1] = true;
    response = "Relay 2 ON";
  }

  else if (cmd == '4') {
    setOutput(8, HIGH);
    relayState[1] = false;
    response = "Relay 2 OFF";
  }

  else if (cmd == '5') {
    setOutput(16, LOW);
    relayState[2] = true;
    response = "Relay 3 ON";
  }

  else if (cmd == '6') {
    setOutput(16, HIGH);
    relayState[2] = false;
    response = "Relay 3 OFF";
  }

  else if (cmd == '7') {
    setOutput(17, LOW);
    relayState[3] = true;
    response = "Relay 4 ON";
  }

  else if (cmd == '8') {
    setOutput(17, HIGH);
    relayState[3] = false;
    response = "Relay 4 OFF";
  }

  else if (cmd == '9') {

    setOutput(0, LOW);
    setOutput(8, LOW);
    setOutput(16, LOW);
    setOutput(17, LOW);

    relayState[0] = true;
    relayState[1] = true;
    relayState[2] = true;
    relayState[3] = true;

    response = "ALL RELAYS ON";
  }

  else if (cmd == '0') {

    setOutput(0, HIGH);
    setOutput(8, HIGH);
    setOutput(16, HIGH);
    setOutput(17, HIGH);

    relayState[0] = false;
    relayState[1] = false;
    relayState[2] = false;
    relayState[3] = false;

    response = "ALL RELAYS OFF";
  }

  if (response.length() > 0) {
    updateRegisters();
  }

  return response;
}

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

  String url =
    "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" +
    String(FIREBASE_API_KEY);

  StaticJsonDocument<256> req;

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
    Serial.println(resp);
    return false;
  }

  StaticJsonDocument<768> doc;

  DeserializationError err = deserializeJson(doc, resp);

  if (err) {
    return false;
  }

  idToken = doc["idToken"].as<String>();

  return true;
}

bool firebasePut(const char* child, const String& value) {

  if (idToken == "") {
    if (!firebaseSignIn()) {
      return false;
    }
  }

  String url =
    devicePath(child) +
    ".json?auth=" +
    idToken;

  String resp;
  int code = 0;

  httpRequest("PUT", url, value, resp, code);

  return code == 200;
}

String firebaseGet(const char* child) {

  if (idToken == "") {
    if (!firebaseSignIn()) {
      return "";
    }
  }

  String url =
    devicePath(child) +
    ".json?auth=" +
    idToken;

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

void connectWiFi() {

  WiFi.mode(WIFI_STA);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected");
  Serial.println(WiFi.localIP());
}

void setup() {

  Serial.begin(115200);

  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(OE_PIN, OUTPUT);

  digitalWrite(OE_PIN, LOW);

  clearAll();

  client.setInsecure();

  connectWiFi();

  if (firebaseSignIn()) {

    Serial.println("Firebase Connected");

    publishState("ESP32 ONLINE");

    firebasePut("command", "\"\"");
  }

  else {
    Serial.println("Firebase Login Failed");
  }
}

void loop() {

  static unsigned long lastPoll = 0;
  static unsigned long lastHeartbeat = 0;
  static unsigned long lastWiFiCheck = 0;

  // 1. WiFi Connection Watchdog — check every 10 seconds
  if (millis() - lastWiFiCheck > 10000) {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection lost! Reconnecting...");
      WiFi.disconnect();
      connectWiFi();
      firebaseSignIn();
    }
  }

  // 2. Poll Firebase commands every 500ms
  if (millis() - lastPoll > 500) {

    lastPoll = millis();

    String cmd = firebaseGet("command");

    if (cmd.length() == 1 && cmd != lastCommand) {

      lastCommand = cmd;

      String resp = handleCommand(cmd[0]);

      if (resp.length() > 0) {

        Serial.println(resp);

        publishState(resp);
      }

      firebasePut("command", "\"\"");

      lastCommand = "";
    }
  }

  // 3. Heartbeat — update Firebase server timestamp every 15 seconds
  if (millis() - lastHeartbeat > 15000) {
    lastHeartbeat = millis();
    firebasePut("lastSeen", "{\".sv\": \"timestamp\"}");
  }

  delay(10);
}