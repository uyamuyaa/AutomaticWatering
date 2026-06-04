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
 *  ЛОГІКА РОБОТИ:
 *  ┌─────────────────────────────────────────────┐
 *  │  Датчик 1 → сухо? → Помпа 1 ON (5с)        │
 *  │           → перевірка → ще сухо? → знову    │
 *  │           → волого?   → Датчик 2            │
 *  │  Датчик 2 → сухо? → Помпа 2 ON (5с)        │
 *  │           → перевірка → ще сухо? → знову    │
 *  │           → волого?   → Датчик 3            │
 *  │  ...і так по колу                           │
 *  └─────────────────────────────────────────────┘
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
 *  └──────────────────────────────────────────────┘
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// =================== WI-FI ===================
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// =================== КАЛІБРУВАННЯ ДАТЧИКІВ ===================
const int WET_ADC = 140;
const int DRY_ADC = 298;

// =================== ПІНИ МУЛЬТИПЛЕКСОРА ===================
const int S0  = 5; //D1  (GPIO5)     ok                  
const int S1  = 4; // D2.   ok
const int S2  = 14;  // D5
const int S3  = 12; // D6
const int SIG = A0;

// =================== ПІНИ ПОМП ===================
const int pumpPins[4] = {13, 15, 16, 2}; // D7, D8, D0, D4

// =================== ПАРАМЕТРИ ===================
const int DRY_THRESHOLD = 170; // ADC поріг сухості

// ┌─────────────────────────────────────────────────────────┐
// │  ЧАС РОБОТИ ПОМПИ за один цикл поливу (мс)             │
// │  Якщо після поливу ще сухо — помпа вмикається знову    │
// └─────────────────────────────────────────────────────────┘
const unsigned long PUMP_DURATION[4] = {
  5000,   // Помпа 1 — 5 секунд за один цикл
  5000,   // Помпа 2
  5000,   // Помпа 3
  5000    // Помпа 4
};

// ┌─────────────────────────────────────────────────────────┐
// │  ПАУЗА МІЖ ПЕРЕВІРКАМИ після поливу (мс)               │
// │  Час щоб вода вбралась в ґрунт перед повторною         │
// │  перевіркою датчика                                     │
// └─────────────────────────────────────────────────────────┘
const unsigned long RECHECK_DELAY[4] = {
  3000,   // Датчик 1 — 3 секунди після поливу
  3000,   // Датчик 2
  3000,   // Датчик 3
  3000    // Датчик 4
};

// ┌─────────────────────────────────────────────────────────┐
// │  ПАУЗА МІЖ ДАТЧИКАМИ (мс)                              │
// │  Час між переходом від одного датчика до наступного    │
// └─────────────────────────────────────────────────────────┘
const unsigned long SENSOR_DELAY = 1000; // 1 секунда між датчиками

// =================== СТАНИ СИСТЕМИ ===================
enum State {
  STATE_CHECK,    // перевірка датчика
  STATE_PUMP,     // помпа працює
  STATE_RECHECK,  // пауза після поливу перед повторною перевіркою
  STATE_NEXT      // перехід до наступного датчика
};

// =================== ЗМІННІ ===================
int  moisture[4]    = {0, 0, 0, 0};
bool pumpState[4]   = {false, false, false, false};
int  pumpCount[4]   = {0, 0, 0, 0}; // скільки разів поливали
unsigned long startTime   = 0;
bool wifiReady = false;

// Змінні стану машини
State        currentState = STATE_CHECK;
int          currentSensor = 0;       // поточний датчик (0–3)
unsigned long stateTimer  = 0;        // таймер поточного стану

ESP8266WebServer server(80);

// =============================================
//  МУЛЬТИПЛЕКСОР CD74HC4067
// =============================================
int readMoisture(int ch) {
  digitalWrite(S0, (ch & 0x01) ? HIGH : LOW);
  digitalWrite(S1, (ch & 0x02) ? HIGH : LOW);
  digitalWrite(S2, (ch & 0x04) ? HIGH : LOW);
  digitalWrite(S3, (ch & 0x08) ? HIGH : LOW);
  delay(5);
  return analogRead(SIG);
}

