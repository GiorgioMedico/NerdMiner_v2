#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "mbedtls/md.h"
#include "HTTPClient.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <cmath>
#include <cstring>
#include <list>
#include <atomic>
#include "mining.h"
#include "utils.h"
#include "monitor.h"
#include "drivers/storage/storage.h"
#include "drivers/devices/device.h"
#include "logging.h"

extern uint32_t templates;
extern std::atomic<uint32_t> hashes;
extern std::atomic<uint32_t> Mhashes;
extern uint32_t totalKHashes;
extern double elapsedKHs;
extern uint64_t upTime;

extern uint32_t shares; // increase if blockhash has 32 bits of zeroes
extern uint32_t valids; // increased if blockhash <= targethalfshares

extern double best_diff; // track best diff (visualization only)

extern monitor_data mMonitor;

//from saved config
extern TSettings Settings; 
#ifdef DEBUG_HARDCODED_CONFIG
bool invertColors = DEBUG_INVERT_COLORS;
#else
bool invertColors = false;
#endif

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 3600000);  // 1 hour update interval
pool_data pData;
String poolAPIUrl;

static const String& getSanitizedWallet()
{
    static String sanitizedWallet;
    static char lastWalletSetting[sizeof(Settings.BtcWallet)] = {0};

    if (strncmp(lastWalletSetting, Settings.BtcWallet, sizeof(lastWalletSetting)) != 0)
    {
        strncpy(lastWalletSetting, Settings.BtcWallet, sizeof(lastWalletSetting));
        lastWalletSetting[sizeof(lastWalletSetting) - 1] = '\0';
        sanitizedWallet = lastWalletSetting;
        const int dotIndex = sanitizedWallet.indexOf('.');
        if (dotIndex > 0)
        {
            sanitizedWallet.remove(dotIndex);
        }
    }

    return sanitizedWallet;
}

static const String& getCachedPoolUrl()
{
    static String cachedUrl;
    static String lastBaseUrl;
    static String lastWallet;
#ifdef SCREEN_WORKERS_ENABLE
    static String lastPoolAddress;
    static int lastPoolPort = -1;
#endif

#ifdef SCREEN_WORKERS_ENABLE
    if (poolAPIUrl.isEmpty() ||
        Settings.PoolAddress != lastPoolAddress ||
        Settings.PoolPort != lastPoolPort)
    {
        poolAPIUrl = getPoolAPIUrl();
        lastPoolAddress = Settings.PoolAddress;
        lastPoolPort = Settings.PoolPort;
    }
    const String& baseUrl = poolAPIUrl;
#else
    static const String baseUrl = String(getPublicPool);
#endif

    const String& wallet = getSanitizedWallet();

    const String& baseRef = baseUrl;

    if (baseRef != lastBaseUrl || wallet != lastWallet)
    {
        cachedUrl.reserve(baseRef.length() + wallet.length());
        cachedUrl = baseRef;
        cachedUrl += wallet;
        lastBaseUrl = baseRef;
        lastWallet = wallet;
    }

    return cachedUrl;
}


void setup_monitor(void)
{
    /******** TIME ZONE SETTING *****/

    timeClient.begin();
    
    // Adjust offset depending on your zone
    // GMT +2 in seconds (zona horaria de Europa Central)
    timeClient.setTimeOffset(3600 * Settings.Timezone);

    DEBUG_SERIAL_PRINTLN("TimeClient setup done");
#ifdef SCREEN_WORKERS_ENABLE
    poolAPIUrl = getPoolAPIUrl();
    DEBUG_SERIAL_PRINTLN("poolAPIUrl: " + poolAPIUrl);
#endif
}



unsigned long mTriggerUpdate = 0;
unsigned long initialMillis = millis();
unsigned long initialTime = 0;
unsigned long mPoolUpdate = 0;

void getTime(unsigned long* currentHours, unsigned long* currentMinutes, unsigned long* currentSeconds){

  //Check if need an NTP call to check current time
  if((mTriggerUpdate == 0) || (millis() - mTriggerUpdate > UPDATE_PERIOD_h * 60 * 60 * 1000)){ //60 sec. * 60 min * 1000ms
    if(WiFi.status() == WL_CONNECTED && timeClient.update()) {
        initialTime = timeClient.getEpochTime(); // Update base time on successful sync
        mTriggerUpdate = millis();
        DEBUG_SERIAL_PRINT("TimeClient NTP updated");
    }
  }

  unsigned long elapsedTime = (millis() - mTriggerUpdate) / 1000; // Tiempo transcurrido en segundos
  unsigned long currentTime = initialTime + elapsedTime; // Current time (timezone handled by timeClient)

  // convierte la hora actual en horas, minutos y segundos (optimizado)
  unsigned long secondsToday = currentTime % 86400;
  *currentHours = secondsToday / 3600;
  *currentMinutes = (secondsToday % 3600) / 60;
  *currentSeconds = secondsToday % 60;
}

