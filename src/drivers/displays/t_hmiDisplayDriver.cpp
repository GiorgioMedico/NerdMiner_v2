#include "displayDriver.h"
#include "logging.h"

#ifdef T_HMI_DISPLAY
#include <FS.h>
#include <xpt2046.h> // https://github.com/liangyingy/arduino_xpt2046_library
#include <TFT_eSPI.h>
#include <TFT_eTouch.h>
#include "media/images_320_170.h"
#include "media/images_bottom_320_70.h"
#include "media/myFonts.h"
#include "media/Free_Fonts.h"
#include "version.h"
#include "monitor.h"
#include "OpenFontRender.h"
#ifdef TOUCH_ENABLE
#include "TouchHandler.h"
#endif
#include <Arduino.h>
#include <esp_adc_cal.h>

#define WIDTH 320
#define HEIGHT 240

OpenFontRender render;
TFT_eSPI tft = TFT_eSPI();                  // Invoke library, pins defined in User_Setup.h
TFT_eSprite background = TFT_eSprite(&tft); // Invoke library sprite

#ifdef TOUCH_ENABLE
TouchHandler touchHandler = TouchHandler(tft, ETOUCH_CS, TOUCH_IRQ, SPI);
#endif

bool showbtcprice = false;

unsigned int lowerScreen = 1;

extern void switchToNextScreen();
extern monitor_data mMonitor;
extern pool_data pData;
extern DisplayDriver *currentDisplayDriver;

void toggleBottomScreen() { lowerScreen = 3 - lowerScreen; }


uint32_t readAdcVoltage(int pin) {
    esp_adc_cal_characteristics_t adc_chars;

    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    return esp_adc_cal_raw_to_voltage(analogRead(pin), &adc_chars);
}

void printBatteryVoltage() {
    uint32_t voltage = readAdcVoltage(BAT_ADC_PIN) * 2;
    DEBUG_SERIAL_PRINT("Battery voltage: ");
    DEBUG_SERIAL_PRINTLN((float)voltage/1000);
    delay(500);
}

void t_hmiDisplay_Init(void)
{
  pinMode(PWR_ON_PIN, OUTPUT);
  digitalWrite(PWR_ON_PIN, HIGH);

  delay(10);
  DEBUG_SERIAL_PRINTLN(F("Turn on the main power"));

  pinMode(PWR_EN_PIN, OUTPUT);
  digitalWrite(PWR_EN_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);                 // Swap the colour byte order when rendering
  background.createSprite(WIDTH,HEIGHT); // Background Sprite
  background.setSwapBytes(true);
  render.setDrawer(background);  // Link drawing object to background instance (so font will be rendered on background)
  render.setLineSpaceRatio(0.9); 
  // Load the font and check it can be read OK
  // if (render.loadFont(NotoSans_Bold, sizeof(NotoSans_Bold))) {
  if (render.loadFont(DigitalNumbers, sizeof(DigitalNumbers)))
  {
    DEBUG_SERIAL_PRINTLN("Initialise error");
    return;
  }

  #ifdef TOUCH_ENABLE
  DEBUG_SERIAL_PRINTLN(F("Initialize the touch screen"));
  touchHandler.begin(HEIGHT, WIDTH);
  touchHandler.setScreenSwitchCallback(switchToNextScreen);
  touchHandler.setScreenSwitchAltCallback(toggleBottomScreen);
  #endif

  DEBUG_SERIAL_PRINTLN(F("Turn on the LCD backlight"));
  pinMode(LED_PIN, OUTPUT);
  pinMode(BK_LIGHT_PIN, OUTPUT);
  digitalWrite(BK_LIGHT_PIN, BK_LIGHT_LEVEL);
  pData.bestDifficulty = "0";
  pData.workersHash = "0";
  pData.workersCount = 0;
}

void t_hmiDisplay_AlternateScreenState(void)
{
  int screen_state = digitalRead(TFT_BL);
  DEBUG_SERIAL_PRINTLN("Switching display state");
  digitalWrite(TFT_BL, !screen_state);
}

void t_hmiDisplay_AlternateRotation(void)
{
   tft.getRotation() == 1 ? tft.setRotation(3) : tft.setRotation(1);
}


