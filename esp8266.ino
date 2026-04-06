#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

/*
  SmartFlow NodeMCU (ESP8266) - 3 lane build

  IMPORTANT WIRING NOTES:
  1) HC-SR04 ECHO is 5V logic. ESP8266 GPIO is 3.3V only.
    Use a voltage divider (or level shifter) on EACH ECHO line.
  2) In this build, sensors use D1/D2, so OLED I2C is moved to D3/D4.
  3) Do NOT place buzzer or sensors on RX (GPIO3) while flashing.
  4) If board fails to boot, disconnect OLED temporarily and retry flashing.
*/

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// DHT is on GPIO3 (RX), so keep Serial debug off by default to avoid pin contention.
#define DEBUG_SERIAL 0

#if DEBUG_SERIAL
  #define DBG_PRINTLN(x) Serial.println(x)
  #define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(...)
#endif

// WiFi
const char* ssid = "RAPTOR";
const char* password = "raptoromen";

String server = "http://192.168.137.1:8080";

// OLED (moved away from default D1/D2 because those are used by sensors here)
#define OLED_SDA D3
#define OLED_SCL D4

// Sensor pins
#define TRIG1 D5
#define ECHO1 D6
#define TRIG2 D7
#define ECHO2 D2
#define TRIG3 D1
#define ECHO3 D0

// Buzzer
#define BUZZER D8
#define BUZZER_ACTIVE_HIGH 1
#define BUZZER_USE_TONE 1

// DHT11 (user requested on GPIO3 / RX)
#define DHTPIN 3
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

int north=0, south=0, east=0;
String activeGreenLane = "north";
int cmdNorthSec = 20;
int cmdSouthSec = 20;
int cmdEastSec = 20;
int cmdEfficiency = 0;
float dhtTempC = NAN;
float dhtHumidity = NAN;
bool buzzerAlarmActive = false;
bool buzzerPhaseOn = false;
int buzzerTickCount = 0;

unsigned long lastSendMs = 0;
unsigned long lastCmdMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long wifiFailCount = 0;
unsigned long lastDhtReadMs = 0;
unsigned long lastBuzzerPhaseMs = 0;
unsigned long lastWifiRetryMs = 0;
unsigned long lastSensorSampleMs = 0;
unsigned long wifiAttemptStartMs = 0;
const unsigned long SEND_EVERY_MS = 1800;
const unsigned long CMD_EVERY_MS = 1800;
const unsigned long HEARTBEAT_EVERY_MS = 4000;
const unsigned long DHT_READ_EVERY_MS = 4500;
const unsigned long WIFI_RETRY_EVERY_MS = 3500;
const unsigned long WIFI_CONNECT_WINDOW_MS = 12000;
const unsigned long SENSOR_SAMPLE_GAP_MS = 70;
const unsigned long BUZZER_ON_MS = 90;
const unsigned long BUZZER_OFF_MS = 220;
const unsigned long BUZZER_PAUSE_MS = 900;
const int BUZZER_TICKS_PER_BURST = 3;
const int BUZZER_ON_THRESHOLD = 90;
const int BUZZER_OFF_THRESHOLD = 78;
const unsigned long BUZZER_ON_HOLD_MS = 1800;
const unsigned long BUZZER_OFF_HOLD_MS = 2600;
int sensorRoundRobin = 0;
bool wifiConnectInProgress = false;
unsigned long buzzerHighSinceMs = 0;
unsigned long buzzerLowSinceMs = 0;

int parseJsonIntField(const String& payload, const char* key, int fallback) {
  String token = String("\"") + key + "\":";
  int idx = payload.indexOf(token);
  if (idx < 0) return fallback;

  idx += token.length();
  int end = idx;
  while (end < payload.length() && (isDigit(payload[end]) || payload[end] == '-')) {
    end++;
  }
  if (end <= idx) return fallback;
  return payload.substring(idx, end).toInt();
}

String parsePriorityLane(const String& payload, const String& fallback) {
  if (payload.indexOf("\"priority_lane\":\"north\"") >= 0) return "north";
  if (payload.indexOf("\"priority_lane\":\"south\"") >= 0) return "south";
  if (payload.indexOf("\"priority_lane\":\"east\"") >= 0) return "east";
  return fallback;
}

