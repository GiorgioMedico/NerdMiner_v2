#include "logging.h"
#define ESP_DRD_USE_SPIFFS true

// Include Libraries
//#include ".h"

#include <WiFi.h>

#include <WiFiManager.h>

#include "wManager.h"
#include "monitor.h"
#include "drivers/displays/display.h"
#include "drivers/storage/SDCard.h"
#include "drivers/storage/nvMemory.h"
#include "drivers/storage/storage.h"
#include "mining.h"
#include "timeconst.h"

#include <ArduinoJson.h>
#include <esp_flash.h>


// Flag for saving data
bool shouldSaveConfig = false;

// Variables to hold data from custom textboxes
TSettings Settings;

// Define WiFiManager Object
WiFiManager wm;
extern monitor_data mMonitor;

nvMemory nvMem;

extern SDCard SDCrd;

String readCustomAPName() {
    DEBUG_SERIAL_PRINTLN("DEBUG: Attempting to read custom AP name from flash at 0x3F0000...");
    
    // Leer directamente desde flash
    const size_t DATA_SIZE = 128;
    uint8_t buffer[DATA_SIZE];
    memset(buffer, 0, DATA_SIZE); // Clear buffer
    
    // Leer desde 0x3F0000
    esp_err_t result = esp_flash_read(NULL, buffer, 0x3F0000, DATA_SIZE);
    if (result != ESP_OK) {
        DEBUG_SERIAL_PRINTF("DEBUG: Flash read error: %s\n", esp_err_to_name(result));
        return "";
    }
    
    DEBUG_SERIAL_PRINTLN("DEBUG: Successfully read from flash");
    String data = String((char*)buffer);
    
    // Debug: show raw data read
    DEBUG_SERIAL_PRINTF("DEBUG: Raw flash data: '%s'\n", data.c_str());
    
    if (data.startsWith("WEBFLASHER_CONFIG:")) {
        DEBUG_SERIAL_PRINTLN("DEBUG: Found WEBFLASHER_CONFIG marker");
        String jsonPart = data.substring(18); // Después del marcador "WEBFLASHER_CONFIG:"
        
        DEBUG_SERIAL_PRINTF("DEBUG: JSON part: '%s'\n", jsonPart.c_str());
        
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, jsonPart);
        
        if (error == DeserializationError::Ok) {
            DEBUG_SERIAL_PRINTLN("DEBUG: JSON parsed successfully");
            
            if (doc.containsKey("apname")) {
                String customAP = doc["apname"].as<String>();
                customAP.trim();
                
                if (customAP.length() > 0 && customAP.length() < 32) {
                    DEBUG_SERIAL_PRINTF("✅ Custom AP name from webflasher: %s\n", customAP.c_str());
                    return customAP;
                } else {
                    DEBUG_SERIAL_PRINTF("DEBUG: AP name invalid length: %d\n", customAP.length());
                }
            } else {
                DEBUG_SERIAL_PRINTLN("DEBUG: 'apname' key not found in JSON");
            }
        } else {
            DEBUG_SERIAL_PRINTF("DEBUG: JSON parse error: %s\n", error.c_str());
        }
    } else {
        DEBUG_SERIAL_PRINTLN("DEBUG: WEBFLASHER_CONFIG marker not found - no custom config");
    }
    
    DEBUG_SERIAL_PRINTLN("DEBUG: Using default AP name");
    return "";
}

void saveConfigCallback()
// Callback notifying us of the need to save configuration
{
    DEBUG_SERIAL_PRINTLN("Should save config");
    shouldSaveConfig = true;    
    //wm.setConfigPortalBlocking(false);
}

/* void saveParamsCallback()
// Callback notifying us of the need to save configuration
{
    DEBUG_SERIAL_PRINTLN("Should save config");
    shouldSaveConfig = true;
    nvMem.saveConfig(&Settings);
} */

void configModeCallback(WiFiManager* myWiFiManager)
// Called when config mode launched
{
    DEBUG_SERIAL_PRINTLN("Entered Configuration Mode");
    drawSetupScreen();
    DEBUG_SERIAL_PRINT("Config SSID: ");
    DEBUG_SERIAL_PRINTLN(myWiFiManager->getConfigPortalSSID());

    DEBUG_SERIAL_PRINT("Config IP Address: ");
    DEBUG_SERIAL_PRINTLN(WiFi.softAPIP());
}

void reset_configuration()
{
    DEBUG_SERIAL_PRINTLN("Erasing Config, restarting");
    nvMem.deleteConfig();
    wm.resetSettings();
    ESP.restart();
}

