/*
 * ============================================================
 *  АВТОМАТИЧНА СИСТЕМА ПОЛИВУ РОСЛИН
 *  Точно відповідає схемі Altium Designer
 *
 *  Лист 1 — ESP8266 MCU
 *  Лист 2 — CD74HC4067 + 4 датчики вологості
 *  Лист 3 — LM2596 (12V → 3.3V)
 *  Лист 4 — 4x IRLZ44N (помпи 12V)
 *
 *  ПІНИ (точно з Altium):
 *  ┌──────────────────────────────────────────────┐
 *  │  CD74HC4067 мультиплексор (Лист 2):          │
 *  │    S0  → D1  (GPIO5)                         │
 *  │    S1  → D2  (GPIO4)                         │
 *  │    S2  → D3  (GPIO0) + 10к підтяжка до 3.3V │
 *  │    S3  → D6  (GPIO12)                        │
 *  │    EN  → підтяжка 10к до GND (завжди ON)     │
 *  │    SIG → A0  через 100R резистор             │
 *  │                                              │
 *  │  IRLZ44N помпи (Лист 4):                     │
 *  │    Помпа 1 → GPIO13 (D7) + 100R + 4K7       │
 *  │    Помпа 2 → GPIO15 (D8) + 100R + 4K7       │
 *  │    Помпа 3 → GPIO16 (D0) + 100R + 4K7       │
 *  │    Помпа 4 → GPIO2  (D4) + 100R + 4K7       │
 *  │                                              │
 *  │  ПРИМІТКА: GPIO15 має підтяжку 4K7 до GND   │
 *  │  на платі — ESP завантажується нормально.    │
 *  │  GPIO0 має підтяжку 10к до 3.3V — безпечно. │
 *  └──────────────────────────────────────────────┘
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// =================== WI-FI ===================
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// =================== ПІНИ МУЛЬТИПЛЕКСОРА ===================
// Точно з Листа 2 схеми Altium
const int S0  = 5;   // D1 — GPIO5
const int S1  = 4;   // D2 — GPIO4
const int S2  = 0;   // D3 — GPIO0 (з підтяжкою 10к→3.3V на платі)
const int S3  = 12;  // D6 — GPIO12
// EN підключений до GND через підтяжку на платі (завжди активний)
const int SIG = A0;  // A0 — через 100R резистор

// =================== ПІНИ ПОМП ===================
// Точно з Листа 4 схеми Altium
// Кожен через 100R (Gate) + 4K7 (Gate→GND) резистори
const int pumpPins[4] = {13, 15, 16, 2};
// GPIO13=D7, GPIO15=D8, GPIO16=D0, GPIO2=D4

// =================== ПАРАМЕТРИ ===================
const int   DRY_THRESHOLD  = 700;   // ADC поріг (0–1023)
                                     // 0   = повністю мокро
                                     // 1023= повністю сухо
                                     // підберіть під свої датчики
const unsigned long PUMP_DURATION  = 5000;  // мс роботи помпи
const unsigned long CHECK_INTERVAL = 10000; // мс між перевірками

// =================== ЗМІННІ ===================
int  moisture[4]       = {0, 0, 0, 0};
bool pumpState[4]      = {false, false, false, false};
unsigned long pumpStart[4] = {0, 0, 0, 0};
unsigned long lastCheck    = 0;
unsigned long startTime    = 0;
bool wifiReady = false;

ESP8266WebServer server(80);

// =============================================
//  МУЛЬТИПЛЕКСОР CD74HC4067
//  Вибір каналу 0–3 і зчитування через A0
// =============================================
int readMoisture(int ch) {
  digitalWrite(S0, (ch & 0x01) ? HIGH : LOW);
  digitalWrite(S1, (ch & 0x02) ? HIGH : LOW);
  digitalWrite(S2, (ch & 0x04) ? HIGH : LOW);
  digitalWrite(S3, (ch & 0x08) ? HIGH : LOW);
  delay(5);               // час встановлення сигналу
  return analogRead(SIG); // 0–1023
}

// =============================================
//  НЕБЛОКУЮЧЕ КЕРУВАННЯ ПОМПАМИ
//  Таймер через millis() — веб-сервер не зупиняється
// =============================================
void startPump(int id) {
  if (id < 0 || id > 3) return;
  if (pumpState[id]) return; // вже працює
  pumpState[id] = true;
  pumpStart[id] = millis();
  digitalWrite(pumpPins[id], HIGH);
}

void updatePumps() {
  for (int i = 0; i < 4; i++) {
    if (pumpState[i] && millis() - pumpStart[i] >= PUMP_DURATION) {
      pumpState[i] = false;
      digitalWrite(pumpPins[i], LOW);
    }
  }
}

// =============================================
//  ПЕРЕВІРКА ДАТЧИКІВ І АВТОПОЛИВ
// =============================================
void checkSensors() {
  for (int i = 0; i < 4; i++) {
    moisture[i] = readMoisture(i);
    int pct = map(moisture[i], 1023, 0, 0, 100);
    pct = constrain(pct, 0, 100);
    Serial.printf("[Рослина %d] ADC=%d (%d%%)", i+1, moisture[i], pct);

    if (moisture[i] > DRY_THRESHOLD && !pumpState[i]) {
      Serial.printf(" → СУХО, запускаємо помпу GPIO%d\n", pumpPins[i]);
      startPump(i);
    } else {
      Serial.println(pumpState[i] ? " → Полив триває" : " → Волого, OK");
    }
  }
}

// =============================================
//  ВЕБ-ІНТЕРФЕЙС
// =============================================
String getUptime() {
  unsigned long s = (millis() - startTime) / 1000;
  char b[9];
  snprintf(b, sizeof(b), "%02lu:%02lu:%02lu",
           s/3600, (s%3600)/60, s%60);
  return String(b);
}

void handleRoot() {
  String html = F("<!DOCTYPE html><html lang='uk'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='3'>"
    "<title>Система поливу</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:Arial,sans-serif;background:#0d1b0e;color:#ccc}"
    ".wrap{max-width:480px;margin:0 auto;padding:16px}"
    "h1{text-align:center;background:#1b5e20;color:#a5d6a7;"
    "padding:16px;border-radius:12px;margin-bottom:14px;font-size:18px}"
    ".info{background:#132b14;border-radius:10px;padding:13px;"
    "margin-bottom:14px;font-size:13px}"
    ".info p{margin:5px 0}"
    ".lbl{color:#66bb6a}.val{color:#fff;font-weight:bold}"
    ".card{background:#132b14;border-radius:12px;padding:14px;"
    "margin-bottom:10px}"
    ".ct{font-weight:bold;font-size:15px;color:#a5d6a7;margin-bottom:10px}"
    ".row{display:flex;justify-content:space-between;font-size:13px;margin:5px 0}"
    ".bar-bg{background:#0a1f0a;border-radius:6px;height:10px;margin-top:8px}"
    ".bar{height:10px;border-radius:6px;transition:width .5s}"
    ".wet{background:#43a047}.dry{background:#e53935}"
    ".s-wet{color:#66bb6a;font-weight:bold}"
    ".s-dry{color:#ef5350;font-weight:bold}"
    ".p-on{color:#ef5350;font-weight:bold}"
    ".p-off{color:#66bb6a}"
    ".btn{display:block;width:100%;padding:11px;background:#1b5e20;"
    "color:#a5d6a7;border:1px solid #43a047;border-radius:9px;"
    "font-size:14px;text-align:center;text-decoration:none;margin-top:8px}"
    "</style></head><body><div class='wrap'>"
    "<h1>&#127807; СИСТЕМА ПОЛИВУ РОСЛИН</h1>");

  html += "<div class='info'>";
  html += "<p><span class='lbl'>Wi-Fi: </span><span class='val'>";
  html += wifiReady ? "&#10003; Підключено" : "&#8987; Підключення...";
  html += "</span></p>";
  if (wifiReady)
    html += "<p><span class='lbl'>IP: </span><span class='val'>"
          + WiFi.localIP().toString() + "</span></p>";
  html += "<p><span class='lbl'>Час роботи: </span><span class='val'>"
        + getUptime() + "</span></p>";
  html += "<p><span class='lbl'>МК: </span><span class='val'>"
          "ESP8266 + CD74HC4067 + 4x IRLZ44N</span></p>";
  html += "<p><span class='lbl'>Поріг сухості: </span><span class='val'>"
        + String(DRY_THRESHOLD) + "/1023</span></p>";
  html += "</div>";

  for (int i = 0; i < 4; i++) {
    int pct = map(moisture[i], 1023, 0, 0, 100);
    pct = constrain(pct, 0, 100);
    bool isDry = moisture[i] > DRY_THRESHOLD;
    unsigned long left = 0;
    if (pumpState[i]) {
      unsigned long el = millis() - pumpStart[i];
      left = el < PUMP_DURATION ? (PUMP_DURATION - el) / 1000 : 0;
    }

    html += "<div class='card'>";
    html += "<div class='ct'>&#127807; Рослина " + String(i+1)
          + " &nbsp;<span style='font-size:12px;color:#66bb6a;font-weight:normal'>"
          + "GPIO" + String(pumpPins[i]) + "</span></div>";
    html += "<div class='row'><span>Вологість ADC:</span>"
            "<span><b>" + String(moisture[i]) + "</b>/1023</span></div>";
    html += "<div class='row'><span>Вологість:</span>"
            "<span><b>" + String(pct) + "%</b></span></div>";
    html += "<div class='row'><span>Стан:</span>"
            "<span class='" + String(isDry?"s-dry":"s-wet") + "'>"
          + (isDry ? "&#9888; СУХО" : "&#10003; ВОЛОГО") + "</span></div>";
    html += "<div class='row'><span>Помпа:</span>"
            "<span class='" + String(pumpState[i]?"p-on":"p-off") + "'>";
    if (pumpState[i])
      html += "&#128167; ПРАЦЮЄ &#8212; ще " + String(left) + "с";
    else
      html += "&#9898; вимкнена";
    html += "</span></div>";
    html += "<div class='bar-bg'><div class='bar "
          + String(isDry?"dry":"wet")
          + "' style='width:" + String(pct) + "%'></div></div>";
    html += "<a class='btn' href='/pump?id=" + String(i)
          + "'>&#128167; Полити вручну</a>";
    html += "</div>";
  }

  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handlePump() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id >= 0 && id < 4) startPump(id);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStatus() {
  String j = "{\"uptime\":\"" + getUptime() + "\","
             "\"wifi\":" + String(wifiReady?"true":"false") + ","
             "\"sensors\":[";
  for (int i = 0; i < 4; i++) {
    if (i) j += ",";
    int pct = constrain(map(moisture[i], 1023, 0, 0, 100), 0, 100);
    j += "{\"id\":" + String(i+1)
       + ",\"gpio\":" + String(pumpPins[i])
       + ",\"adc\":"  + String(moisture[i])
       + ",\"pct\":"  + String(pct)
       + ",\"dry\":"  + String(moisture[i]>DRY_THRESHOLD?"true":"false")
       + ",\"pump\":" + String(pumpState[i]?"true":"false") + "}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// =============================================
//  SETUP
// =============================================
void setup() {
  Serial.begin(115200);
  startTime = millis();
  Serial.println(F("\n=== СИСТЕМА ПОЛИВУ РОСЛИН ==="));
  Serial.println(F("ESP8266 + CD74HC4067 + 4x IRLZ44N + LM2596"));

  // Мультиплексор
  pinMode(S0, OUTPUT); digitalWrite(S0, LOW);
  pinMode(S1, OUTPUT); digitalWrite(S1, LOW);
  pinMode(S2, OUTPUT); digitalWrite(S2, LOW); // GPIO0 — з підтяжкою на платі
  pinMode(S3, OUTPUT); digitalWrite(S3, LOW);
  // EN не керується програмно — завжди LOW через підтяжку на платі

  // Помпи — LOW за замовчуванням
  for (int i = 0; i < 4; i++) {
    pinMode(pumpPins[i], OUTPUT);
    digitalWrite(pumpPins[i], LOW);
  }

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print(F("Wi-Fi"));
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-t < 15000) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    Serial.println("\nIP: " + WiFi.localIP().toString());
  } else {
    Serial.println(F("\nWi-Fi недоступний — працюємо автономно"));
  }

  server.on("/",       handleRoot);
  server.on("/pump",   handlePump);
  server.on("/status", handleStatus);
  server.begin();
  Serial.println(F("Сервер запущено. Система готова!\n"));
}

// =============================================
//  LOOP — повністю неблокуючий
// =============================================
void loop() {
  server.handleClient();
  updatePumps(); // таймери помп без delay()

  if (!wifiReady && WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    Serial.println("Wi-Fi OK: " + WiFi.localIP().toString());
  }

  if (millis() - lastCheck >= CHECK_INTERVAL) {
    lastCheck = millis();
    Serial.println(F("--- Перевірка датчиків ---"));
    checkSensors();
    Serial.println(F("--------------------------"));
  }
}