char laneToCode(const String& lane) {
  if (lane == "north") return 'N';
  if (lane == "south") return 'S';
  if (lane == "east") return 'E';
  return '-';
}

void drawLaneBar(int y, const char* label, int value, bool isActiveGreen) {
  const int barX = 12;
  const int barW = 78;
  const int barH = 7;
  const int safeValue = constrain(value, 0, 100);
  const int fillW = map(safeValue, 0, 100, 0, barW - 2);

  display.setCursor(0, y);
  display.print(label);

  display.drawRect(barX, y, barW, barH, WHITE);
  if (fillW > 0) {
    display.fillRect(barX + 1, y + 1, fillW, barH - 2, WHITE);
  }

  display.setCursor(94, y);
  display.printf("%3d", safeValue);
  display.setCursor(116, y);
  display.print('%');

  if (isActiveGreen) {
    display.fillCircle(126, y + 3, 2, WHITE);
  } else {
    display.drawCircle(126, y + 3, 2, WHITE);
  }
}

const char* wifiStatusText(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

void logScanForTargetSSID() {
  DBG_PRINTLN("[WIFI] scanning networks...");
  DBG_PRINTF("[WIFI] looking for SSID='%s'\n", ssid);
  int n = WiFi.scanNetworks(false, true);
  if (n <= 0) {
    DBG_PRINTLN("[WIFI] scan found no networks");
    return;
  }

  bool found = false;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    int r = WiFi.RSSI(i);
    int ch = WiFi.channel(i);
    bool enc = WiFi.encryptionType(i) != ENC_TYPE_NONE;
    DBG_PRINTF("[WIFI] AP[%d] '%s' ch=%d rssi=%d enc=%s\n", i, s.c_str(), ch, r, enc ? "ON" : "OPEN");
    if (s == String(ssid)) {
      found = true;
      DBG_PRINTF("[WIFI] target SSID found: ch=%d rssi=%d enc=%s\n", ch, r, enc ? "ON" : "OPEN");
      break;
    }
  }
  if (!found) DBG_PRINTLN("[WIFI] target SSID not visible in scan");
}

void ensureWiFi() {
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    if (wifiConnectInProgress) {
      DBG_PRINTF("[WIFI] connected. IP=%s RSSI=%d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    wifiConnectInProgress = false;
    wifiFailCount = 0;
    return;
  }

  unsigned long now = millis();
  if (!wifiConnectInProgress) {
    if (now - lastWifiRetryMs < WIFI_RETRY_EVERY_MS) return;
    lastWifiRetryMs = now;
    wifiAttemptStartMs = now;
    wifiConnectInProgress = true;
    DBG_PRINTLN("[WIFI] begin connect...");
    WiFi.begin(ssid, password);
    return;
  }

  // While connection is in progress, do not restart too quickly.
  if (now - wifiAttemptStartMs < WIFI_CONNECT_WINDOW_MS) return;

  // Attempt timed out; retry with a fresh begin().
  wifiFailCount++;
  lastWifiRetryMs = now;
  wifiAttemptStartMs = now;
  DBG_PRINTF("[WIFI] connect timeout. status=%d (%s), failCount=%lu\n", (int)st, wifiStatusText(st), wifiFailCount);
  if (wifiFailCount == 1 || wifiFailCount % 4 == 0) {
    logScanForTargetSSID();
  }

  WiFi.disconnect(false);
  WiFi.begin(ssid, password);
}

void setBuzzer(bool on) {
#if BUZZER_USE_TONE
  if (on) {
    tone(BUZZER, 2400);
  } else {
    noTone(BUZZER);
    digitalWrite(BUZZER, BUZZER_ACTIVE_HIGH ? LOW : HIGH);
  }
#else
  if (on) {
    digitalWrite(BUZZER, BUZZER_ACTIVE_HIGH ? HIGH : LOW);
  } else {
    digitalWrite(BUZZER, BUZZER_ACTIVE_HIGH ? LOW : HIGH);
  }
#endif
}

void updateBuzzerPattern() {
  if (!buzzerAlarmActive) {
    buzzerPhaseOn = false;
    buzzerTickCount = 0;
    setBuzzer(false);
    return;
  }

  unsigned long now = millis();
  unsigned long elapsed = now - lastBuzzerPhaseMs;

  if (buzzerPhaseOn) {
    if (elapsed >= BUZZER_ON_MS) {
      buzzerPhaseOn = false;
      setBuzzer(false);
      lastBuzzerPhaseMs = now;
      buzzerTickCount++;
    }
    return;
  }

  unsigned long neededOff = (buzzerTickCount >= BUZZER_TICKS_PER_BURST) ? BUZZER_PAUSE_MS : BUZZER_OFF_MS;
  if (elapsed >= neededOff) {
    if (buzzerTickCount >= BUZZER_TICKS_PER_BURST) buzzerTickCount = 0;
    buzzerPhaseOn = true;
    setBuzzer(true);
    lastBuzzerPhaseMs = now;
  }
}

void updateDhtReading() {
  unsigned long now = millis();
  if (now - lastDhtReadMs < DHT_READ_EVERY_MS) return;
  lastDhtReadMs = now;

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    dhtHumidity = h;
    dhtTempC = t;
  }
}

