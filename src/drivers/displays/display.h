#ifndef DISPLAY_H
#define DISPLAY_H

#include "displayDriver.h"

extern DisplayDriver *currentDisplayDriver;

void initDisplay();
void alternateScreenState();
void alternateScreenRotation();
void switchToNextScreen();  // Deprecated: Does nothing when num_cyclic_screens == 1
void resetToFirstScreen();
void drawLoadingScreen();
void drawSetupScreen();
void drawCurrentScreen(unsigned long mElapsed);
void animateCurrentScreen(unsigned long frame);
void doLedStuff(unsigned long frame);

#endif // DISPLAY_H
