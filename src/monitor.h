#ifndef MONITOR_API_H
#define MONITOR_API_H

#include <Arduino.h>
#include <atomic>

// Monitor states
#define SCREEN_MINING   0
#define NO_SCREEN       3   //Used when board has no TFT

//Time update period
#define UPDATE_PERIOD_h   5

//API public-pool.io
#define getPublicPool "https://public-pool.io:40557/api/client/" // +btcString
#define UPDATE_POOL_min   1

enum NMState {
  NM_waitingConfig,
  NM_Connecting,
  NM_hashing
};

typedef struct{
  uint8_t screen;
  bool rotation;
  std::atomic<NMState> NerdStatus;
}monitor_data;

typedef struct {
  String completedShares;
  String totalMHashes;
  String totalKHashes;
  String currentHashRate;
  String templates;
  String bestDiff;
  String timeMining;
  String valids;
  String temp;
  String currentTime;
}mining_data;

typedef struct {
  String currentHashRate;
  String valids;
  unsigned long currentHours;
  unsigned long currentMinutes;
  unsigned long currentSeconds;
}clock_data_t;

typedef struct{
  int workersCount;       // Workers count, how many nerdminers using your address
  String workersHash;     // Workers Total Hash Rate
  String bestDifficulty;  // Your miners best difficulty
}pool_data;

void setup_monitor(void);

mining_data getMiningData(unsigned long mElapsed);
pool_data getPoolData(void);
clock_data_t getClockData_t(unsigned long mElapsed);
String getPoolAPIUrl(void);

#endif //MONITOR_API_H