long readDistance(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long d = pulseIn(echo, HIGH, 30000);
  if (d==0) return 200;
  return d*0.034/2;
}

int toDensity(long d){
  if(d<=5) return 100;
  if(d>=100) return 5;
  return map(d,100,5,5,100);
}

void setup(){
  Serial.begin(115200);
  delay(150);
  DBG_PRINTLN("\n[BOOT] SmartFlow NodeMCU start");
  DBG_PRINTF("[BOOT] target SSID='%s'\n", ssid);
  DBG_PRINTF("[BOOT] server='%s'\n", server.c_str());
  if (String(ssid) == "YOUR_WIFI" || String(password) == "YOUR_PASS") {
    DBG_PRINTLN("[BOOT] WARNING: WiFi credentials still set to placeholder values");
  }
  if (server.indexOf("YOUR_PC_IP") >= 0) {
    DBG_PRINTLN("[BOOT] WARNING: server still uses placeholder YOUR_PC_IP");
  }
  DBG_PRINTF("[BOOT] Sensor pins N(%d,%d) S(%d,%d) E(%d,%d)\n", TRIG1, ECHO1, TRIG2, ECHO2, TRIG3, ECHO3);
  DBG_PRINTF("[BOOT] OLED SDA=%d SCL=%d, BUZZER=%d\n", OLED_SDA, OLED_SCL, BUZZER);

  pinMode(TRIG1, OUTPUT); digitalWrite(TRIG1, LOW); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); digitalWrite(TRIG2, LOW); pinMode(ECHO2, INPUT);
  pinMode(TRIG3, OUTPUT); digitalWrite(TRIG3, LOW); pinMode(ECHO3, INPUT);

  pinMode(BUZZER, OUTPUT);
  setBuzzer(false);
  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  ensureWiFi();

  Wire.begin(OLED_SDA, OLED_SCL);
  bool oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  DBG_PRINTF("[OLED] init %s\n", oledOk ? "OK" : "FAIL");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("SMARTFLOW BOOT");
  display.println(oledOk ? "OLED OK" : "OLED FAIL");
  display.display();
}

void sendData(){
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;
  String url = server + "/sensor-data";

  http.begin(client, url);
  http.addHeader("Content-Type","application/json");
  http.setTimeout(900);

  String json = "{\"north\":"+String(north)+",\"south\":"+String(south)+",\"east\":"+String(east);
  if (!isnan(dhtTempC)) json += ",\"tempC\":" + String(dhtTempC, 1);
  if (!isnan(dhtHumidity)) json += ",\"humidity\":" + String(dhtHumidity, 1);
  json += "}";
  int code = http.POST(json);
  if (code <= 0) {
    DBG_PRINTF("[HTTP] POST %s failed: %d (%s)\n", url.c_str(), code, HTTPClient::errorToString(code).c_str());
  } else {
    DBG_PRINTF("[HTTP] POST %s -> %d\n", url.c_str(), code);
  }
  http.end();
}

