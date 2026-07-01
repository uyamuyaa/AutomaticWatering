  # AutomaticWatering
    Setting Up the Automatic Watering System
The work is done in the Arduino IDE  
Load the `irr_sys_final_local` file  
Tools - > Board - > Boards Manager… - > Install the required board packages - >      

      ●	Arduino AVR Boards by Arduino;      
      ●	Arduino ESP32 Boards by Arduino;      
      ●	Esp32 by Espressif Systems;      
      ●	Esp8266 by ESP8266 Community;   
      
On the left side of the control panel is the Library Manager. Here, you need to install the libraries required for the project:  

      ● ArduinoJson by Benoit Blanchon;  
      ● PubSubClient by Nick O'Leary;  
Next, in the board selection field, select the NodeMCU 1.0 (ESP-12E Module) board;   
to the right, select the port to which the board is connected via a cable to the PC(com3, com6, com7 etc.)  

In code lines 17–18 – enter the SSID and password for your local network   

            const char* ssid = “your_network_ssid”;  
            const char* password = “your_network_password”;  
            
  Click the Upload arrow to upload the firmware containing the network settings to the board. The Serial Monitor will display connection details and sensor status, including the board’s network IP address which contains webserver. 

  When you visit the IP address, a website appears with information about the connection, operating time, and the overall system status.   
There is a mode-switch button; by default, the system operates in automatic mode, in which it checks the moisture level of each sensor in turn. 

  If the soil is dry, watering begins for 5 seconds, followed by a 3-second delay after watering. The sensor then checks the moisture level again; if it is satisfactory, the system moves on to the next plant; if not, it continues watering. 

  If you switch to manual mode, buttons for 5-second watering cycles appear; watering does not occur automatically, and the sensors continue to check moisture levels sequentially every second. Pressing a button starts a 5-second watering cycle.
  
      ● Below are cards with information about the plant: 
    plant number, 
      ● moisture level in ADC values and as a percentage,
      ● status (wet or dry), 
      ● and the number of waterings for this session.


# Smart IoT Plant Irrigation System (Cloud Edition)

This project is an automated and manual IoT plant irrigation system designed to monitor and water up to 4 plants. It architecture is built around an **ESP8266** microcontroller, a **CD74HC4067** 16-channel analog multiplexer, a cloud-based **HiveMQ MQTT broker**, and a responsive frontend web application hosted on **Render**.

---

##  CRITICAL SECURITY WARNING FOR FUTURE USERS
Before deployment, you **MUST** replace the default credentials in the source code with your own. Leaving default hardcoded credentials poses a severe security risk. 
* **Do not share your custom links with embedded credentials.**
* Read the deployment sections below to find exactly where to locate and update your credentials.

---

## 1. HiveMQ Cloud Setup (MQTT Broker)

HiveMQ Cloud serves as the central data bridge between the hardware board and the web interface.

### Step-by-Step Deployment:
1. Go to the [HiveMQ Cloud Console](https://console.hivemq.cloud/) and sign up for a free account.
2. Create a new free **Serverless Cluster**.
3. Once the cluster is provisioned, go to the **Overview** tab and copy your **Cluster URL** (it looks like `xxxxxxxx.s1.eu.hivemq.cloud`).
4. Navigate to the **Access Management** (or **Credentials**) tab to create credentials:
   * **For the Web Application:** Create a user (e.g., username: `web_app`) and assign a secure password.
   * **For the ESP8266 Board:** Create a second user (e.g., username: `esp_board`) and assign a password.
5. **Permissions:** Ensure both users have **Publish & Subscribe** permissions allowed for the wildcard topic (`#`).

---

## 2. Web Application Deployment (Render)

The frontend interface provides real-time telemetry rendering and remote pump switching using WebSockets over MQTT.

### Step-by-Step Deployment:
1. Push your project frontend files (`index.html`, `app.js`, `style.css`) to a **GitHub** repository.
2. Sign up or log in to [Render](https://render.com/).
3. Click **New +** in the dashboard and select **Static Site**.
4. Connect your GitHub account and select your repository.
5. Configure the deployment settings:
   * **Name:** Choose a unique project name (e.g., `auto-irrigation`).
   * **Build Command:** Leave blank (not required for standard HTML/JS).
   * **Publish Directory:** `.` (or your root folder name if files are in a subdirectory).
6. Click **Deploy Static Site**. Render will generate a live HTTPS URL for your application.

### Where to Find & Change Credentials in Frontend:
Open `app.js` and edit the configuration block at the top:
```javascript
const MQTT_BROKER   = "wss://YOUR_CLUSTER_URL_HERE:8884/mqtt"; // Replace with your HiveMQ URL (Keep port 8884 and wss://)
const DEFAULT_USER  = "YOUR_WEB_APP_USERNAME";                  // Your HiveMQ Web App user
const DEFAULT_PASS  = "YOUR_WEB_APP_PASSWORD";                  // Your HiveMQ Web App password



