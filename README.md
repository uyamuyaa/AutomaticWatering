  # AutomaticWatering
    Setting Up the Automatic Watering System
The work is done in the Arduino IDE  
Load the `irr_sys_local` file  
Tools - > Board - > Boards Manager… - > Install the required board packages - >  
  ●	Arduino AVR Boards by Arduino;  
  ●	Arduino ESP32 Boards by Arduino;  
  ●	Esp32 by Espressif Systems;  
  ●	Esp8266 by ESP8266 Community;  

On the left side of the control panel is the Library Manager. Here, you need to install the libraries required for the project:  
  ●	ArduinoJson by Benoit Blanchon;  
  ● PubSubClient by Nick O'Leary;  

Next, in the board selection field, select the NodeMCU 1.0 (ESP-12E Module) board;   
to the right, select the port to which the board is connected via a cable to the PC(com3, com6, com7 etc.)  

In code lines 17–18 – enter the SSID and password for your local network   
      const char* ssid = “your_network_ssid”;  
      const char* password = “your_network_password”;  
Click the Upload arrow to upload the firmware containing the network settings to the board. The Serial Monitor will display connection details and sensor status, including the board’s network IP address which contains webserver.  


