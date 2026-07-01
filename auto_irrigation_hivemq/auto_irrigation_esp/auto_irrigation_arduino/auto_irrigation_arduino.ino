/*
 * ============================================================
 * АВТОМАТИЧНА ТА РУЧНА СИСТЕМА ПОЛИВУ РОСЛИН (ХМАРНА ВЕРСІЯ MQTT)
 * Точно відповідає схемі Altium Designer та сайту на Render
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>  // Переконайся, що бібліотека встановлена в Arduino IDE
#include <ArduinoJson.h>  // Для пакування даних у JSON для сайту

// =================== НАЛАШТУВАННЯ WI-FI ===================
const char* ssid     = "НАЗВА ВАШОГО ВАЙ ФАЙ";
const char* password = "ПАРОЛЬ ВАШОГО ВАЙ ФАЙ";

// =================== НАЛАШТУВАННЯ HIVEMQ CLOUD ===================
const char* mqtt_server = "ПОСИЛАННЯ НА СЕРВЕР.s1.eu.hivemq.cloud";
const int mqtt_port     = 8883; // ДЛЯ ПЛАТИ ПОРТ 8883 — АБСОЛЮТНО ПРАВИЛЬНО!
const char* mqtt_user   = "НАЗВА ПЛАТИ"; 
const char* mqtt_pass   = "ПАРОЛЬ ПЛАТИ"; // Пароль від брокера

const char* topic_status = "my_irr_sys_2026/status";
const char* topic_cmd    = "my_irr_sys_2026/cmd";

// =================== ПІНИ МУЛЬТИПЛЕКСОРА ===================
const int S0  = 5;  // D1 (GPIO5)
const int S1  = 4;  // D2 (GPIO4)
const int S2  = 14; // D5 (GPIO14)
const int S3  = 12; // D6 (GPIO12)
const int SIG = A0;

// =================== ПІНИ ПОМП ===================
const int pumpPins[4] = {13, 15, 16, 2}; // D7, D8, D0, D4

// =================== ПАРАМЕТРИ СИСТЕМИ ===================
const int WET_ADC = 140;
const int DRY_ADC = 298;
const int DRY_THRESHOLD = 190; // Поріг сухості

// Структура для зберігання даних про рослину
struct SensorData {
  int id;
  int gpio;
  int adc;
  int pct;
  bool pump;
  int pumpCount;
};

SensorData plants[4] = {
  {1, 13, 0, 0, false, 0},
  {2, 15, 0, 0, false, 0},
  {3, 16, 0, 0, false, 0},
  {4, 2,  0, 0, false, 0}
};

// Стан системи
bool isAutoMode = true;
int currentSensor = 1;
String systemState = "Ініціалізація...";

// Черга для ручного поливу
bool manualPumpFlags[4] = {false, false, false, false};

// Машина станів автоматики
enum State { STATE_CHECK, STATE_PUMP, STATE_RECHECK };
State currentState = STATE_CHECK;
unsigned long stateTimer = 0;

unsigned long lastMqttReport = 0;

// Ініціалізація захищеного клієнта Wi-Fi та MQTT
WiFiClientSecure espClient;
PubSubClient client(espClient);

// Функція вибору каналу мультиплексора
void selectChannel(int channel) {
  digitalWrite(S0, (channel & 1) ? HIGH : LOW);
  digitalWrite(S1, (channel & 2) ? HIGH : LOW);
  digitalWrite(S2, (channel & 4) ? HIGH : LOW);
  digitalWrite(S3, (channel & 8) ? HIGH : LOW);
  delay(10); // Пауза для стабілізації сигналу
}

// Функція зчитування показників датчика
void readSensor(int index) {
  selectChannel(index); // Датчики на каналах 0, 1, 2, 3
  int raw = analogRead(SIG);
  plants[index].adc = raw;
  
  // Перевід в % (мапування)
  int percent = map(raw, DRY_ADC, WET_ADC, 0, 100);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  plants[index].pct = percent;
}

// Формування та відправка JSON-пакету в хмару для сайту
void sendJsonStatus() {
  JsonDocument doc; // Для версії ArduinoJson v7
  
  doc["auto"] = isAutoMode;
  doc["currentSensor"] = currentSensor;
  doc["state"] = systemState;
  
  // Формуємо красивий Uptime (гг:хх:сс)
  unsigned long sec = millis() / 1000;
  char upTimeStr[16];
  sprintf(upTimeStr, "%02lu:%02lu:%02lu", (sec / 3600) % 24, (sec / 60) % 60, sec % 60);
  doc["uptime"] = upTimeStr;

  JsonArray array = doc["sensors"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject sObj = array.add<JsonObject>();
    sObj["id"] = plants[i].id;
    sObj["gpio"] = plants[i].gpio;
    sObj["adc"] = plants[i].adc;
    sObj["pct"] = plants[i].pct;
    sObj["pump"] = plants[i].pump;
    sObj["pumpCount"] = plants[i].pumpCount;
  }

  char buffer[1024];
  serializeJson(doc, buffer);
  client.publish(topic_status, buffer);
  Serial.println(F("Телеметрію відправлено в хмару HiveMQ!"));
}

// Обробка команд від сайту (кнопки міняють стан тут)
void mqttCallback(char* topic, byte* payload, unsigned length) {
  Serial.print(F("Отримано команду з сайту: "));
  JsonDocument doc;
  deserializeJson(doc, payload, length);

  String action = doc["action"].as<String>();
  
  if (action == "toggleMode") {
    isAutoMode = !isAutoMode;
    systemState = isAutoMode ? "Автоматичний режим" : "Ручний режим";
    Serial.println("Перемкнено режим. Авто: " + String(isAutoMode));
    
    // Якщо перемкнули в ручний — гасимо всі автоматичні помпи
    if (!isAutoMode) {
      for(int i=0; i<4; i++) {
        digitalWrite(pumpPins[i], LOW);
        plants[i].pump = false;
      }
    }
    sendJsonStatus();
  } 
  else if (action == "pump" && !isAutoMode) {
    int plantId = doc["id"].as<int>();
    if (plantId >= 1 && plantId <= 4) {
      manualPumpFlags[plantId - 1] = true;
      Serial.println("Запит на ручний полив Рослини №" + String(plantId));
    }
  }
}

// Перепідключення до MQTT
void reconnectMQTT() {
  while (!client.connected() && WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Спроба підключення плати (esp_board) до HiveMQ..."));
    String clientId = "ESP8266-Irrigation-" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println(F("УСПІШНО підключено до брокера порт 8883!"));
      client.subscribe(topic_cmd);
      sendJsonStatus();
    } else {
      Serial.print(F("Помилка підключення, код стану="));
      Serial.print(client.state());
      Serial.println(F(" Повтор через 5 сек..."));
      delay(5000);
    }
  }
}

// Логіка Автоматичного режиму (Машина станів)
void runStateMachine() {
  if (!isAutoMode) return;

  switch (currentState) {
    case STATE_CHECK:
      systemState = "Перевірка рослини №" + String(currentSensor);
      readSensor(currentSensor - 1);

      if (plants[currentSensor - 1].adc > DRY_THRESHOLD) {
        Serial.println("Рослина " + String(currentSensor) + " СУХА! Вмикаю помпу.");
        digitalWrite(pumpPins[currentSensor - 1], HIGH);
        plants[currentSensor - 1].pump = true;
        plants[currentSensor - 1].pumpCount++;
        stateTimer = millis();
        currentState = STATE_PUMP;
      } else {
        // Якщо волога — йдемо до наступної
        currentSensor = (currentSensor % 4) + 1;
        stateTimer = millis();
        // Робимо невелику паузу перед наступною перевіркою
        currentState = STATE_RECHECK;
      }
      sendJsonStatus();
      break;

    case STATE_PUMP:
      systemState = "Поливається рослина №" + String(currentSensor);
      if (millis() - stateTimer >= 5000) { // Полив 5 секунд
        digitalWrite(pumpPins[currentSensor - 1], LOW);
        plants[currentSensor - 1].pump = false;
        Serial.println("Полив завершено. Очікування всотування.");
        stateTimer = millis();
        currentState = STATE_RECHECK;
        sendJsonStatus();
      }
      break;

    case STATE_RECHECK:
      systemState = "Очікування стабілізації...";
      if (millis() - stateTimer >= 3000) { // Пауза 3 секунди
        if (plants[currentSensor - 1].pump == false) {
          // Тільки якщо полив закінчився, переходимо до наступної рослини
          currentSensor = (currentSensor % 4) + 1;
        }
        currentState = STATE_CHECK;
      }
      break;
  }
}

// Логіка Ручного режиму
void handleManualPumps() {
  if (isAutoMode) return;

  for (int i = 0; i < 4; i++) {
    if (manualPumpFlags[i]) {
      manualPumpFlags[i] = false; // Скидаємо прапорець
      systemState = "Ручний полив рослини №" + String(i + 1);
      
      digitalWrite(pumpPins[i], HIGH);
      plants[i].pump = true;
      plants[i].pumpCount++;
      sendJsonStatus();
      
      delay(5000); // Полив у ручному режимі 5 сек
      
      digitalWrite(pumpPins[i], LOW);
      plants[i].pump = false;
      systemState = "Ручний режим";
      sendJsonStatus();
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Налаштування пінів мультиплексора
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  
  // Налаштування пінів помп
  for(int i = 0; i < 4; i++) {
    pinMode(pumpPins[i], OUTPUT);
    digitalWrite(pumpPins[i], LOW); // Вимикаємо помпи на старті
  }

  // Підключення до Wi-Fi
  Serial.print(F("Підключення до Wi-Fi..."));
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi підключено! IP: " + WiFi.localIP().toString());

  // Налаштування SSL для HiveMQ Cloud (ігноруємо сертифікат для простоти)
  espClient.setInsecure();

  // Налаштування MQTT клієнта
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  
  // Збільшуємо буфер пакета під наш JSON
  client.setBufferSize(1024); 
}

void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();

  runStateMachine();
  handleManualPumps();

  // Періодичне зчитування датчиків у ручному режимі та оновлення сайту
  if (!isAutoMode) {
    static unsigned long lastRead = 0;
    if (millis() - lastRead > 3000) { // Раз на 3 секунди
      lastRead = millis();
      for(int i = 0; i < 4; i++) {
        readSensor(i);
      }
      sendJsonStatus();
    }
  }
}