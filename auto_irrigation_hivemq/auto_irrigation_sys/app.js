// ================= НАЛАШТУВАННЯ MQTT ХМАРИ =================
// Використовуємо захищений WebSocket-порт (8884) для HiveMQ Cloud
const MQTT_BROKER   = "ПОСИЛАННЯ НА БРОКЕРА";
const TOPIC_STATUS  = "my_irr_sys_2026/status";
const TOPIC_CMD     = "my_irr_sys_2026/cmd";

// Резервні дефолтні дані для сайту, якщо в URL нічого не передано
const DEFAULT_USER  = "ІМ'Я БРОКЕРА ВЕБ-ДОДАТКУ";
const DEFAULT_PASS  = "ПАРОЛЬ БРОКЕРА ВЕБ-ДОДАТКУ";

let mqttClient;

// ================= ІНІЦІАЛІЗАЦІЯ ПРИ СТАРТІ =================
document.addEventListener("DOMContentLoaded", () => {
    const urlParams = new URLSearchParams(window.location.search);
    
    // Чітко зчитуємо параметри 'u' (username) та 'p' (password) з вашого посилання
    const userParam = urlParams.get("u");
    const passParam = urlParams.get("p");

    if (userParam && passParam) {
        console.log("Знайдено параметри автоматичної авторизації в URL...");
        
        // Знаходимо інпути на сторінці та вписуємо туди дані з посилання
        const userInput = document.getElementById("username");
        const passInput = document.getElementById("password");

        if (userInput) userInput.value = userParam;
        if (passInput) passInput.value = passParam;

        // МИТТЄВИЙ АВТОХІД: запускаємо підключення без очікування кліків
        connectToMqtt();
    } else {
        console.log("Параметрів URL не знайдено. Очікування ручного введення користувачем.");
    }
});

// ================= ПІДКЛЮЧЕННЯ ДО HIVEMQ CLOUD =================
function connectToMqtt() {
    console.log("Підключення до HiveMQ Cloud через WebSockets...");
    
    const errorEl = document.getElementById("login-error");
    if (errorEl) errorEl.style.display = "none";

    // Зчитуємо дані з інпутів. Якщо порожні — беремо DEFAULT_USER (web_app)
    const inputUser = document.getElementById("username") ? document.getElementById("username").value.trim() : "";
    const inputPass = document.getElementById("password") ? document.getElementById("password").value.trim() : "";

    const activeUser = inputUser !== "" ? inputUser : DEFAULT_USER;
    const activePass = inputPass !== "" ? inputPass : DEFAULT_PASS;

    console.log(`Сайт авторизується під логіном: ${activeUser}`);

    // Генеруємо випадковий унікальний ID клієнта для сесії браузера
    const clientId = "web_client_" + Math.random().toString(16).substr(2, 8);
    
    mqttClient = mqtt.connect(MQTT_BROKER, {
        clientId: clientId,
        username: activeUser,
        password: activePass,
        clean: true,
        connectTimeout: 5000,
        reconnectPeriod: 2000,
    });

    // Подія: Успішно з'єдналися з хмарою
    mqttClient.on("connect", () => {
        console.log("Сайт успішно підключився до MQTT хмари!");
        
        // Ховаємо вікно входу
        showMainInterface();

        // Підписуємося на топік, куди плата (esp_board) шле дані
        mqttClient.subscribe(TOPIC_STATUS, (err) => {
            if (!err) {
                console.log(`Підписано на топік телеметрії: ${TOPIC_STATUS}`);
                if (document.getElementById("wf")) {
                    document.getElementById("wf").innerText = "🟢 Підключено до хмари";
                    document.getElementById("wf").style.color = "#43a047";
                }
                if (document.getElementById("modeBtn")) {
                    document.getElementById("modeBtn").removeAttribute("disabled");
                }
            }
        });
    });

    // Подія: Отримання повідомлень від плати (esp_board)
    mqttClient.on("message", (topic, payload) => {
        if (topic === TOPIC_STATUS) {
            try {
                const data = JSON.parse(payload.toString());
                console.log("Отримано дані від плати (esp_board):", data);
                updateUI(data); 
            } catch (e) {
                console.error("Помилка парсингу JSON:", e);
            }
        }
    });

    mqttClient.on("error", (err) => {
        console.error("Помилка MQTT підключення:", err);
        if (errorEl) errorEl.style.display = "block";
    });

    mqttClient.on("close", () => {
        console.log("З'єднання з брокером розірвано.");
        if (document.getElementById("wf")) {
            document.getElementById("wf").innerText = "🔴 Відключено";
            document.getElementById("wf").style.color = "#ef5350";
        }
    });
}

