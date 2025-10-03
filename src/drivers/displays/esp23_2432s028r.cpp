#include "displayDriver.h"
#include "logging.h"

#if defined ESP32_2432S028R || ESP32_2432S028_2USB

#include <TFT_eSPI.h>
#ifdef TOUCH_ENABLE
#include <TFT_eTouch.h>
#endif
#include "media/images_320_170.h"
#include "media/images_bottom_320_70.h"
#include "media/myFonts.h"
#include "media/Free_Fonts.h"
#include "version.h"
#include "monitor.h"
#include "OpenFontRender.h"
#include <SPI.h>
#include "rotation.h"
#include "drivers/storage/nvMemory.h"
#include "drivers/storage/storage.h"

#define WIDTH 130 //320
#define HEIGHT 170 

extern nvMemory nvMem;

OpenFontRender render;
TFT_eSPI tft = TFT_eSPI();                  // Invoke library, pins defined in platformio.ini
TFT_eSprite background = TFT_eSprite(&tft); // Invoke library sprite
#ifdef TOUCH_ENABLE
SPIClass hSPI(HSPI);
TFT_eTouch<TFT_eSPI> touch(tft, ETOUCH_CS, 0xFF, hSPI);
#endif 

extern monitor_data mMonitor;
extern pool_data pData;
extern DisplayDriver *currentDisplayDriver;
extern bool invertColors; 
extern TSettings Settings;
bool hasChangedScreen = true;

void getChipInfo(void){
  DEBUG_SERIAL_PRINT("Chip: ");
  DEBUG_SERIAL_PRINTLN(ESP.getChipModel());
  DEBUG_SERIAL_PRINT("ChipRevision: ");
  DEBUG_SERIAL_PRINTLN(ESP.getChipRevision());
  DEBUG_SERIAL_PRINT("Psram size: ");
  DEBUG_SERIAL_PRINT(ESP.getPsramSize() / 1024);
  DEBUG_SERIAL_PRINTLN("KB");
  DEBUG_SERIAL_PRINT("Flash size: ");
  DEBUG_SERIAL_PRINT(ESP.getFlashChipSize() / 1024);
  DEBUG_SERIAL_PRINTLN("KB");
  DEBUG_SERIAL_PRINT("CPU frequency: ");
  DEBUG_SERIAL_PRINT(ESP.getCpuFreqMHz());
  DEBUG_SERIAL_PRINTLN("MHz");  
}

void esp32_2432S028R_Init(void)
{ 
  // getChipInfo();  
  tft.init();
  if (nvMem.loadConfig(&Settings))
    {      
     // DEBUG_SERIAL_PRINT("Invert Colors: ");
     // DEBUG_SERIAL_PRINTLN(Settings.invertColors);  
      invertColors = Settings.invertColors;           
    }  
  tft.invertDisplay(invertColors);
  tft.setRotation(1);    
  tft.setSwapBytes(true); // Swap the colour byte order when rendering
  if (invertColors) {
    tft.writecommand(ILI9341_GAMMASET);
    tft.writedata(2);
    delay(120);
    tft.writecommand(ILI9341_GAMMASET); //Gamma curve selected
    tft.writedata(1);
  }
#ifdef TOUCH_ENABLE
  hSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, ETOUCH_CS);
  touch.init();

  TFT_eTouchBase::Calibation calibation = { 233, 3785, 3731, 120, 2 };
  touch.setCalibration(calibation);