String getTime(void){
  unsigned long currentHours, currentMinutes, currentSeconds;
  getTime(&currentHours, &currentMinutes, &currentSeconds);

  char timeBuffer[6];  // "HH:MM\0" = 6 chars
  sprintf(timeBuffer, "%02lu:%02lu", currentHours, currentMinutes);

  return String(timeBuffer);
}


enum EHashRateScale
{
  HashRateScale_99KH,
  HashRateScale_999KH,
  HashRateScale_9MH
};

static EHashRateScale s_hashrate_scale = HashRateScale_99KH;
static uint32_t s_skip_first = 3;
static double s_top_hashrate = 0.0;

static std::list<double> s_hashrate_avg_list;
static double s_hashrate_summ = 0.0;
static uint8_t s_hashrate_recalc = 0;
static char s_hashrate_buffer[16] = {0};  // Cache for hashrate string
static double s_last_avg_hashrate = -1.0;  // Track last value to avoid recalculation

String getCurrentHashRate(unsigned long mElapsed)
{
  double hashrate = elapsedKHs * 1000.0 / (double)mElapsed;

  s_hashrate_summ += hashrate;
  s_hashrate_avg_list.push_back(hashrate);
  if (s_hashrate_avg_list.size() > 5)
  {
    s_hashrate_summ -= s_hashrate_avg_list.front();
    s_hashrate_avg_list.pop_front();
  }

  ++s_hashrate_recalc;
  if (s_hashrate_recalc == 0)
  {
    s_hashrate_summ = 0.0;
    for (auto itt = s_hashrate_avg_list.begin(); itt != s_hashrate_avg_list.end(); ++itt)
      s_hashrate_summ += *itt;
  }

  double avg_hashrate = s_hashrate_summ / (double)s_hashrate_avg_list.size();
  if (avg_hashrate < 0.0)
    avg_hashrate = 0.0;

  if (s_skip_first > 0)
  {
    s_skip_first--;
  } else
  {
    if (avg_hashrate > s_top_hashrate)
    {
      s_top_hashrate = avg_hashrate;
      if (avg_hashrate > 999.9)
        s_hashrate_scale = HashRateScale_9MH;
      else if (avg_hashrate > 99.9)
        s_hashrate_scale = HashRateScale_999KH;
    }
  }

  // Only update string if hashrate changed significantly
  if (std::abs(avg_hashrate - s_last_avg_hashrate) > 0.2)
  {
    s_last_avg_hashrate = avg_hashrate;
    switch (s_hashrate_scale)
    {
      case HashRateScale_99KH:
        snprintf(s_hashrate_buffer, sizeof(s_hashrate_buffer), "%.2f", avg_hashrate);
        break;
      case HashRateScale_999KH:
        snprintf(s_hashrate_buffer, sizeof(s_hashrate_buffer), "%.1f", avg_hashrate);
        break;
      default:
        snprintf(s_hashrate_buffer, sizeof(s_hashrate_buffer), "%d", (int)avg_hashrate);
        break;
    }
  }

  return String(s_hashrate_buffer);
}

mining_data getMiningData(unsigned long mElapsed)
{
  mining_data data;

  char bestDiffBuf[16];
  suffix_string(best_diff, bestDiffBuf, sizeof(bestDiffBuf), 0);
  data.bestDiff = bestDiffBuf;

  // timeMining - format uptime
  uint64_t tm = upTime;
  int secs = tm % 60;
  tm /= 60;
  int mins = tm % 60;
  tm /= 60;
  int hours = tm % 24;
  int days = tm / 24;
  char timeBuf[20];
  snprintf(timeBuf, sizeof(timeBuf), "%01d  %02d:%02d:%02d", days, hours, mins, secs);
  data.timeMining = timeBuf;

  // Cache temperature reading - update only every 5 seconds
  static unsigned long lastTempUpdate = 0;
  static char cachedTemp[8] = "0";
  unsigned long currentMillis = millis();
  if (currentMillis - lastTempUpdate >= 5000 || lastTempUpdate == 0)
  {
    snprintf(cachedTemp, sizeof(cachedTemp), "%.0f", temperatureRead());
    lastTempUpdate = currentMillis;
  }

  // Direct String assignments - numeric values auto-convert
  data.completedShares = shares;
  data.totalMHashes = Mhashes.load();  // Explicit load for atomic
  const uint32_t totalKHInt = totalKHashes;
  const uint32_t hashRemainder = hashes.load() % 1000;  // Explicit load for atomic
  const float totalKH = static_cast<float>(totalKHInt) + static_cast<float>(hashRemainder) / 1000.0f;
  data.totalKHashes = String(totalKH, 2);
  data.currentHashRate = getCurrentHashRate(mElapsed);
  data.templates = templates;
  data.valids = valids;
  data.temp = cachedTemp;
  data.currentTime = getTime();

  return data;
}

