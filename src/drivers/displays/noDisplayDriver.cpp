#include "displayDriver.h"
#include "logging.h"

#ifdef NO_DISPLAY

#include <Arduino.h>
#include "monitor.h"
#include "wManager.h"

extern monitor_data mMonitor;
bool ledOn = true;

void noDisplay_Init(void)
{
  DEBUG_SERIAL_PRINTLN("No display driver initialized");
  pinMode(LED_PIN, OUTPUT);
}

void noDisplay_AlternateScreenState(void)
{
  DEBUG_SERIAL_PRINTLN("Switching display state");
  ledOn = !ledOn;
}

void noDisplay_AlternateRotation(void)
{
}

void noDisplay_NoScreen(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);

  // Print hashrate to serial
  DEBUG_SERIAL_PRINTF(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str());

  // Print extended data to serial for no display devices
  DEBUG_SERIAL_PRINTF(">>> Valid blocks: %s\n", data.valids.c_str());
  DEBUG_SERIAL_PRINTF(">>> Block templates: %s\n", data.templates.c_str());
  DEBUG_SERIAL_PRINTF(">>> Best difficulty: %s\n", data.bestDiff.c_str());
  DEBUG_SERIAL_PRINTF(">>> 32Bit shares: %s\n", data.completedShares.c_str());
  DEBUG_SERIAL_PRINTF(">>> Temperature: %s\n", data.temp.c_str());
  DEBUG_SERIAL_PRINTF(">>> Total MHashes: %s\n", data.totalMHashes.c_str());
  DEBUG_SERIAL_PRINTF(">>> Time mining: %s\n", data.timeMining.c_str());
}
void noDisplay_LoadingScreen(void)
{
  DEBUG_SERIAL_PRINTLN("Initializing...");
}

void noDisplay_SetupScreen(void)
{
  DEBUG_SERIAL_PRINTLN("Setup...");
}

// Variables para controlar el parpadeo con millis()
unsigned long previousMillis = 0;

void noDisplay_DoLedStuff(unsigned long frame)
{
  unsigned long currentMillis = millis();

  if (!ledOn)
  {
    digitalWrite(LED_PIN, INACTIVE_LED);
    return;
  }

  NMState status = mMonitor.NerdStatus;
  switch (status)
  {

  case NM_waitingConfig:
    digitalWrite(LED_PIN, ACTIVE_LED); // LED encendido de forma continua
    break;

  case NM_Connecting:
    if (currentMillis - previousMillis >= 500)
    { // 0.5sec blink
      previousMillis = currentMillis;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Cambia el estado del LED
    }
    break;

  case NM_hashing:
    if (currentMillis - previousMillis >= 100)
    { // 0.1sec blink
      previousMillis = currentMillis;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Cambia el estado del LED
    }
    break;
  }
}

void noDisplay_AnimateCurrentScreen(unsigned long frame)
{
}

CyclicScreenFunction noDisplayCyclicScreens[] = {noDisplay_NoScreen};

DisplayDriver noDisplayDriver = {
    noDisplay_Init,
    noDisplay_AlternateScreenState,
    noDisplay_AlternateRotation,
    noDisplay_LoadingScreen,
    noDisplay_SetupScreen,
    noDisplayCyclicScreens,
    noDisplay_AnimateCurrentScreen,
    noDisplay_DoLedStuff,
    SCREENS_ARRAY_SIZE(noDisplayCyclicScreens),
    0,
    0,
    0,
};
#endif