void getCommand(){
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;
  String url = server + "/command";

  http.begin(client, url);
  http.setTimeout(900);
  int code = http.GET();

  if(code>0){
    String payload = http.getString();
    DBG_PRINTF("[HTTP] GET %s -> %d, payload=%s\n", url.c_str(), code, payload.c_str());

    cmdNorthSec = parseJsonIntField(payload, "north", cmdNorthSec);
    cmdSouthSec = parseJsonIntField(payload, "south", cmdSouthSec);
    cmdEastSec = parseJsonIntField(payload, "east", cmdEastSec);
    cmdEfficiency = parseJsonIntField(payload, "efficiency", cmdEfficiency);
    activeGreenLane = parsePriorityLane(payload, activeGreenLane);
  } else {
    DBG_PRINTF("[HTTP] GET %s failed: %d (%s)\n", url.c_str(), code, HTTPClient::errorToString(code).c_str());
  }
  http.end();
}

void updateSensorsRoundRobin() {
  unsigned long now = millis();
  if (now - lastSensorSampleMs < SENSOR_SAMPLE_GAP_MS) return;
  lastSensorSampleMs = now;

  if (sensorRoundRobin == 0) {
    north = toDensity(readDistance(TRIG1, ECHO1));
  } else if (sensorRoundRobin == 1) {
    south = toDensity(readDistance(TRIG2, ECHO2));
  } else {
    east = toDensity(readDistance(TRIG3, ECHO3));
  }

  sensorRoundRobin = (sensorRoundRobin + 1) % 3;
}

void updateBuzzerAlarmState() {
  unsigned long now = millis();
  int peak = max(north, max(south, east));

  if (peak >= BUZZER_ON_THRESHOLD) {
    buzzerLowSinceMs = 0;
    if (buzzerHighSinceMs == 0) buzzerHighSinceMs = now;
    if (!buzzerAlarmActive && (now - buzzerHighSinceMs >= BUZZER_ON_HOLD_MS)) {
      buzzerAlarmActive = true;
    }
  } else if (peak <= BUZZER_OFF_THRESHOLD) {
    buzzerHighSinceMs = 0;
    if (buzzerLowSinceMs == 0) buzzerLowSinceMs = now;
    if (buzzerAlarmActive && (now - buzzerLowSinceMs >= BUZZER_OFF_HOLD_MS)) {
      buzzerAlarmActive = false;
    }
  } else {
    // In hysteresis band: keep current state and clear timers.
    buzzerHighSinceMs = 0;
    buzzerLowSinceMs = 0;
  }
}

void updateOLED(){
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  display.setCursor(0,0);
  display.println("SMARTFLOW");
  display.setCursor(74,0);
  display.print("WiFi:");
  display.print(WiFi.status() == WL_CONNECTED ? "OK" : "..");

  drawLaneBar(12, "N", north, activeGreenLane == "north");
  drawLaneBar(22, "S", south, activeGreenLane == "south");
  drawLaneBar(32, "E", east,  activeGreenLane == "east");

  display.drawFastHLine(0, 42, SCREEN_WIDTH, WHITE);
  display.setCursor(0,46);
  display.printf("GREEN:%c EFF:%d", laneToCode(activeGreenLane), cmdEfficiency);
  display.setCursor(0,56);
  display.printf("T:%d/%d/%d", cmdNorthSec, cmdSouthSec, cmdEastSec);

  display.display();
}

void loop(){
  ensureWiFi();
  updateSensorsRoundRobin();
  updateBuzzerAlarmState();
  updateDhtReading();
  updateBuzzerPattern();

  unsigned long now = millis();
  if (now - lastSendMs >= SEND_EVERY_MS) {
    sendData();
    lastSendMs = now;
  }

  if (now - lastCmdMs >= CMD_EVERY_MS) {
    getCommand();
    lastCmdMs = now;
  }

  if (now - lastHeartbeatMs >= HEARTBEAT_EVERY_MS) {
    DBG_PRINTF("[HB] wifi=%s N=%d S=%d E=%d\n",
      WiFi.status() == WL_CONNECTED ? "OK" : "DOWN",
      north, south, east);
    lastHeartbeatMs = now;
  }

  updateOLED();
}