#pragma once

#include <Arduino.h>

#ifdef DEBUG_MINING
#define DEBUG_SERIAL_PRINT(...) ((void)Serial.print(__VA_ARGS__))
#define DEBUG_SERIAL_PRINTLN(...) ((void)Serial.println(__VA_ARGS__))
#define DEBUG_SERIAL_PRINTF(...) ((void)Serial.printf(__VA_ARGS__))
#else
#define DEBUG_SERIAL_PRINT(...) ((void)0)
#define DEBUG_SERIAL_PRINTLN(...) ((void)0)
#define DEBUG_SERIAL_PRINTF(...) ((void)0)
#endif