#endif

  // Configuring screen backlight brightness using ledcontrol channel 0.
  // Using 5000Hz in 8bit resolution, which gives 0-255 possible duty cycle setting.
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, Settings.Brightness);
 
  //background.createSprite(WIDTH, HEIGHT); // Background Sprite
  //background.setSwapBytes(true);
  //render.setDrawer(background);  // Link drawing object to background instance (so font will be rendered on background)
  //render.setLineSpaceRatio(0.9); // Espaciado entre texto

  // Load the font and check it can be read OK
  // if (render.loadFont(NotoSans_Bold, sizeof(NotoSans_Bold)))
  if (render.loadFont(DigitalNumbers, sizeof(DigitalNumbers)))
  {
    DEBUG_SERIAL_PRINTLN("Initialise error");
    return;
  }
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_PIN_B, OUTPUT);
  pinMode(LED_PIN_G, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(LED_PIN_B, HIGH);
  digitalWrite(LED_PIN_G, HIGH);
  pData.bestDifficulty = "0";
  pData.workersHash = "0";
  pData.workersCount = 0;
  //DEBUG_SERIAL_PRINTLN("=========== Fim Display ==============") ;
}

void esp32_2432S028R_AlternateScreenState(void)
{
  DEBUG_SERIAL_PRINTLN("Switching display state");
  int screen_state_duty = ledcRead(0);
  // Switching the duty cycle for the ledc channel, where the TFT_BL pin is attached.
  if (screen_state_duty > 0) {
    ledcWrite(0, 0);
  } else {
    ledcWrite(0, Settings.Brightness);
  }
}

void esp32_2432S028R_AlternateRotation(void)
{
  tft.setRotation( flipRotation(tft.getRotation()) );
  hasChangedScreen = true;
}

bool bottomScreenBlue = true;

void printheap(){
  DEBUG_SERIAL_PRINT("$$ Free Heap:");
  DEBUG_SERIAL_PRINTLN(ESP.getFreeHeap()); 
  // DEBUG_SERIAL_PRINTF("### stack WMark usage: %d\n", uxTaskGetStackHighWaterMark(NULL));
}

bool createBackgroundSprite(int16_t wdt, int16_t hgt){  // Set the background and link the render, used multiple times to fit in heap
  background.createSprite(wdt, hgt) ; //Background Sprite
  // printheap();
  if (background.created()) {
      background.setColorDepth(16);
      background.setSwapBytes(true);
      render.setDrawer(background); // Link drawing object to background instance (so font will be rendered on background)
      render.setLineSpaceRatio(0.9);      
  } else {
    DEBUG_SERIAL_PRINTLN("#### Sprite Error ####");
    DEBUG_SERIAL_PRINTF("Size w:%d h:%d \n", wdt, hgt);
    printheap();
  }
  return background.created();
}

extern unsigned long mPoolUpdate;

void printPoolData(){
  if ((hasChangedScreen) || (mPoolUpdate == 0) || (millis() - mPoolUpdate > UPDATE_POOL_min * 60 * 1000)){     
      if (Settings.PoolAddress != "tn.vkbit.com") { 
          pData = getPoolData();             
          background.createSprite(320,50); //Background Sprite
          if (!background.created()) {    
            DEBUG_SERIAL_PRINTLN("###### POOL SPRITE ERROR ######");
          // DEBUG_SERIAL_PRINTF("Pool data W:%d H:%s D:%s\n", pData.workersCount, pData.workersHash, pData.bestDifficulty);
            printheap();        
          }       
          background.setSwapBytes(true);
          if (bottomScreenBlue) {
            background.pushImage(0, -20, 320, 70, bottonPoolScreen);
            tft.pushImage(0,170,320,20,bottonPoolScreen);      
          } else {
            background.pushImage(0, -20, 320, 70, bottonPoolScreen_g);
            tft.pushImage(0,170,320,20,bottonPoolScreen_g);
          }
                
          render.setDrawer(background); // Link drawing object to background instance (so font will be rendered on background)
          render.setLineSpaceRatio(1);
          
          render.setFontSize(24);
          render.cdrawString(String(pData.workersCount).c_str(), 157, 16, TFT_BLACK);
          render.setFontSize(18);
          render.setAlignment(Align::BottomRight);
          render.cdrawString(pData.workersHash.c_str(), 265, 14, TFT_BLACK);
          render.setAlignment(Align::BottomLeft);
          render.cdrawString(pData.bestDifficulty.c_str(), 54, 14, TFT_BLACK);
          background.pushSprite(0,190);      
          background.deleteSprite();
      } else {
        pData.bestDifficulty = "TESTNET";
        pData.workersHash = "TESTNET";
        pData.workersCount = 1;
        tft.fillRect(0,170,320,70, TFT_DARKGREEN);        
        background.createSprite(320,40); //Background Sprite
        background.fillSprite(TFT_DARKGREEN);
          if (!background.created()) {    
            DEBUG_SERIAL_PRINTLN("###### POOL SPRITE ERROR ######");
          // DEBUG_SERIAL_PRINTF("Pool data W:%d H:%s D:%s\n", pData.workersCount, pData.workersHash, pData.bestDifficulty);
            printheap();        
          }
        background.setFreeFont(FF24);
        background.setTextDatum(TL_DATUM);
        background.setTextSize(1);
        background.setTextColor(TFT_WHITE, TFT_DARKGREEN);        
        background.drawString("TESTNET", 50, 0, GFXFF);
        background.pushSprite(0,185);  
        mPoolUpdate = millis();
        DEBUG_SERIAL_PRINTLN("Testnet");
        background.deleteSprite();
      }
  }
}



