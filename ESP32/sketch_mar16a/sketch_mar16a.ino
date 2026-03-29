#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ── Pin & config constants ────────────────────────────────────────
#define BULB_PIN        5
#define LED_BUILTIN_PIN 2
#define TOKEN_LENGTH    20        // FIXED: was 16, server generates 20-digit tokens

// ── WiFi & server config — CHANGE THESE ──────────────────────────
const char* WIFI_SSID     = "No data";
const char* WIFI_PASSWORD = "Kimal246";
const char* SERVER_IP     = "192.168.1.2";   // your laptop's local IP (run ipconfig)
const char* METER_NUMBER  = "YK-001";        // must match what user types in USSD

// ── State variables ───────────────────────────────────────────────
Preferences prefs;
String  activeToken    = "";
int     remainingUnits = 0;
bool    bulbState      = false;
bool    wifiConnected  = false;

// ── Forward declarations ──────────────────────────────────────────
bool validateToken(String token);
void applyToken(String token, int units);
void setBulb(bool state);
void printStatus();
void handleSerialInput();
void setupWiFi();
void pollServer();
void activateToken(String meter, String token);
void markTokenUsed(String meter, String token);

// ════════════════════════════════════════════════════════════════
//  setup()
// ════════════════════════════════════════════════════════════════
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

  // Restore previous session from flash memory
  prefs.begin("yaka", false);
  activeToken    = prefs.getString("token", "");
  remainingUnits = prefs.getInt("units", 0);

  if (remainingUnits > 0 && activeToken != "") {
    Serial.println("[INFO] Restored previous session — bulb ON.");
    setBulb(true);
  } else {
    Serial.println("[INFO] No active session. Bulb OFF. Waiting for token.");
    setBulb(false);
  }

  setupWiFi();
  printStatus();
  Serial.println("[READY] Polling server every 5 seconds for new tokens.");
  Serial.println("[READY] Or send a 20-digit token via Serial to test.");
}

// ════════════════════════════════════════════════════════════════
//  loop()
// ════════════════════════════════════════════════════════════════
void loop() {
  // Handle manual Serial input (for testing without server)
  handleSerialInput();

  // Poll the Node.js server every 5 seconds
  static unsigned long lastPoll = 0;
  if (wifiConnected && millis() - lastPoll >= 5000) {s
    lastPoll = millis();
    pollServer();
  }

  // Unit depletion — subtract 1 unit every 30 seconds while bulb is ON
  static unsigned long lastTick = 0;
  if (bulbState && millis() - lastTick >= 30000) {
    lastTick = millis();
    remainingUnits--;
    prefs.putInt("units", remainingUnits);
    Serial.printf("[METER] Units remaining: %d\n", remainingUnits);

    if (remainingUnits <= 0) {
      Serial.println("[METER] Units exhausted! Turning bulb OFF.");
      setBulb(false);

      // Tell the server this token has been fully used
      if (wifiConnected && activeToken != "") {
        markTokenUsed(METER_NUMBER, activeToken);
      }

      // Clear the active token
      activeToken = "";
      prefs.putString("token", "");
      prefs.putInt("units", 0);
      Serial.println("[METER] Ready for next token.");
    }
  }

  delay(100);
}

// ════════════════════════════════════════════════════════════════
//  pollServer() — asks the server if there is a new token
// ════════════════════════════════════════════════════════════════
void pollServer() {
  HTTPClient http;
  String url = String("http://") + SERVER_IP + ":3000/api/tokens/" + METER_NUMBER;

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (err) {
      Serial.println("[POLL] Failed to parse server response.");
      http.end();
      return;
    }

    bool hasNewToken = doc["hasNewToken"].as<bool>();

    if (hasNewToken) {
      String token = doc["token"].as<String>();
      int    units = doc["units"].as<int>();

      Serial.printf("[POLL] New token received: %s (%d units)\n", token.c_str(), units);
      applyToken(token, units);

      // Tell the server the token is now active (bulb is ON)
      activateToken(METER_NUMBER, token);
    } else {
      Serial.println("[POLL] No new token. Waiting...");
    }
  } else {
    Serial.printf("[POLL] Server error — HTTP %d\n", httpCode);
  }

  http.end();
}

// ════════════════════════════════════════════════════════════════
//  activateToken() — tells server: "I received this token, bulb is ON"
// ════════════════════════════════════════════════════════════════
void activateToken(String meter, String token) {
  HTTPClient http;
  String url = String("http://") + SERVER_IP + ":3000/api/tokens/activate";

  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "meterNumber=" + meter + "&token=" + token;
  int httpCode = http.POST(body);

  if (httpCode == 200) {
    Serial.println("[ACTIVATE] Server updated — status: Active.");
  } else {
    Serial.printf("[ACTIVATE] Server error — HTTP %d\n", httpCode);
  }

  http.end();
}

