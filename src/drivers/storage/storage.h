#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <Arduino.h>

// config files

// default settings
#ifndef HAN
#define DEFAULT_SSID		"NerdMinerAP"
#else
#define DEFAULT_SSID		"HanSoloAP"
#endif
#define DEFAULT_WIFIPW		"MineYourCoins"
#define DEFAULT_POOLURL		"public-pool.io"
#define DEFAULT_POOLPASS	"x"
#define DEFAULT_WALLETID	"yourBtcAddress"
#define DEFAULT_POOLPORT	21496
#define DEFAULT_TIMEZONE	2
#define DEFAULT_INVERTCOLORS	false
#define DEFAULT_BRIGHTNESS	250

// Hardcoded config for fast debugging (only when DEBUG_HARDCODED_CONFIG is defined)
// Edit these values for your testing needs, then add -D DEBUG_HARDCODED_CONFIG to your build_flags
#ifdef DEBUG_HARDCODED_CONFIG
#define DEBUG_WIFI_SSID         "Vodafone-C01270070"      // Your WiFi network name
#define DEBUG_WIFI_PASSWORD     "Bolo2022"  // Your WiFi password
#define DEBUG_POOL_URL          "public-pool.io"    // Mining pool URL
#define DEBUG_POOL_PORT         21496               // Mining pool port
#define DEBUG_BTC_WALLET        "bc1qgvvwktl0l593eukc58m4ezs44gqa2xhunqyevu"    // Your Bitcoin wallet address
#define DEBUG_POOL_PASSWORD     "x"                 // Pool password (usually "x")
#define DEBUG_TIMEZONE          2                   // Timezone offset from UTC
#define DEBUG_INVERT_COLORS     false               // Invert display colors
#define DEBUG_BRIGHTNESS        200                 // Screen brightness (0-255)
#endif

// JSON config files
#define JSON_CONFIG_FILE	"/config.json"

// JSON config file SD card (for user interaction, readme.md)
#define JSON_KEY_SSID		"SSID"
#define JSON_KEY_PASW		"WifiPW"
#define JSON_KEY_POOLURL	"PoolUrl"
#define JSON_KEY_POOLPASS	"PoolPassword"
#define JSON_KEY_WALLETID	"BtcWallet"
#define JSON_KEY_POOLPORT	"PoolPort"
#define JSON_KEY_TIMEZONE	"Timezone"
#define JSON_KEY_INVCOLOR	"invertColors"
#define JSON_KEY_BRIGHTNESS	"Brightness"

// JSON config file SPIFFS (different for backward compatibility with existing devices)
#define JSON_SPIFFS_KEY_POOLURL		"poolString"
#define JSON_SPIFFS_KEY_POOLPORT	"portNumber"
#define JSON_SPIFFS_KEY_POOLPASS	"poolPassword"
#define JSON_SPIFFS_KEY_WALLETID	"btcString"
#define JSON_SPIFFS_KEY_TIMEZONE	"gmtZone"
#define JSON_SPIFFS_KEY_INVCOLOR	"invertColors"
#define JSON_SPIFFS_KEY_BRIGHTNESS	"Brightness"

// settings
struct TSettings
{
	String WifiSSID{ DEFAULT_SSID };
	String WifiPW{ DEFAULT_WIFIPW };
	String PoolAddress{ DEFAULT_POOLURL };
	char BtcWallet[80]{ DEFAULT_WALLETID };
	char PoolPassword[80]{ DEFAULT_POOLPASS };
	int PoolPort{ DEFAULT_POOLPORT };
	int Timezone{ DEFAULT_TIMEZONE };
	bool invertColors{ DEFAULT_INVERTCOLORS };
	int Brightness{ DEFAULT_BRIGHTNESS };
};

#endif // _STORAGE_H_