void esp32_2432S028R_MinerScreen(unsigned long mElapsed)
{
  if (ledcRead(0) == 0) return;

  mining_data data = getMiningData(mElapsed);

  printPoolData();

  if (hasChangedScreen) tft.pushImage(0, 0, initWidth, initHeight, MinerScreen);
    
  hasChangedScreen = false; 
 
  int wdtOffset = 190;
  // Recreate sprite to the right side of the screen
  createBackgroundSprite(WIDTH-5, HEIGHT-7);
  //Print background screen    
  background.pushImage(-190, 0, MinerWidth, MinerHeight, MinerScreen);
  
  // Total hashes
  render.setFontSize(18);
  render.rdrawString(data.totalMHashes.c_str(), 268-wdtOffset, 138, TFT_BLACK);

  // Block templates
  render.setFontSize(18);
  render.setAlignment(Align::TopLeft);
  render.drawString(data.templates.c_str(), 189-wdtOffset, 20, 0xDEDB);
  // Best diff
  render.drawString(data.bestDiff.c_str(), 189-wdtOffset, 48, 0xDEDB);
  // 32Bit shares
  render.setFontSize(18);
  render.drawString(data.completedShares.c_str(), 189-wdtOffset, 76, 0xDEDB);
  // Hores
  render.setFontSize(14);
  render.rdrawString(data.timeMining.c_str(), 315-wdtOffset, 104, 0xDEDB);

  // Valid Blocks
  render.setFontSize(24);
  render.setAlignment(Align::TopCenter);
  render.drawString(data.valids.c_str(), 290-wdtOffset, 56, 0xDEDB);

  // Print Temp
  render.setFontSize(10);
  render.rdrawString(data.temp.c_str(), 239-wdtOffset, 1, TFT_BLACK);

  render.setFontSize(4);
  render.rdrawString(String(0).c_str(), 244-wdtOffset, 3, TFT_BLACK);

  // Print Hour
  render.setFontSize(10);
  render.rdrawString(data.currentTime.c_str(), 286-wdtOffset, 1, TFT_BLACK);

  // Push prepared background to screen
  background.pushSprite(190, 0);

  // Delete sprite to free the memory heap
  background.deleteSprite();   
  // printheap();

   //DEBUG_SERIAL_PRINTLN("=========== Mining Display ==============") ;
  // Create background sprite to print data at once
  createBackgroundSprite(WIDTH-7, HEIGHT-100); // initHeight); //Background Sprite
  //Print background screen    
  background.pushImage(0, -90, MinerWidth, MinerHeight, MinerScreen);

  // Hashrate 
  render.setFontSize(35);
  render.setCursor(19, 118);
  render.setFontColor(TFT_BLACK);
  render.rdrawString(data.currentHashRate.c_str(), 118, 114-90, TFT_BLACK);
  
  // Push prepared background to screen
  background.pushSprite(0, 90);
  
  // Delete sprite to free the memory heap
  background.deleteSprite();  

  DEBUG_SERIAL_PRINTF(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str()); 
   
  #ifdef DEBUG_MEMORY
    // Print heap
    printheap();
  #endif
}