// ════════════════════════════════════════════════════════════════
//  markTokenUsed() — tells server: "units hit zero, bulb is OFF"
// ════════════════════════════════════════════════════════════════
void markTokenUsed(String meter, String token) {
  HTTPClient http;
  String url = String("http://") + SERVER_IP + ":3000/api/tokens/use";

  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "meterNumber=" + meter + "&token=" + token;
  int httpCode = http.POST(body);

  if (httpCode == 200) {
    Serial.println("[USE] Server updated — status: Used.");
  } else {
    Serial.printf("[USE] Server error — HTTP %d\n", httpCode);
  }

  http.end();
}

// ════════════════════════════════════════════════════════════════
//  validateToken() — checks format only, no checksum
// ════════════════════════════════════════════════════════════════
bool validateToken(String token) {
  // Check length
  if (token.length() != TOKEN_LENGTH) {
    Serial.printf("[VALIDATE] Fail — wrong length (%d, expected %d).\n",
                  token.length(), TOKEN_LENGTH);
    return false;
  }

  // Check all digits are numeric
  for (char ch : token) {
    if (!isDigit(ch)) {
      Serial.println("[VALIDATE] Fail — non-numeric character found.");
      return false;
    }
  }

  // Check token is not already active
  if (token == activeToken && remainingUnits > 0) {
    Serial.println("[VALIDATE] Fail — token already active.");
    return false;
  }

  // NOTE: Checksum removed — server generates tokens with Math.random()
  // which will not reliably satisfy any fixed checksum formula.

  return true;
}

// ════════════════════════════════════════════════════════════════
//  applyToken() — loads the token and turns the bulb on
// ════════════════════════════════════════════════════════════════
void applyToken(String token, int units) {
  if (!validateToken(token)) {
    Serial.println("[TOKEN] Invalid token. Ignoring.");
    return;
  }

  Serial.println("[TOKEN] Valid! Activating meter...");
  activeToken    = token;
  remainingUnits = units;   // FIXED: units come from server, not hardcoded

  // Save to flash so a power cut doesn't lose the session
  prefs.putString("token", activeToken);
  prefs.putInt("units",    remainingUnits);

  setBulb(true);
  Serial.printf("[TOKEN] Bulb ON. Units loaded: %d\n", remainingUnits);
  printStatus();
}

// ════════════════════════════════════════════════════════════════
//  handleSerialInput() — lets you test by typing a token via USB
// ════════════════════════════════════════════════════════════════
void handleSerialInput() {
  if (!Serial.available()) return;

  String input = Serial.readStringUntil('\n');
  input.trim();
  if (input.length() == 0) return;

  if (input.equalsIgnoreCase("STATUS")) {
    printStatus();
    return;
  }

  if (input.equalsIgnoreCase("RESET")) {
    activeToken = "";
    remainingUnits = 0;
    prefs.clear();
    setBulb(false);
    Serial.println("[CMD] Meter reset. All data cleared.");
    return;
  }

  if (input.equalsIgnoreCase("POLL")) {
    Serial.println("[CMD] Forcing poll now...");
    if (wifiConnected) pollServer();
    else Serial.println("[CMD] Not connected to WiFi.");
    return;
  }

  // Treat anything else as a token (for Serial testing)
  Serial.printf("[SERIAL] Token entered: %s\n", input.c_str());
  applyToken(input, 10);  // default 10 units for Serial-entered tokens
}

// ════════════════════════════════════════════════════════════════
//  setBulb() — controls relay and built-in LED
// ════════════════════════════════════════════════════════════════
void setBulb(bool state) {
  bulbState = state;
  digitalWrite(BULB_PIN,        state ? HIGH : LOW);
  digitalWrite(LED_BUILTIN_PIN, state ? HIGH : LOW);
}

// ════════════════════════════════════════════════════════════════
//  printStatus() — prints current meter state to Serial Monitor
// ════════════════════════════════════════════════════════════════
void printStatus() {
  Serial.println("──────────────────────────────────────────");
  Serial.printf("  Meter  : %s\n", METER_NUMBER);
  Serial.printf("  Token  : %s\n", activeToken == "" ? "NONE" : activeToken.c_str());
  Serial.printf("  Units  : %d\n", remainingUnits);
  Serial.printf("  Bulb   : %s\n", bulbState ? "ON" : "OFF");
  Serial.printf("  WiFi   : %s\n", wifiConnected
                  ? WiFi.localIP().toString().c_str()
                  : "Not connected");
  Serial.printf("  Server : http://%s:3000\n", SERVER_IP);
  Serial.println("──────────────────────────────────────────");
}

// ════════════════════════════════════════════════════════════════
//  setupWiFi() — connects to hotspot, times out after 10 seconds
// ════════════════════════════════════════════════════════════════
void setupWiFi() {
  Serial.printf("[WiFi] Connecting to: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }

  wifiConnected = (WiFi.status() == WL_CONNECTED);

  if (wifiConnected) {
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Not connected. Running in Serial-only mode.");
    Serial.println("[WiFi] Type a 20-digit token via Serial to test locally.");
  }
}
