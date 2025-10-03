#include "displayDriver.h"
#include "logging.h"

#ifdef ST7735S_DISPLAY

#include <TFT_eSPI.h>
#include "media/images_128_128.h"
#include "media/myFonts.h"
#include "media/Free_Fonts.h"
#include "version.h"
#include "monitor.h"
#include "OpenFontRender.h"
#include "rotation.h"

#define WIDTH 128
#define HEIGHT 128

OpenFontRender render;
TFT_eSPI tft = TFT_eSPI();                  // Invoke library, pins defined in User_Setup.h
TFT_eSprite background = TFT_eSprite(&tft); // Invoke library sprite

void sp_kcDisplay_Init(void)
{
  tft.init();
  tft.setRotation(PORTRAIT);
  tft.setSwapBytes(true);                 // Swap the colour byte order when rendering
  background.createSprite(WIDTH, HEIGHT); // Background Sprite
  background.setSwapBytes(true);
  render.setDrawer(background);  // Link drawing object to background instance (so font will be rendered on background)
  render.setLineSpaceRatio(0.9); // Espaciado entre texto

  // Load the font and check it can be read OK
  // if (render.loadFont(NotoSans_Bold, sizeof(NotoSans_Bold))) {
  if (render.loadFont(DigitalNumbers, sizeof(DigitalNumbers)))
  {
    DEBUG_SERIAL_PRINTLN("Initialise error");
    return;
  }
}

void sp_kcDisplay_AlternateScreenState(void)
{
  //int screen_state = digitalRead(TFT_BL);
  DEBUG_SERIAL_PRINTLN("Switching display state");
  //digitalWrite(TFT_BL, !screen_state);
}

void sp_kcDisplay_AlternateRotation(void)
{
  tft.setRotation( rotationRight(tft.getRotation()) );
}

void sp_kcDisplay_MinerScreen(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);

  // Print background screen
  background.pushImage(0, 0, MinerWidth, MinerHeight, MinerScreen);

  DEBUG_SERIAL_PRINTF(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str());

    //Hashrate
    render.setFontSize(32);
    render.setCursor(0, 0);
    render.setFontColor(TFT_BLACK);    
    render.rdrawString(data.currentHashRate.c_str(), 114, 24, TFT_DARKGREY);

    //Valid Blocks
    render.setFontSize(22);
    render.drawString(data.valids.c_str(), 15, 92, TFT_BLACK);
    
    //Mining Time
    char timeMining[15]; 
    unsigned long secElapsed = millis() / 1000;
    int days = secElapsed / 86400; 
    int hours = (secElapsed - (days * 86400)) / 3600;                                                        //Number of seconds in an hour
    int mins = (secElapsed - (days * 86400) - (hours * 3600)) / 60;                                              //Remove the number of hours and calculate the minutes.
    int secs = secElapsed - (days * 86400) - (hours * 3600) - (mins * 60);   
    sprintf(timeMining, "%01d  %02d:%02d:%02d", days, hours, mins, secs);
    render.setFontSize(10);
    render.setCursor(0, 10);        
    render.rdrawString(String(timeMining).c_str(), 124, 0, TFT_BLACK);

    //Push prepared background to screen
    background.pushSprite(0,0);
}


void sp_kcDisplay_LoadingScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 0, initWidth, initHeight, initScreen);
  tft.setTextColor(TFT_GOLD);
  tft.drawString(CURRENT_VERSION, 2, 100, FONT2); 
}

void sp_kcDisplay_SetupScreen(void)
{
  tft.pushImage(0, 0, setupModeWidth, setupModeHeight, setupModeScreen);
}

void sp_kcDisplay_AnimateCurrentScreen(unsigned long frame)
{
}

void sp_kcDisplay_DoLedStuff(unsigned long frame)
{
}

// Removed screens: sp_kcDisplay_ClockScreen (code simplification)
CyclicScreenFunction sp_kcDisplayCyclicScreens[] = {sp_kcDisplay_MinerScreen};

DisplayDriver sp_kcDisplayDriver = {
    sp_kcDisplay_Init,
    sp_kcDisplay_AlternateScreenState,
    sp_kcDisplay_AlternateRotation,
    sp_kcDisplay_LoadingScreen,
    sp_kcDisplay_SetupScreen,
    sp_kcDisplayCyclicScreens,
    sp_kcDisplay_AnimateCurrentScreen,
    sp_kcDisplay_DoLedStuff,
    SCREENS_ARRAY_SIZE(sp_kcDisplayCyclicScreens),
    0,
    WIDTH,
    HEIGHT};
#endif
