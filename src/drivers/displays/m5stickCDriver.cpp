#include "displayDriver.h"
#include "logging.h"

#ifdef M5STICKC_DISPLAY

#include <M5StickC.h>

#include "media/images_160_80.h"
#include "media/myFonts.h"
#include "media/Free_Fonts.h"
#include "version.h"
#include "monitor.h"
#include "rotation.h"

#define WIDTH 80
#define HEIGHT 160

#define GRAY 0x632C
#define LIGHTBLUE 0x4C77

int screen_state = 1;

void m5stickCDriver_Init(void)
{
  M5.begin();
  M5.Lcd.setRotation(LANDSCAPE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.fillScreen(BLACK);
  M5.Axp.ScreenBreath(10);  //screen brightness 7-15
}

void m5stickCDriver_AlternateScreenState(void)
{
  if (screen_state==1) {
    M5.Lcd.writecommand(ST7735_DISPOFF);
    M5.Axp.ScreenBreath(0);
    screen_state=0;
  } else {
    M5.Lcd.writecommand(ST7735_DISPON);
    M5.Axp.ScreenBreath(10);
    screen_state=1;
  }
}

void m5stickCDriver_AlternateRotation(void)
{
    M5.Lcd.setRotation( flipRotation(M5.Lcd.getRotation()) );
}

void m5stickCDriver_MinerScreen(unsigned long mElapsed)
{
  if (screen_state == 0) return;

  mining_data data = getMiningData(mElapsed);

  M5.Lcd.drawBitmap(0,0,MinerWidth, MinerHeight, MinerScreen);
  M5.Lcd.setFreeFont(&DSEG7_Classic_Bold_12);
  M5.Lcd.setTextColor(LIGHTBLUE,BLACK);
  M5.Lcd.setCursor(69, 69);
  M5.Lcd.println(String(data.currentHashRate));

  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextColor(GRAY,BLACK);
  M5.Lcd.setCursor(117, 56);
  M5.Lcd.println("kH/s");

  M5.Lcd.setFreeFont(FMB9);
  M5.Lcd.setCursor(81, 22);
  M5.Lcd.println("VALID");

  M5.Lcd.setFreeFont(&DSEG7_Classic_Bold_17);
  M5.Lcd.setTextColor(LIGHTBLUE,BLACK);
  M5.Lcd.setCursor(101, 44);
  M5.Lcd.println(String(data.valids));
  
}


void m5stickCDriver_LoadingScreen(void)
{
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.drawBitmap(0,0,MinerWidth, MinerHeight, MinerScreen);
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextColor(ORANGE,BLACK);
  M5.Lcd.setCursor(100, 10);
  M5.Lcd.println(CURRENT_VERSION);
}

void m5stickCDriver_SetupScreen(void)
{
 
}

void m5stickCDriver_AnimateCurrentScreen(unsigned long frame)
{
}

void m5stickCDriver_DoLedStuff(unsigned long frame)
{
}

// Only Mining Screen enabled - other screens commented out to eliminate HTTP API overhead
// Removed screens: m5stickCDriver_ClockScreen, m5stickCDriver_GlobalHashScreen (code simplification)
CyclicScreenFunction m5stickCDriverCyclicScreens[] = {m5stickCDriver_MinerScreen};
// Disabled: m5stickCDriver_ClockScreen, m5stickCDriver_GlobalHashScreen

DisplayDriver m5stickCDriver = {
    m5stickCDriver_Init,
    m5stickCDriver_AlternateScreenState,
    m5stickCDriver_AlternateRotation,
    m5stickCDriver_LoadingScreen,
    m5stickCDriver_SetupScreen,
    m5stickCDriverCyclicScreens,
    m5stickCDriver_AnimateCurrentScreen,
    m5stickCDriver_DoLedStuff,
    SCREENS_ARRAY_SIZE(m5stickCDriverCyclicScreens),
    0,
    WIDTH,
    HEIGHT};
#endif