String getPoolAPIUrl(void) 
{
    poolAPIUrl = String(getPublicPool);
    if (Settings.PoolAddress == "public-pool.io") {
        poolAPIUrl = "https://public-pool.io:40557/api/client/";
    } 
    else {
        if (Settings.PoolAddress == "pool.nerdminers.org") {
            poolAPIUrl = "https://pool.nerdminers.org/users/";
        }
        else {
            switch (Settings.PoolPort) {
                case 3333:
                    if (Settings.PoolAddress == "pool.sethforprivacy.com")
                        poolAPIUrl = "https://pool.sethforprivacy.com/api/client/";
                    if (Settings.PoolAddress == "pool.solomining.de")
                        poolAPIUrl = "https://pool.solomining.de/api/client/";
                    // Add more cases for other addresses with port 3333 if needed
                    break;
                case 2018:
                    // Local instance of public-pool.io on Umbrel or Start9
                    poolAPIUrl = "http://" + Settings.PoolAddress + ":2019/api/client/";
                    break;
                default:
                    poolAPIUrl = String(getPublicPool);
                    break;
            }
        }
    }
    return poolAPIUrl;
}

pool_data getPoolData(void)
{
    //pool_data pData;
    if((mPoolUpdate == 0) || (millis() - mPoolUpdate > UPDATE_POOL_min * 60 * 1000))
    {
        if (WiFi.status() != WL_CONNECTED) return pData;
        //Make first API call to get global hash and current difficulty
        HTTPClient http;
        http.setTimeout(10000);

        const String& poolUrl = getCachedPoolUrl();
        DEBUG_SERIAL_PRINTLN("Pool API : " + poolUrl);
        http.begin(poolUrl.c_str());
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) 
        {
            StaticJsonDocument<128> filter;
            filter["bestDifficulty"] = true;
            filter["workersCount"] = true;
            filter["workers"][0]["hashRate"] = true;
            StaticJsonDocument<1024> doc;
            DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
            if (!error)
            {
                if (doc.containsKey("workersCount"))
                    pData.workersCount = doc["workersCount"].as<int>();

                double totalhashs = 0.0;
                double bestDifficultyValue = NAN;
                JsonArrayConst workers = doc["workers"].as<JsonArrayConst>();
                if (!workers.isNull())
                {
                    for (JsonObjectConst worker : workers)
                    {
                        totalhashs += worker["hashRate"].as<double>();
                    }
                }
                char totalhashs_s[16] = {0};
                suffix_string(totalhashs, totalhashs_s, sizeof(totalhashs_s), 0);
                pData.workersHash = String(totalhashs_s);

                if (doc.containsKey("bestDifficulty"))
                {
                    bestDifficultyValue = doc["bestDifficulty"].as<double>();
                    char best_diff_string[16] = {0};
                    suffix_string(bestDifficultyValue, best_diff_string, sizeof(best_diff_string), 0);
                    pData.bestDifficulty = String(best_diff_string);
                }
                #ifdef DEBUG_MINING_ALL
                DEBUG_SERIAL_PRINTF("[POOL] workers=%d totalHash=%s raw=%.2f bestDifficulty=%s raw=%.2f\n",
                                    pData.workersCount,
                                    pData.workersHash.c_str(),
                                    totalhashs,
                                    pData.bestDifficulty.c_str(),
                                    bestDifficultyValue);
                #endif
                mPoolUpdate = millis();
                DEBUG_SERIAL_PRINTLN("\n####### Pool Data OK!");
            }
            else
            {
                DEBUG_SERIAL_PRINTF("\n####### ❌ Pool Data JSON Error! (%s)\n", error.c_str());
                pData.bestDifficulty = "P";
                pData.workersHash = "E";
                pData.workersCount = 0;
            }
        }
        else
        {
            DEBUG_SERIAL_PRINTF("\n####### Pool Data HTTP Error! Code: %d\n", httpCode);
            String payload = http.getString();
            
            #ifdef DEBUG_MINING_ALL
            if (payload.length() > 0) {
                DEBUG_SERIAL_PRINTLN("Response: " + payload);
            }
            #endif
            pData.bestDifficulty = "P";
            pData.workersHash = "E";
            pData.workersCount = 0;
        }
        http.end();
    }
    return pData;
}
