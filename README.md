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