// =============================================
//  ЗУПИНИТИ ВСІ ПОМПИ
// =============================================
void stopAllPumps() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(pumpPins[i], LOW);
    pumpState[i] = false;
  }
}

// =============================================
//  ГОЛОВНА МАШИНА СТАНІВ
//  Послідовна перевірка датчиків по черзі
// =============================================
void runStateMachine() {
  int i = currentSensor;

  switch (currentState) {

    // ----- СТАН: ПЕРЕВІРКА ДАТЧИКА -----
    case STATE_CHECK: {
      moisture[i] = readMoisture(i);
      int pct = map(moisture[i], DRY_ADC, WET_ADC, 0, 100);
      pct = constrain(pct, 0, 100);
      Serial.printf("[Датчик %d] ADC=%d (%d%%)", i + 1, moisture[i], pct);

      if (moisture[i] > DRY_THRESHOLD) {
        // СУХО — вмикаємо помпу
        Serial.printf(" → СУХО! Вмикаємо помпу %d\n", i + 1);
        pumpState[i] = true;
        pumpCount[i]++;
        digitalWrite(pumpPins[i], HIGH);
        stateTimer   = millis();
        currentState = STATE_PUMP;
      } else {
        // ВОЛОГО — переходимо до наступного датчика
        Serial.println(" → ВОЛОГО, переходимо далі");
        stateTimer   = millis();
        currentState = STATE_NEXT;
      }
      break;
    }

    // ----- СТАН: ПОМПА ПРАЦЮЄ -----
    case STATE_PUMP: {
      if (millis() - stateTimer >= PUMP_DURATION[i]) {
        // Час поливу вийшов — вимикаємо помпу
        digitalWrite(pumpPins[i], LOW);
        pumpState[i] = false;
        Serial.printf("[Помпа %d] вимкнена, чекаємо %lus перед перевіркою\n",
                      i + 1, RECHECK_DELAY[i] / 1000);
        stateTimer   = millis();
        currentState = STATE_RECHECK;
      }
      break;
    }

    // ----- СТАН: ПАУЗА ПІСЛЯ ПОЛИВУ -----
    case STATE_RECHECK: {
      if (millis() - stateTimer >= RECHECK_DELAY[i]) {
        // Пауза минула — перевіряємо датчик знову
        Serial.printf("[Датчик %d] повторна перевірка...\n", i + 1);
        currentState = STATE_CHECK;
      }
      break;
    }

    // ----- СТАН: ПЕРЕХІД ДО НАСТУПНОГО ДАТЧИКА -----
    case STATE_NEXT: {
      if (millis() - stateTimer >= SENSOR_DELAY) {
        currentSensor = (currentSensor + 1) % 4; // 0→1→2→3→0
        Serial.printf("[Система] Переходимо до датчика %d\n",
                      currentSensor + 1);
        currentState = STATE_CHECK;
      }
      break;
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
           s / 3600, (s % 3600) / 60, s % 60);
  return String(b);
}

String getStateName() {
  switch (currentState) {
    case STATE_CHECK:   return "Перевірка";
    case STATE_PUMP:    return "Полив";
    case STATE_RECHECK: return "Пауза після поливу";
    case STATE_NEXT:    return "Перехід";
    default:            return "—";
  }
}

void handleRoot() {
  String html = F("<!DOCTYPE html><html lang='uk'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='2'>"
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
    ".active{border:2px solid #43a047}"
    ".card{background:#132b14;border-radius:12px;padding:14px;"
    "margin-bottom:10px;border:2px solid transparent}"
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
    ".state{font-size:11px;color:#78909c;margin-top:4px}"
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
  html += "<p><span class='lbl'>Активний датчик: </span><span class='val'>"
        + String(currentSensor + 1) + "</span></p>";
  html += "<p><span class='lbl'>Стан системи: </span><span class='val'>"
        + getStateName() + "</span></p>";
  html += "</div>";

  for (int i = 0; i < 4; i++) {
    int pct = map(moisture[i], DRY_ADC, WET_ADC, 0, 100);
    pct = constrain(pct, 0, 100);
    bool isDry = moisture[i] > DRY_THRESHOLD;
    bool isActive = (i == currentSensor);

    html += "<div class='card" + String(isActive ? " active" : "") + "'>";
    html += "<div class='ct'>"
          + String(isActive ? "&#9654; " : "&#9711; ")
          + "Рослина " + String(i + 1)
          + " <span style='font-size:12px;color:#66bb6a;font-weight:normal'>"
          + "GPIO" + String(pumpPins[i]) + "</span>"
          + (isActive ? " <span style='font-size:11px;color:#fff;background:#1b5e20;"
                        "padding:2px 6px;border-radius:4px'>"
                        + getStateName() + "</span>" : "")
          + "</div>";
    html += "<div class='row'><span>Вологість ADC:</span>"
            "<span><b>" + String(moisture[i]) + "</b>/1023</span></div>";
    html += "<div class='row'><span>Вологість:</span>"
            "<span><b>" + String(pct) + "%</b></span></div>";
    html += "<div class='row'><span>Стан:</span>"
            "<span class='" + String(isDry ? "s-dry" : "s-wet") + "'>"
          + (isDry ? "&#9888; СУХО" : "&#10003; ВОЛОГО") + "</span></div>";
    html += "<div class='row'><span>Помпа:</span>"
            "<span class='" + String(pumpState[i] ? "p-on" : "p-off") + "'>"
          + (pumpState[i] ? "&#128167; ПРАЦЮЄ" : "&#9898; вимкнена")
          + "</span></div>";
    html += "<div class='row'><span>Кількість поливів:</span>"
            "<span><b>" + String(pumpCount[i]) + "</b></span></div>";
    html += "<div class='bar-bg'><div class='bar "
          + String(isDry ? "dry" : "wet")
          + "' style='width:" + String(pct) + "%'></div></div>";
    html += "</div>";
  }

  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleStatus() {
  String j = "{\"uptime\":\"" + getUptime() + "\","
             "\"wifi\":"         + String(wifiReady ? "true" : "false") + ","
             "\"currentSensor\":" + String(currentSensor + 1) + ","
             "\"state\":\"" + getStateName() + "\","
             "\"sensors\":[";
  for (int i = 0; i < 4; i++) {
    if (i) j += ",";
    int pct = constrain(map(moisture[i], DRY_ADC, WET_ADC, 0, 100), 0, 100);
    j += "{\"id\":"        + String(i + 1)
       + ",\"gpio\":"      + String(pumpPins[i])
       + ",\"adc\":"       + String(moisture[i])
       + ",\"pct\":"       + String(pct)
       + ",\"dry\":"       + String(moisture[i] > DRY_THRESHOLD ? "true" : "false")
       + ",\"pump\":"      + String(pumpState[i] ? "true" : "false")
       + ",\"pumpCount\":" + String(pumpCount[i]) + "}";
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

  // Помпи — спочатку LOW потім OUTPUT
  for (int i = 0; i < 4; i++) {
    digitalWrite(pumpPins[i], LOW);
    pinMode(pumpPins[i], OUTPUT);
    digitalWrite(pumpPins[i], LOW);
  }

  // Мультиплексор
  pinMode(S0, OUTPUT); digitalWrite(S0, LOW);
  pinMode(S1, OUTPUT); digitalWrite(S1, LOW);
  pinMode(S2, OUTPUT); digitalWrite(S2, LOW);
  pinMode(S3, OUTPUT); digitalWrite(S3, LOW);

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print(F("Wi-Fi"));
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    Serial.println("\nIP: " + WiFi.localIP().toString());
  } else {
    Serial.println(F("\nWi-Fi недоступний — автономний режим"));
  }

  server.on("/",       handleRoot);
  server.on("/status", handleStatus);
  server.begin();

  Serial.println(F("Система готова!"));
  Serial.printf("Полив: %lus | Пауза: %lus | Між датчиками: %lus\n\n",
                PUMP_DURATION[0] / 1000,
                RECHECK_DELAY[0] / 1000,
                SENSOR_DELAY / 1000);
}

// =============================================
//  LOOP
// =============================================
void loop() {
  server.handleClient();
  runStateMachine();

  if (!wifiReady && WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    Serial.println("Wi-Fi OK: " + WiFi.localIP().toString());
  }
}
