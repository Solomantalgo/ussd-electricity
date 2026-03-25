#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#define BULB_PIN        5
#define LED_BUILTIN_PIN 2
#define TOKEN_LENGTH    16
#define MAX_UNITS       100

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

WebServer   server(80);
Preferences prefs;

String  activeToken    = "";
int     remainingUnits = 0;
bool    bulbState      = false;
bool    wifiConnected  = false;

// ── Forward declarations ──────────────────────────
bool validateToken(String token);
void applyToken(String token);
void setBulb(bool state);
void printStatus();
void handleSerialInput();
void setupWiFi();
void setupServer();

// ── setup() ──────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BULB_PIN,        OUTPUT);
  pinMode(LED_BUILTIN_PIN, OUTPUT);
  digitalWrite(BULB_PIN,        LOW);
  digitalWrite(LED_BUILTIN_PIN, LOW);

  Serial.println("==============================================");
  Serial.println("  Automated Yaka Token System — ESP32 Meter  ");
  Serial.println("==============================================");

  prefs.begin("yaka", false);
  activeToken    = prefs.getString("token", "");
  remainingUnits = prefs.getInt("units", 0);

  if (remainingUnits > 0 && activeToken != "") {
    Serial.println("[INFO] Restored previous session.");
    setBulb(true);
  } else {
    Serial.println("[INFO] No active token. Bulb is OFF.");
    setBulb(false);
  }

  setupWiFi();
  if (wifiConnected) setupServer();

  printStatus();
  Serial.println("[READY] Send a 16-digit token via Serial or POST /token");
}

// ── loop() ───────────────────────────────────────
void loop() {
  handleSerialInput();

  if (wifiConnected) {
    server.handleClient();
  }

  static unsigned long lastTick = 0;
  if (bulbState && millis() - lastTick >= 30000) {
    lastTick = millis();
    remainingUnits--;
    prefs.putInt("units", remainingUnits);
    Serial.printf("[METER] Units remaining: %d\n", remainingUnits);

    if (remainingUnits <= 0) {
      Serial.println("[METER] Units exhausted! Bulb OFF.");
      setBulb(false);
      activeToken = "";
      prefs.putString("token", "");
    }
  }

  delay(100);
}

// ── handleSerialInput() ──────────────────────────
void handleSerialInput() {
  if (!Serial.available()) return;

  String input = Serial.readStringUntil('\n');
  input.trim();
  if (input.length() == 0) return;

  if (input.equalsIgnoreCase("STATUS")) { printStatus(); return; }
  if (input.equalsIgnoreCase("RESET")) {
    activeToken = ""; remainingUnits = 0;
    prefs.clear(); setBulb(false);
    Serial.println("[CMD] Meter reset.");
    return;
  }

  Serial.printf("[SERIAL] Token received: %s\n", input.c_str());
  applyToken(input);
}

// ── validateToken() ──────────────────────────────
bool validateToken(String token) {
  if (token.length() != TOKEN_LENGTH) {
    Serial.println("[VALIDATE] Fail — wrong length.");
    return false;
  }
  for (char c : token) {
    if (!isDigit(c)) {
      Serial.println("[VALIDATE] Fail — non-numeric character.");
      return false;
    }
  }
  if (token == activeToken && remainingUnits > 0) {
    Serial.println("[VALIDATE] Fail — token already active.");
    return false;
  }
  int sum = 0;
  for (char c : token) sum += (c - '0');
  if (sum % 7 != 0) {
    Serial.printf("[VALIDATE] Fail — checksum failed (sum=%d).\n", sum);
    return false;
  }
  return true;
}

// ── applyToken() ─────────────────────────────────
void applyToken(String token) {
  if (validateToken(token)) {
    Serial.println("[TOKEN] Valid! Activating meter...");
    activeToken    = token;
    remainingUnits = MAX_UNITS;
    prefs.putString("token", activeToken);
    prefs.putInt("units", remainingUnits);
    setBulb(true);
    Serial.printf("[TOKEN] Bulb ON. Units granted: %d\n", remainingUnits);
  } else {
    Serial.println("[TOKEN] Invalid token. Bulb remains OFF.");
    setBulb(false);
  }
}

// ── setBulb() ────────────────────────────────────
void setBulb(bool state) {
  bulbState = state;
  digitalWrite(BULB_PIN,        state ? HIGH : LOW);
  digitalWrite(LED_BUILTIN_PIN, state ? HIGH : LOW);
}

// ── printStatus() ────────────────────────────────
void printStatus() {
  Serial.println("──────────────────────────────");
  Serial.printf("  Token  : %s\n", activeToken == "" ? "NONE" : activeToken.c_str());
  Serial.printf("  Units  : %d\n", remainingUnits);
  Serial.printf("  Bulb   : %s\n", bulbState ? "ON" : "OFF");
  Serial.printf("  WiFi   : %s\n", wifiConnected ? WiFi.localIP().toString().c_str() : "Not connected");
  Serial.println("──────────────────────────────");
}

// ── setupWiFi() ──────────────────────────────────
void setupWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500); Serial.print(".");
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected)
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println("\n[WiFi] Not connected. Serial-only mode.");
}

// ── setupServer() ────────────────────────────────
void setupServer() {
  server.on("/token", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"No body\"}");
      return;
    }
    String body = server.arg("plain");
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    String token = doc["token"].as<String>();
    int units    = doc.containsKey("units") ? (int)doc["units"] : MAX_UNITS;

    Serial.printf("[API] Token from backend: %s (units: %d)\n", token.c_str(), units);

    if (validateToken(token)) {
      activeToken = token; remainingUnits = units;
      prefs.putString("token", activeToken);
      prefs.putInt("units", remainingUnits);
      setBulb(true);
      server.send(200, "application/json",
        "{\"status\":\"success\",\"units\":" + String(units) + "}");
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid token\"}");
    }
  });

  server.on("/status", HTTP_GET, []() {
    String resp = "{\"token\":\"" + activeToken + "\",\"units\":"
                  + String(remainingUnits) + ",\"bulb\":\""
                  + (bulbState ? "ON" : "OFF") + "\"}";
    server.send(200, "application/json", resp);
  });

  server.begin();
  Serial.println("[HTTP] Server started.");
}