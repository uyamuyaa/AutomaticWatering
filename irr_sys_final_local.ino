/*
 * ============================================================
 * АВТОМАТИЧНА ТА РУЧНА СИСТЕМА ПОЛИВУ РОСЛИН
 * Точно відповідає схемі Altium Designer
 *
 * Оновлено: Додано перемикач режимів (Авто/Ручний) + ручний полив кнопками
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// =================== WI-FI ===================
const char* ssid     = "361a";
const char* password = "78860872";
IPAddress lastIP;
unsigned long lastIpPrint = 0;

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
const int DRY_THRESHOLD = 150; // ADC поріг сухості

const unsigned long PUMP_DURATION[4] = { 5000, 5000, 5000, 5000 };
const unsigned long RECHECK_DELAY[4] = { 3000, 3000, 3000, 3000 };
const unsigned long SENSOR_DELAY = 1000; 

// =================== СТАНИ СИСТЕМИ ===================
enum State {
  STATE_CHECK,    
  STATE_PUMP,     
  STATE_RECHECK,  
  STATE_NEXT      
};

// =================== ЗМІННІ ===================
int  moisture[4]    = {0, 0, 0, 0};
bool pumpState[4]   = {false, false, false, false};
int  pumpCount[4]   = {0, 0, 0, 0}; 
unsigned long startTime   = 0;
bool wifiReady = false;

// РЕЖИМ РОБОТИ: true = Автоматичний, false = Ручний
bool isAutoMode = true; 

// Для ручного таймера помп
unsigned long manualPumpTimer[4] = {0, 0, 0, 0};

State         currentState = STATE_CHECK;
int           currentSensor = 0;       
unsigned long stateTimer   = 0;        

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
//  ГОЛОВНА МАШИНА СТАНІВ (Працює в АВТО режимі)
// =============================================
void runStateMachine() {
  // Якщо режим ручний, автоматична машина станів відпочиває
  if (!isAutoMode) return; 

  int i = currentSensor;

  switch (currentState) {
    case STATE_CHECK: {
      moisture[i] = readMoisture(i);
      int pct = map(moisture[i], DRY_ADC, WET_ADC, 0, 100);
      pct = constrain(pct, 0, 100);
      Serial.printf("[Датчик %d] ADC=%d (%d%%)", i + 1, moisture[i], pct);

      if (moisture[i] > DRY_THRESHOLD) {
        Serial.printf(" → СУХО! Вмикаємо помпу %d\n", i + 1);

        Serial.printf(
            "START PUMP %d GPIO=%d ADC=%d\n",
            i + 1,
            pumpPins[i],
            moisture[i]
        );

        pumpState[i] = true;
        pumpCount[i]++;
        digitalWrite(pumpPins[i], HIGH);
        stateTimer   = millis();
        currentState = STATE_PUMP;
      } else {
        Serial.println(" → ВОЛОГО, переходимо далі");
        stateTimer   = millis();
        currentState = STATE_NEXT;
      }
      break;
    }

    case STATE_PUMP: {
      if (millis() - stateTimer >= PUMP_DURATION[i]) {
        digitalWrite(pumpPins[i], LOW);
        pumpState[i] = false;
        Serial.printf("[Помпа %d] вимкнена, чекаємо %lus перед перевіркою\n",
                      i + 1, RECHECK_DELAY[i] / 1000);
        stateTimer   = millis();
        currentState = STATE_RECHECK;
      }
      break;
    }

    case STATE_RECHECK: {
      if (millis() - stateTimer >= RECHECK_DELAY[i]) {
        Serial.printf("[Датчик %d] повторна перевірка...\n", i + 1);
        currentState = STATE_CHECK;
      }
      break;
    }

    case STATE_NEXT: {
      if (millis() - stateTimer >= SENSOR_DELAY) {
        currentSensor = (currentSensor + 1) % 4; 
        Serial.printf("[Система] Переходимо до датчика %d\n", currentSensor + 1);
        currentState = STATE_CHECK;
      }
      break;
    }
  }
}

// =============================================
//  ОБРОБКА ТАЙМЕРІВ ДЛЯ РУЧНОГО РЕЖИМУ
// =============================================
void handleManualPumps() {
  if (isAutoMode) return;

  for (int i = 0; i < 4; i++) {
    if (pumpState[i]) {
      if (millis() - manualPumpTimer[i] >= PUMP_DURATION[i]) {
        digitalWrite(pumpPins[i], LOW);
        pumpState[i] = false;
        Serial.printf("[Ручний режим] Помпа %d вимкнена за таймером\n", i + 1);   
      }
    }
  }
}

// =============================================
//  ВЕБ-ІНТЕРФЕЙС
// =============================================
String getUptime() {
  unsigned long s = (millis() - startTime) / 1000;
  char b[20];
  snprintf(b, sizeof(b), "%02lu:%02lu:%02lu", s / 3600, (s % 3600) / 60, s % 60);
  return String(b);
}

String getStateName() {
  if (!isAutoMode) return "Ручне керування";
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
    "<title>Система поливу</title>"
    "<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:Arial,sans-serif;background:#e8f5e9;min-height:100vh;"
"background-image:url('https://images.unsplash.com/photo-1466692476868-aef1dfb1e735');"
"background-size:cover;background-position:center;background-repeat:no-repeat;color:#222}"

"header{position:fixed;top:0;left:0;width:100%;"
"background:linear-gradient(90deg,#041f10,#0a5f35);color:#fff;"
"padding:30px;text-align:center;font-size:28px;font-weight:bold;"
"box-shadow:0 4px 12px rgba(0,0,0,.3);z-index:1000}"

".wrap{max-width:860px;margin:120px auto 80px auto;padding:30px}"

".mode-box,.info{background:rgba(255,255,255,.95);"
"border-radius:16px;padding:20px;margin-bottom:20px;"
"box-shadow:0 6px 20px rgba(0,0,0,.15)}"

".mode-box{display:flex;justify-content:space-between;"
"align-items:center;flex-wrap:wrap;gap:15px}"

".btn-mode{padding:14px 20px;border:none;border-radius:12px;"
"cursor:pointer;font-size:15px;font-weight:bold;color:white;"
"background:linear-gradient(135deg,#3aa86b,#2f8f5f,#25724c);"
"transition:.2s}"

".btn-mode:hover{transform:scale(1.03)}"
".btn-mode.manual{background:linear-gradient(135deg,#d32f2f,#b71c1c,#8e0000)}"

".lbl{font-weight:bold;color:#0b6b3a}"
".val{font-weight:bold}"

"#cards{display:grid;"
"grid-template-columns:repeat(auto-fit,minmax(320px,1fr));"
"gap:20px}"

".card{background:white;padding:25px;border-radius:16px;"
"box-shadow:0 6px 20px rgba(0,0,0,.15);"
"transition:.3s;border:3px solid transparent}"

".card.active{border-color:#66bb6a;transform:translateY(-2px)}"

".ct{font-size:20px;font-weight:bold;color:#0b6b3a;"
"margin-bottom:15px}"

".row{display:flex;justify-content:space-between;"
"margin:8px 0;font-size:14px}"

".s-wet{color:#43a047;font-weight:bold}"
".s-dry{color:#e53935;font-weight:bold}"
".p-on{color:#e53935;font-weight:bold}"
".p-off{color:#43a047;font-weight:bold}"

".bar-bg{background:#ddd;height:12px;border-radius:10px;"
"overflow:hidden;margin-top:12px}"

".bar{height:12px;border-radius:10px;transition:.5s}"

".wet{background:linear-gradient(90deg,#43a047,#2e7d32)}"
".dry{background:linear-gradient(90deg,#ef5350,#c62828)}"

".btn-pump{width:100%;padding:14px;margin-top:14px;"
"border:none;border-radius:12px;color:white;"
"font-size:15px;font-weight:bold;cursor:pointer;"
"background:linear-gradient(135deg,#3aa86b,#2f8f5f,#25724c);"
"transition:.2s}"

".btn-pump:hover{transform:scale(1.03)}"
".btn-pump:disabled{background:#999;cursor:not-allowed}"

"footer{position:fixed;bottom:0;left:0;width:100%;"
"background:linear-gradient(90deg,#0a5f35,#041f10);"
"color:white;text-align:center;padding:20px;"
"box-shadow:0 -4px 12px rgba(0,0,0,.3);z-index:1000}"

"@media(max-width:768px){"
"header{font-size:22px;padding:20px}"
".wrap{padding:15px;margin:100px auto 70px auto}"
".mode-box{flex-direction:column;align-items:stretch}"
"#cards{grid-template-columns:1fr}"
".card{padding:18px}"
".ct{font-size:18px}"
"}"

"</style></head><body><div class='wrap'>"
    "<header>🌿 СИСТЕМА ПОЛИВУ РОСЛИН</header>"
    
    // Блок перемикання режимів
    "<div class='mode-box'>"
    "<span>Поточний режим: <b id='modeTxt' class='val'>Автоматичний</b></span>"
    "<button id='modeBtn' class='btn-mode' onclick='toggleMode()'>Змінити режим</button>"
    "</div>"

    "<div class='info'>"
    "<p><span class='lbl'>Wi-Fi: </span><span class='val' id='wf'>&#8987; Перевірка...</span></p>"
    "<p><span class='lbl'>Час роботи: </span><span class='val' id='up'>00:00:00</span></p>"
    "<p><span class='lbl'>Активний датчик: </span><span class='val' id='ac'>—</span></p>"
    "<p><span class='lbl'>Стан системи: </span><span class='val' id='st'>—</span></p>"
    "</div>"
    "<div id='cards'></div>"
    "</div>"
      "<footer>© 2026 Smart Irrigation System</footer>"
      "<script>"
    "const dryTh = 170;" 
    "let autoMode = true;"
    
    "function toggleMode(){"
    "fetch('/toggleMode').then(r=>r.json()).then(d=>{autoMode=d.auto; updateUI(d);});"
    "}"
    
    "function startManualPump(id){"
    "fetch('/pump?id='+id);"
    "}"

    "function updateData(){"
    "fetch('/status').then(r=>r.json()).then(data=>{autoMode=data.auto; updateUI(data);});"
    "}"
    
    "function updateUI(data){"
    "document.getElementById('wf').innerHTML=data.wifi?'&#10003; Підключено':'&#9888; Автономно';"
    "document.getElementById('up').innerText=data.uptime;"
    "document.getElementById('ac').innerText=data.auto ? data.currentSensor : '—';"
    "document.getElementById('st').innerText=data.state;"
    
    "const mBtn = document.getElementById('modeBtn');"
    "const mTxt = document.getElementById('modeTxt');"
    "if(data.auto){"
    "mTxt.innerText='Автоматичний'; mTxt.style.color='#66bb6a';"
    "mBtn.innerText='Ввімкнути Ручний'; mBtn.className='btn-mode';"
    "}else{"
    "mTxt.innerText='Ручний'; mTxt.style.color='#ef5350';"
    "mBtn.innerText='Ввімкнути Авто'; mBtn.className='btn-mode manual';"
    "}"
    
    "let h='';"
    "data.sensors.forEach((s,i)=>{"
    "let isActive=data.auto && ((i+1)==data.currentSensor);"
    "let isDry=s.adc > dryTh;" 
    "h+='<div class=\"card'+(isActive?' active':'')+'\">';"
    "h+='<div class=\"ct\">'+(isActive?'&#9654; ':'&#9711; ')+'Рослина '+s.id+' <span style=\"font-size:12px;color:#a5d6a7;font-weight:normal\">GPIO'+s.gpio+'</span></div>';"
    "h+='<div class=\"row\"><span>Вологість ADC:</span><span><b>'+s.adc+'</b>/1023</span></div>';"
    "h+='<div class=\"row\"><span>Вологість:</span><span><b>'+s.pct+'%</b></span></div>';"
    "h+='<div class=\"row\"><span>Стан:</span><span class=\"'+(isDry?'s-dry':'s-wet')+'\">'+(isDry?'&#9888; СУХО':'&#10003; ВОЛОГО')+'</span></div>';"
    "h+='<div class=\"row\"><span>Помпа:</span><span class=\"'+(s.pump?'p-on':'p-off')+'\">'+(s.pump?'&#128167; ПРАЦЮЄ':'&#9898; вимкнена')+'</span></div>';"
    "h+='<div class=\"row\"><span>Кількість поливів:</span><span><b>'+s.pumpCount+'</b></span></div>';"
    "h+='<div class=\"bar-bg\"><div class=\"bar '+(isDry?'dry':'wet')+'\" style=\"width:'+s.pct+'%\"></div></div>';"
    
    // Кнопка поливу активна лише в ручному режимі та коли помпа зараз не качає
    "if(!data.auto){"
    "h+='<button class=\"btn-pump\" '+(s.pump?'disabled':'')+' onclick=\"startManualPump('+s.id+')\">'+(s.pump?'Поливається...':'Полити 5с')+'</button>';"
    "}"
    
    "h+='</div>';"
    "});"
    "document.getElementById('cards').innerHTML=h;"
    "}"
    
    "setInterval(updateData,1000);updateData();"
    "</script>"
    "</body></html>");

  server.send(200, "text/html; charset=utf-8", html);
}

void handleStatus() {
  String j = "{\"uptime\":\"" + getUptime() + "\","
             "\"wifi\":"         + String(wifiReady ? "true" : "false") + ","
             "\"auto\":"         + String(isAutoMode ? "true" : "false") + ","
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
       + ",\"pump\":"      + String(pumpState[i] ? "true" : "false")
       + ",\"pumpCount\":" + String(pumpCount[i]) + "}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// =============================================
//  ОБРОБКА ПЕРЕМИКАННЯ РЕЖИМІВ
// =============================================
void handleToggleMode() {
  isAutoMode = !isAutoMode;
  stopAllPumps(); // Безпека: гасимо всі помпи під час зміни режимів
  
  // Якщо повернулися в автоматику — скидаємо машину станів у дефолт на перевірку
  if (isAutoMode) {
    currentState = STATE_CHECK;
    stateTimer = millis();
  }
  
  handleStatus(); // Віддаємо оновлений статус клієнту
}

// =============================================
//  ОБРОБКА РУЧНОГО ЗАПУСКУ ПОМПИ
// =============================================
void handleManualPump() {
  if (isAutoMode || !server.hasArg("id")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  
  int id = server.arg("id").toInt() - 1; // Переводимо ID (1-4) в індекс масиву (0-3)
  
  if (id >= 0 && id < 4 && !pumpState[id]) {
    pumpState[id] = true;
    pumpCount[id]++;
    digitalWrite(pumpPins[id], HIGH);
    manualPumpTimer[id] = millis();
    Serial.printf("[Ручний режим] Запуск помпи %d на %lus\n", id + 1, PUMP_DURATION[id]/1000);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Invalid ID or Pump Busy");
  }
}

// =============================================
//  SETUP
// =============================================
void setup() {

ArduinoOTA.setHostname("IrrigationESP");

ArduinoOTA.onStart([]() {
  Serial.println("OTA Start");
});

ArduinoOTA.onEnd([]() {
  Serial.println("\nOTA End");
});

ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  Serial.printf("Progress: %u%%\r", (progress * 100) / total);
});

ArduinoOTA.onError([](ota_error_t error) {
  Serial.printf("Error[%u]\n", error);
});

ArduinoOTA.begin();

Serial.println("OTA Ready");

  Serial.begin(115200);
  startTime = millis();
  Serial.println(F("\n=== СИСТЕМА ПОЛИВУ РОСЛИН ==="));

  for (int i = 0; i < 4; i++) {
    digitalWrite(pumpPins[i], LOW);
    pinMode(pumpPins[i], OUTPUT);
    digitalWrite(pumpPins[i], LOW);
  }

  pinMode(S0, OUTPUT); digitalWrite(S0, LOW);
  pinMode(S1, OUTPUT); digitalWrite(S1, LOW);
  pinMode(S2, OUTPUT); digitalWrite(S2, LOW);
  pinMode(S3, OUTPUT); digitalWrite(S3, LOW);

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
  lastIP = WiFi.localIP();

  Serial.println("\nWiFi підключено!");
  Serial.print("IP address: ");
  Serial.println(lastIP);
} else {
  Serial.println(F("\nWi-Fi недоступний — автономний режим"));
}

  server.on("/",           handleRoot);
  server.on("/status",     handleStatus);
  server.on("/toggleMode", handleToggleMode); // Нове правило для зміни режимів
  server.on("/pump",       handleManualPump); // Нове правило для запуску помп руцями
  server.begin();

  Serial.println(F("Система готова!"));
}

// =============================================
//  LOOP
// =============================================
void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  // Кожна функція відповідає лише за свій режим роботи
  runStateMachine();
  handleManualPumps();

    if (millis() - lastIpPrint > 10000) { // кожні 10 секунд
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      lastIpPrint = millis();
    }
  // Окремо підтягуємо зчитування показників датчиків у ручному режимі, 
  // щоб дані на сайті оновлювалися, навіть коли автоматика вимкнена.
  if (!isAutoMode) {
    static unsigned long lastRead = 0;
    if (millis() - lastRead > 200) { // Швидке кругове опитування раз на 200мс
      static int ch = 0;
      moisture[ch] = readMoisture(ch);
      ch = (ch + 1) % 4;
      lastRead = millis();
    }
  }

  if (!wifiReady && WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    Serial.println("Wi-Fi OK: " + WiFi.localIP().toString());

  if (WiFi.status() == WL_CONNECTED) {
  if (WiFi.localIP() != lastIP) {
    lastIP = WiFi.localIP();
    Serial.print("NEW IP: ");
    Serial.println(lastIP);
  }


}
  }
}