void init_WifiManager()
{
#ifdef MONITOR_SPEED
    Serial.begin(MONITOR_SPEED);
#else
    Serial.begin(115200);
#endif //MONITOR_SPEED
    //Serial.setTxTimeoutMs(10);
    
    // Check for custom AP name from flasher config, otherwise use default
    String customAPName = readCustomAPName();
    const char* apName = customAPName.length() > 0 ? customAPName.c_str() : DEFAULT_SSID;

    //Init pin 15 to eneble 5V external power (LilyGo bug)
#ifdef PIN_ENABLE5V
    pinMode(PIN_ENABLE5V, OUTPUT);
    digitalWrite(PIN_ENABLE5V, HIGH);
#endif

    // Change to true when testing to force configuration every time we run
    bool forceConfig = false;

#ifdef DEBUG_HARDCODED_CONFIG
    // ========== DEBUG MODE: Use hardcoded configuration ==========
    // This bypasses NVS, SD card, and WiFi portal for fast debugging
    DEBUG_SERIAL_PRINTLN("========================================");
    DEBUG_SERIAL_PRINTLN("DEBUG_HARDCODED_CONFIG enabled!");
    DEBUG_SERIAL_PRINTLN("Using hardcoded configuration...");
    DEBUG_SERIAL_PRINTLN("========================================");

    // Load hardcoded settings
    Settings.WifiSSID = DEBUG_WIFI_SSID;
    Settings.WifiPW = DEBUG_WIFI_PASSWORD;
    Settings.PoolAddress = DEBUG_POOL_URL;
    Settings.PoolPort = DEBUG_POOL_PORT;
    strncpy(Settings.BtcWallet, DEBUG_BTC_WALLET, sizeof(Settings.BtcWallet));
    strncpy(Settings.PoolPassword, DEBUG_POOL_PASSWORD, sizeof(Settings.PoolPassword));
    Settings.Timezone = DEBUG_TIMEZONE;
    Settings.invertColors = DEBUG_INVERT_COLORS;
    Settings.Brightness = DEBUG_BRIGHTNESS;

    // Print loaded config for verification
    DEBUG_SERIAL_PRINTF("WiFi SSID: %s\n", Settings.WifiSSID.c_str());
    DEBUG_SERIAL_PRINTF("Pool: %s:%d\n", Settings.PoolAddress.c_str(), Settings.PoolPort);
    DEBUG_SERIAL_PRINTF("Wallet: %s\n", Settings.BtcWallet);

    // Connect to WiFi directly
    WiFi.mode(WIFI_STA);
    WiFi.begin(Settings.WifiSSID.c_str(), Settings.WifiPW.c_str());

    DEBUG_SERIAL_PRINT("Connecting to WiFi");
    int wifi_retry = 0;
    while (WiFi.status() != WL_CONNECTED && wifi_retry < 30) {
        delay(500);
        DEBUG_SERIAL_PRINT(".");
        wifi_retry++;
    }
    DEBUG_SERIAL_PRINTLN();

    if (WiFi.status() == WL_CONNECTED) {
        DEBUG_SERIAL_PRINTLN("WiFi connected!");
        DEBUG_SERIAL_PRINT("IP address: ");
        DEBUG_SERIAL_PRINTLN(WiFi.localIP());
        mMonitor.NerdStatus.store(NM_Connecting, std::memory_order_release);
    } else {
        DEBUG_SERIAL_PRINTLN("WiFi connection failed!");
        DEBUG_SERIAL_PRINTLN("Check your DEBUG_WIFI_SSID and DEBUG_WIFI_PASSWORD in storage.h");
    }

    return; // Skip all the normal config loading below
#endif

#if defined(PIN_BUTTON_2)
    // Check if button2 is pressed to enter configMode with actual configuration
    if (!digitalRead(PIN_BUTTON_2)) {
        DEBUG_SERIAL_PRINTLN(F("Button pressed to force start config mode"));
        forceConfig = true;
        wm.setBreakAfterConfig(true); //Set to detect config edition and save
    }
#endif
    // Explicitly set WiFi mode
    WiFi.mode(WIFI_STA);

    if (!nvMem.loadConfig(&Settings))
    {
        //No config file on internal flash.
        if (SDCrd.loadConfigFile(&Settings))
        {
            //Config file on SD card.
            SDCrd.SD2nvMemory(&nvMem, &Settings); // reboot on success.          
        }
        else
        {
            //No config file on SD card. Starting wifi config server.
            forceConfig = true;
        }
    };
    
    // Free the memory from SDCard class 
    SDCrd.terminate();
    
    // Reset settings (only for development)
    //wm.resetSettings();

    //Set dark theme
    //wm.setClass("invert"); // dark theme

    // Set config save notify callback
    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setSaveParamsCallback(saveConfigCallback);

    // Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wm.setAPCallback(configModeCallback);    

    //Advanced settings
    wm.setConfigPortalBlocking(false); //Hacemos que el portal no bloquee el firmware
    wm.setConnectTimeout(40); // how long to try to connect for before continuing
    wm.setConfigPortalTimeout(180); // auto close configportal after n seconds
    // wm.setCaptivePortalEnable(false); // disable captive portal redirection
    // wm.setAPClientCheck(true); // avoid timeout if client connected to softap
    //wm.setTimeout(120);
    //wm.setConfigPortalTimeout(120); //seconds

    // Custom elements

    // Text box (String) - 80 characters maximum
    WiFiManagerParameter pool_text_box("Poolurl", "Pool url", Settings.PoolAddress.c_str(), 80);

    // Need to convert numerical input to string to display the default value.
    char convertedValue[6];
    sprintf(convertedValue, "%d", Settings.PoolPort);

    // Text box (Number) - 7 characters maximum
    WiFiManagerParameter port_text_box_num("Poolport", "Pool port", convertedValue, 7);

    // Text box (String) - 80 characters maximum
    //WiFiManagerParameter password_text_box("Poolpassword", "Pool password (Optional)", Settings.PoolPassword, 80);

    // Text box (String) - 80 characters maximum
    WiFiManagerParameter addr_text_box("btcAddress", "Your BTC address", Settings.BtcWallet, 80);

  // Text box (Number) - 2 characters maximum
  char charZone[6];
  sprintf(charZone, "%d", Settings.Timezone);
  WiFiManagerParameter time_text_box_num("TimeZone", "TimeZone fromUTC (-12/+12)", charZone, 3);

  // Text box (String) - 80 characters maximum
  WiFiManagerParameter password_text_box("Poolpassword - Optional", "Pool password", Settings.PoolPassword, 80);

  // Add all defined parameters
  wm.addParameter(&pool_text_box);
  wm.addParameter(&port_text_box_num);
  wm.addParameter(&password_text_box);
  wm.addParameter(&addr_text_box);
  wm.addParameter(&time_text_box_num);
  #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
  char checkboxParams2[32] = "type=\"checkbox\"";
  if (Settings.invertColors)
  {
    strncat(checkboxParams2, " checked", sizeof(checkboxParams2) - strlen(checkboxParams2) - 1);
  }
  WiFiManagerParameter invertColors("inverColors", "Invert Display Colors (if the colors looks weird)", "T", 2, checkboxParams2, WFM_LABEL_AFTER);
  wm.addParameter(&invertColors);
  #endif
  #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
    char brightnessConvValue[2];
    sprintf(brightnessConvValue, "%d", Settings.Brightness);
    // Text box (Number) - 3 characters maximum
    WiFiManagerParameter brightness_text_box_num("Brightness", "Screen backlight Duty Cycle (0-255)", brightnessConvValue, 3);
    wm.addParameter(&brightness_text_box_num);
  #endif

    DEBUG_SERIAL_PRINTLN("AllDone: ");
    if (forceConfig)    
    {
        // Run if we need a configuration
        //No configuramos timeout al modulo
        wm.setConfigPortalBlocking(true); //Hacemos que el portal SI bloquee el firmware
        drawSetupScreen();
        mMonitor.NerdStatus.store(NM_Connecting, std::memory_order_release);
        wm.startConfigPortal(apName, DEFAULT_WIFIPW);

        if (shouldSaveConfig)
        {
            //Could be break forced after edditing, so save new config
            DEBUG_SERIAL_PRINTLN("failed to connect and hit timeout");
            Settings.PoolAddress = pool_text_box.getValue();
            Settings.PoolPort = atoi(port_text_box_num.getValue());
            strncpy(Settings.PoolPassword, password_text_box.getValue(), sizeof(Settings.PoolPassword));
            strncpy(Settings.BtcWallet, addr_text_box.getValue(), sizeof(Settings.BtcWallet));
            Settings.Timezone = atoi(time_text_box_num.getValue());
            #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
                Settings.invertColors = (strncmp(invertColors.getValue(), "T", 1) == 0);
            #endif
            #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
                Settings.Brightness = atoi(brightness_text_box_num.getValue());
            #endif
            nvMem.saveConfig(&Settings);
            delay(3*SECOND_MS);
            //reset and try again, or maybe put it to deep sleep
            ESP.restart();            
        };
    }
    else
    {
        //Tratamos de conectar con la configuración inicial ya almacenada
        mMonitor.NerdStatus.store(NM_Connecting, std::memory_order_release);
        // disable captive portal redirection
        wm.setCaptivePortalEnable(true); 
        wm.setConfigPortalBlocking(true);
        wm.setEnableConfigPortal(true);
        // if (!wm.autoConnect(Settings.WifiSSID.c_str(), Settings.WifiPW.c_str()))
        if (!wm.autoConnect(apName, DEFAULT_WIFIPW))
        {
            DEBUG_SERIAL_PRINTLN("Failed to connect to configured WIFI, and hit timeout");
            if (shouldSaveConfig) {
                // Save new config            
                Settings.PoolAddress = pool_text_box.getValue();
                Settings.PoolPort = atoi(port_text_box_num.getValue());
                strncpy(Settings.PoolPassword, password_text_box.getValue(), sizeof(Settings.PoolPassword));
                strncpy(Settings.BtcWallet, addr_text_box.getValue(), sizeof(Settings.BtcWallet));
                Settings.Timezone = atoi(time_text_box_num.getValue());
                #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
                Settings.invertColors = (strncmp(invertColors.getValue(), "T", 1) == 0);
                #endif
                #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
                Settings.Brightness = atoi(brightness_text_box_num.getValue());
                #endif
                nvMem.saveConfig(&Settings);
                vTaskDelay(2000 / portTICK_PERIOD_MS);      
            }        
            ESP.restart();                            
        } 
    }
    
    //Conectado a la red Wifi
    if (WiFi.status() == WL_CONNECTED) {
        //tft.pushImage(0, 0, MinerWidth, MinerHeight, MinerScreen);
        DEBUG_SERIAL_PRINTLN("");
        DEBUG_SERIAL_PRINTLN("WiFi connected");
        DEBUG_SERIAL_PRINT("IP address: ");
        DEBUG_SERIAL_PRINTLN(WiFi.localIP());


        // Lets deal with the user config values

        // Copy the string value
        Settings.PoolAddress = pool_text_box.getValue();
        //strncpy(Settings.PoolAddress, pool_text_box.getValue(), sizeof(Settings.PoolAddress));
        DEBUG_SERIAL_PRINT("PoolString: ");
        DEBUG_SERIAL_PRINTLN(Settings.PoolAddress);

        //Convert the number value
        Settings.PoolPort = atoi(port_text_box_num.getValue());
        DEBUG_SERIAL_PRINT("portNumber: ");
        DEBUG_SERIAL_PRINTLN(Settings.PoolPort);

        // Copy the string value
        strncpy(Settings.PoolPassword, password_text_box.getValue(), sizeof(Settings.PoolPassword));
        DEBUG_SERIAL_PRINT("poolPassword: ");
        DEBUG_SERIAL_PRINTLN(Settings.PoolPassword);

        // Copy the string value
        strncpy(Settings.BtcWallet, addr_text_box.getValue(), sizeof(Settings.BtcWallet));
        DEBUG_SERIAL_PRINT("btcString: ");
        DEBUG_SERIAL_PRINTLN(Settings.BtcWallet);

        //Convert the number value
        Settings.Timezone = atoi(time_text_box_num.getValue());
        DEBUG_SERIAL_PRINT("TimeZone fromUTC: ");
        DEBUG_SERIAL_PRINTLN(Settings.Timezone);

        #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
        Settings.invertColors = (strncmp(invertColors.getValue(), "T", 1) == 0);
        DEBUG_SERIAL_PRINT("Invert Colors: ");
        DEBUG_SERIAL_PRINTLN(Settings.invertColors);        
        #endif

        #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
        Settings.Brightness = atoi(brightness_text_box_num.getValue());
        DEBUG_SERIAL_PRINT("Brightness: ");
        DEBUG_SERIAL_PRINTLN(Settings.Brightness);
        #endif

    }

    // Save the custom parameters to FS
    if (shouldSaveConfig)
    {
        nvMem.saveConfig(&Settings);
        #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
         if (Settings.invertColors) ESP.restart();                
        #endif
        #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
        if (Settings.Brightness != 250) ESP.restart();
        #endif
    }
}

//----------------- MAIN PROCESS WIFI MANAGER --------------
int oldStatus = 0;

void wifiManagerProcess() {

    wm.process(); // avoid delays() in loop when non-blocking and other long running code

    int newStatus = WiFi.status();
    if (newStatus != oldStatus) {
        if (newStatus == WL_CONNECTED) {
            DEBUG_SERIAL_PRINTLN("CONNECTED - Current ip: " + WiFi.localIP().toString());
        } else {
            DEBUG_SERIAL_PRINT("[Error] - current status: ");
            DEBUG_SERIAL_PRINTLN(newStatus);
        }
        oldStatus = newStatus;
    }
}