void esp32_2432S028R_LoadingScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 33, initWidth, initHeight, initScreen);
  tft.setTextColor(TFT_BLACK);
  tft.drawString(CURRENT_VERSION, 24, 147, FONT2);
  // delay(2000);
  // tft.fillScreen(TFT_BLACK);
  // tft.pushImage(0, 0, initWidth, initHeight, MinerScreen);
}

void esp32_2432S028R_SetupScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 33, setupModeWidth, setupModeHeight, setupModeScreen);
}

void esp32_2432S028R_AnimateCurrentScreen(unsigned long frame)
{
}

// Variables para controlar el parpadeo con millis()
unsigned long previousMillis = 0;
unsigned long previousTouchMillis = 0;
char currentScreen = 0;

void esp32_2432S028R_DoLedStuff(unsigned long frame)
{
  unsigned long currentMillis = millis();
#ifdef TOUCH_ENABLE
  // / Check the touch coordinates 110x185 210x240
  if (currentMillis - previousTouchMillis >= 500)
    {
      int16_t t_x , t_y;  // To store the touch coordinates
      bool pressed = touch.getXY(t_x, t_y);
      if (pressed) {
          if (((t_x > 109)&&(t_x < 211)) && ((t_y > 185)&&(t_y < 241))) {
            bottomScreenBlue ^= true;
            hasChangedScreen = true;
          } else if((t_x > 235) && ((t_y > 0)&&(t_y < 16))) {
            // Touching the top right corner of the screen, roughly in the gray status label.
            // Disabling the screen backlight.
            esp32_2432S028R_AlternateScreenState();
          }
          else
            if (t_x > 160) {
              // next screen
             // DEBUG_SERIAL_PRINTF("Next screen touch( x:%d y:%d )\n", t_x, t_y);
              currentDisplayDriver->current_cyclic_screen = (currentDisplayDriver->current_cyclic_screen + 1) % currentDisplayDriver->num_cyclic_screens;
            } else if (t_x < 160)
            {
              // Previus screen
             // DEBUG_SERIAL_PRINTF("Previus screen touch( x:%d y:%d )\n", t_x, t_y);
              /* DEBUG_SERIAL_PRINTLN(currentDisplayDriver->current_cyclic_screen); */
              currentDisplayDriver->current_cyclic_screen = currentDisplayDriver->current_cyclic_screen - 1;
              if (currentDisplayDriver->current_cyclic_screen<0) currentDisplayDriver->current_cyclic_screen = currentDisplayDriver->num_cyclic_screens - 1;
            }
      }
      previousTouchMillis = currentMillis;
    }

    if (currentScreen != currentDisplayDriver->current_cyclic_screen) hasChangedScreen ^= true;
    currentScreen = currentDisplayDriver->current_cyclic_screen;
#endif


}

// Only Mining Screen enabled - other screens commented out to eliminate HTTP API overhead
CyclicScreenFunction esp32_2432S028RCyclicScreens[] = {esp32_2432S028R_MinerScreen};
// Disabled: esp32_2432S028R_ClockScreen, esp32_2432S028R_GlobalHashScreen, esp32_2432S028R_BTCprice

DisplayDriver esp32_2432S028RDriver = {
    esp32_2432S028R_Init,
    esp32_2432S028R_AlternateScreenState,
    esp32_2432S028R_AlternateRotation,
    esp32_2432S028R_LoadingScreen,
    esp32_2432S028R_SetupScreen,
    esp32_2432S028RCyclicScreens,
    esp32_2432S028R_AnimateCurrentScreen,
    esp32_2432S028R_DoLedStuff,
    SCREENS_ARRAY_SIZE(esp32_2432S028RCyclicScreens),
    0,
    WIDTH,
    HEIGHT};
#endif