void printPoolData()
{
  // DEBUG_SERIAL_PRINT("\nPool ============ Free Heap:");
  // DEBUG_SERIAL_PRINTLN(ESP.getFreeHeap()); 
  pData = getPoolData();

  background.pushImage(0, 170, 320, 70, bottonPoolScreen);
  render.setLineSpaceRatio(1);
  
  render.setFontSize(24);
  render.drawString(String(pData.workersCount).c_str(), 146, 170+35, TFT_BLACK);

  render.setFontSize(18);
  render.drawString(pData.workersHash.c_str(), 216, 170+34, TFT_BLACK);
  render.drawString(pData.bestDifficulty.c_str(), 5, 170+34, TFT_BLACK);
  // printBatteryVoltage();
}



void t_hmiDisplay_MinerScreen(unsigned long mElapsed)
{
  if (digitalRead(TFT_BL)) return;

  mining_data data = getMiningData(mElapsed);
  background.pushImage(0, 0, MinerWidth, 170, MinerScreen);
  DEBUG_SERIAL_PRINTF(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str());
   // Hashrate
  render.setFontSize(35);
  render.setCursor(19, 118);
  render.setFontColor(TFT_BLACK);
  render.rdrawString(data.currentHashRate.c_str(), 118, 114, TFT_BLACK);
  
  // Total hashes
  render.setFontSize(18);
  render.rdrawString(data.totalMHashes.c_str(), 268, 138, TFT_BLACK);
  // Block templates
  render.setFontSize(18);
  render.drawString(data.templates.c_str(), 186, 20, 0xDEDB);
  // Best diff
  render.drawString(data.bestDiff.c_str(), 186, 48, 0xDEDB);
  // 32Bit shares
  render.setFontSize(18);
  render.drawString(data.completedShares.c_str(), 186, 76, 0xDEDB);
  // Hores
  render.setFontSize(14);
  render.rdrawString(data.timeMining.c_str(), 315, 104, 0xDEDB);

  // Valid Blocks
  render.setFontSize(24);
  render.drawString(data.valids.c_str(), 285, 56, 0xDEDB);

  // Print Temp
  render.setFontSize(10);
  render.rdrawString(data.temp.c_str(), 239, 1, TFT_BLACK);

  render.setFontSize(4);
  render.rdrawString(String(0).c_str(), 244, 3, TFT_BLACK);

  // Print Hour
  render.setFontSize(10);
  render.rdrawString(data.currentTime.c_str(), 286, 1, TFT_BLACK);

  printPoolData();

  // Push prepared background to screen
  background.pushSprite(0, 0);
}



void t_hmiDisplay_LoadingScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  // tft.pushImage(0, 0, initWidth, initHeight, initScreen);
  tft.pushImage(0, 0, initWidth, initHeight, initScreen);
  tft.setTextColor(TFT_BLACK);
  tft.drawString(CURRENT_VERSION, 24, 147, FONT2);
  delay(2000);
  tft.fillScreen(TFT_BLACK);
  // tft.pushImage(0, 0, initWidth, initHeight, MinerScreen);
  tft.pushImage(0, 0, initWidth, 170, MinerScreen);
  tft.pushImage(0, 170, initWidth, 70, bottonPoolScreen);
  if (showbtcprice)
  {
    // blackout title
    tft.fillRect( 105, 170,  110, 20, TFT_BLACK);
  }
}


void t_hmiDisplay_SetupScreen(void)
{
  tft.pushImage(0, 0, setupModeWidth, setupModeHeight, setupModeScreen);
}

void t_hmiDisplay_AnimateCurrentScreen(unsigned long frame)
{
}

void t_hmiDisplay_DoLedStuff(unsigned long frame)
{
}

// Only Mining Screen enabled - other screens commented out to eliminate HTTP API overhead
// Removed screens: t_hmiDisplay_ClockScreen, t_hmiDisplay_GlobalHashScreen (code simplification)
CyclicScreenFunction t_hmiDisplayCyclicScreens[] = {t_hmiDisplay_MinerScreen};
// Disabled: t_hmiDisplay_ClockScreen, t_hmiDisplay_GlobalHashScreen, t_hmiDisplay_BTCprice

DisplayDriver t_hmiDisplayDriver = {
    t_hmiDisplay_Init,
    t_hmiDisplay_AlternateScreenState,
    t_hmiDisplay_AlternateRotation,
    t_hmiDisplay_LoadingScreen,
    t_hmiDisplay_SetupScreen,
    t_hmiDisplayCyclicScreens,
    t_hmiDisplay_AnimateCurrentScreen,
    t_hmiDisplay_DoLedStuff,
    SCREENS_ARRAY_SIZE(t_hmiDisplayCyclicScreens),
    0,
    WIDTH,
    HEIGHT};
#endif