function showMainInterface() {
    const loginOverlay = document.getElementById("login-overlay");
    if (loginOverlay) {
        loginOverlay.style.setProperty("display", "none", "important");
    }
}
// ================= ОНОВЛЕННЯ ДАНИХ НА СТОРІНЦІ =================
function updateUI(data) {
    // 1. Загальна інформація
    const uptimeEl = document.getElementById("up"); 
    if (uptimeEl) uptimeEl.innerText = data.uptime || "00:00:00";

    const stateEl = document.getElementById("st");
    if (stateEl) stateEl.innerText = data.state;

    const activeSensorEl = document.getElementById("ac");
    if (activeSensorEl) {
        activeSensorEl.innerText = data.auto ? `Датчик №${data.currentSensor}` : "—";
    }

    const modeBtn = document.getElementById("modeBtn");
    const modeTxt = document.getElementById("modeTxt");

    if (modeTxt && modeBtn) {
        if (data.auto) {
            modeTxt.innerText = "Автоматичний";
            modeTxt.style.color = "#43a047";
            modeBtn.innerText = "Увімкнути Ручний";
            modeBtn.className = "btn-mode";
        } else {
            modeTxt.innerText = "Ручний режим";
            modeTxt.style.color = "#e53935";
            modeBtn.innerText = "Увімкнути Авто";
            modeBtn.className = "btn-mode manual";
        }
    }

    // 2. Оновлення карток рослин
    if (data.sensors && Array.isArray(data.sensors)) {
        const cardsContainer = document.getElementById("cards");
        if (!cardsContainer) return;

        cardsContainer.innerHTML = "";

        data.sensors.forEach(sensor => {
            const isPumpOn = sensor.pump;
            const isDry = sensor.adc > 150; // Поріг сухості з прошивки (DRY_THRESHOLD)
            const isActiveCard = data.auto && (sensor.id == data.currentSensor);
            
            const cardHtml = `
                <div class="card ${isPumpOn || isActiveCard ? 'active' : ''}">
                    <div class="ct">${isActiveCard ? '▶ ' : '⭕ '}Рослина №${sensor.id} <span style="font-size:12px;color:#66bb6a;font-weight:normal">GPIO${sensor.gpio}</span></div>
                    <div class="row">
                        <span class="lbl">Вологість ADC:</span>
                        <span class="val">${sensor.adc} / 1023</span>
                    </div>
                    <div class="row">
                        <span class="lbl">Вологість у %:</span>
                        <span class="val">${sensor.pct}%</span>
                    </div>
                    <div class="row">
                        <span class="lbl">Стан ґрунту:</span>
                        <span class="${isDry ? 's-dry' : 's-wet'}">${isDry ? '⚠ СУХО' : '✓ ВОЛОГО'}</span>
                    </div>
                    <div class="row">
                        <span class="lbl">Стан помпи:</span>
                        <span class="${isPumpOn ? 'p-on' : 'p-off'}">${isPumpOn ? '💧 ПРАЦЮЄ' : '⚪ вимкнена'}</span>
                    </div>
                    <div class="row">
                        <span class="lbl">Кількість поливів:</span>
                        <span class="val">${sensor.pumpCount}</span>
                    </div>
                    
                    <div class="bar-bg">
                        <div class="bar ${isDry ? 'dry' : 'wet'}" style="width: ${sensor.pct}%"></div>
                    </div>
                    
                    ${!data.auto ? `
                    <button class="btn-pump" ${isPumpOn ? 'disabled' : ''} onclick="sendPumpCommand(${sensor.id})">
                        ${isPumpOn ? 'Поливається...' : 'Полити 5с'}
                    </button>
                    ` : ''}
                </div>
            `;
            cardsContainer.insertAdjacentHTML("beforeend", cardHtml);
        });
    }
}

// ================= ВІДПРАВКА КОМАНД У ХМАРУ =================
function toggleMode() {
    if (!mqttClient || !mqttClient.connected) return;
    const command = JSON.stringify({ action: "toggleMode" });
    mqttClient.publish(TOPIC_CMD, command);
}

function sendPumpCommand(plantId) {
    if (!mqttClient || !mqttClient.connected) return;
    const command = JSON.stringify({ action: "pump", id: plantId });
    mqttClient.publish(TOPIC_CMD, command);
}