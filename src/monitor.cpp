#include <Arduino.h>
#include <WiFi.h>
#include "mbedtls/md.h"
#include "HTTPClient.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <list>
#include <atomic>
#include <mutex>
#include "mining.h"
#include "utils.h"
#include "monitor.h"
#include "drivers/storage/storage.h"
#include "drivers/devices/device.h"
#include "logging.h"

extern std::atomic<uint32_t> templates;
extern std::atomic<uint32_t> hashes;
extern std::atomic<uint32_t> Mhashes;
extern std::atomic<uint32_t> totalKHashes;
extern std::atomic<uint32_t> elapsedKHs;
extern std::atomic<uint64_t> upTime;

extern std::atomic<uint32_t> shares; // increase if blockhash has 32 bits of zeroes
extern std::atomic<uint32_t> valids; // increased if blockhash <= targethalfshares

extern double best_diff; // track best diff
extern std::mutex best_diff_mutex; // mutex for best_diff

extern monitor_data mMonitor;

//from saved config
extern TSettings Settings; 
bool invertColors = false;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 3600000);  // 1 hour update interval
pool_data pData;
String poolAPIUrl;


void setup_monitor(void){
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
    if(WiFi.status() == WL_CONNECTED) {
        if(timeClient.update()) mTriggerUpdate = millis(); //NTP call to get current time
        initialTime = timeClient.getEpochTime(); // Guarda la hora inicial (en segundos desde 1970)
        DEBUG_SERIAL_PRINT("TimeClient NTPupdateTime ");
    }
  }

  unsigned long elapsedTime = (millis() - mTriggerUpdate) / 1000; // Tiempo transcurrido en segundos
  unsigned long currentTime = initialTime + elapsedTime; // La hora actual

  // convierte la hora actual en horas, minutos y segundos
  *currentHours = currentTime % 86400 / 3600;
  *currentMinutes = currentTime % 3600 / 60;
  *currentSeconds = currentTime % 60;
}

String getDate(){
  
  unsigned long elapsedTime = (millis() - mTriggerUpdate) / 1000; // Tiempo transcurrido en segundos
  unsigned long currentTime = initialTime + elapsedTime; // La hora actual

  // Convierte la hora actual (epoch time) en una estructura tm
  struct tm *tm = localtime((time_t *)&currentTime);

  int year = tm->tm_year + 1900; // tm_year es el número de años desde 1900
  int month = tm->tm_mon + 1;    // tm_mon es el mes del año desde 0 (enero) hasta 11 (diciembre)
  int day = tm->tm_mday;         // tm_mday es el día del mes

  char currentDate[20];
  sprintf(currentDate, "%02d/%02d/%04d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);

  return String(currentDate);
}

String getTime(void){
  static char LocalHour[10];  // Static buffer to avoid heap allocation
  unsigned long currentHours, currentMinutes, currentSeconds;
  getTime(&currentHours, &currentMinutes, &currentSeconds);

  sprintf(LocalHour, "%02d:%02d", currentHours, currentMinutes);

  return String(LocalHour);
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
  double hashrate = (double)elapsedKHs.load(std::memory_order_relaxed) * 1000.0 / (double)mElapsed;

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
  if (abs(avg_hashrate - s_last_avg_hashrate) > 0.2)
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

  // bestDiff - use temp buffer for suffix_string (read with mutex)
  char bestDiffBuf[16];
  {
    std::lock_guard<std::mutex> diff_lock(best_diff_mutex);
    suffix_string(best_diff, bestDiffBuf, sizeof(bestDiffBuf), 0);
  }
  data.bestDiff = bestDiffBuf;

  // timeMining - format uptime
  uint64_t tm = upTime.load(std::memory_order_relaxed);
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
  data.completedShares = shares.load(std::memory_order_relaxed);
  data.totalMHashes = Mhashes.load(std::memory_order_relaxed);
  data.totalKHashes = totalKHashes.load(std::memory_order_relaxed);
  data.currentHashRate = getCurrentHashRate(mElapsed);
  data.templates = templates.load(std::memory_order_relaxed);
  data.valids = valids.load(std::memory_order_relaxed);
  data.temp = cachedTemp;
  data.currentTime = getTime();

  return data;
}


clock_data_t getClockData_t(unsigned long mElapsed)
{
  clock_data_t data;

  data.valids = valids.load(std::memory_order_relaxed);
  data.currentHashRate = getCurrentHashRate(mElapsed);
  getTime(&data.currentHours, &data.currentMinutes, &data.currentSeconds);

  return data;
}


String getPoolAPIUrl(void) {
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

pool_data getPoolData(void){
    //pool_data pData;    
    if((mPoolUpdate == 0) || (millis() - mPoolUpdate > UPDATE_POOL_min * 60 * 1000)){      
        if (WiFi.status() != WL_CONNECTED) return pData;            
        //Make first API call to get global hash and current difficulty
        HTTPClient http;
        http.setTimeout(10000);        
        try {          
          String btcWallet = Settings.BtcWallet;
          // DEBUG_SERIAL_PRINTLN(btcWallet);
          if (btcWallet.indexOf(".")>0) btcWallet = btcWallet.substring(0,btcWallet.indexOf("."));
#ifdef SCREEN_WORKERS_ENABLE
          DEBUG_SERIAL_PRINTLN("Pool API : " + poolAPIUrl+btcWallet);
          http.begin(poolAPIUrl+btcWallet);
#else
          http.begin(String(getPublicPool)+btcWallet);
#endif
          int httpCode = http.GET();
          if (httpCode == HTTP_CODE_OK) {
              String payload = http.getString();
              // DEBUG_SERIAL_PRINTLN(payload);
              StaticJsonDocument<300> filter;
              filter["bestDifficulty"] = true;
              filter["workersCount"] = true;
              filter["workers"][0]["sessionId"] = true;
              filter["workers"][0]["hashRate"] = true;
              StaticJsonDocument<2048> doc;
              deserializeJson(doc, payload, DeserializationOption::Filter(filter));
              //DEBUG_SERIAL_PRINTLN(serializeJsonPretty(doc, Serial));
              if (doc.containsKey("workersCount")) pData.workersCount = doc["workersCount"].as<int>();
              const JsonArray& workers = doc["workers"].as<JsonArray>();
              float totalhashs = 0;
              for (const JsonObject& worker : workers) {
                totalhashs += worker["hashRate"].as<double>();
                /* DEBUG_SERIAL_PRINT(worker["sessionId"].as<String>()+": ");
                DEBUG_SERIAL_PRINT(" - "+worker["hashRate"].as<String>()+": ");
                DEBUG_SERIAL_PRINTLN(totalhashs); */
              }
              char totalhashs_s[16] = {0};
              suffix_string(totalhashs, totalhashs_s, 16, 0);
              pData.workersHash = String(totalhashs_s);

              double temp;
              if (doc.containsKey("bestDifficulty")) {
              temp = doc["bestDifficulty"].as<double>();            
              char best_diff_string[16] = {0};
              suffix_string(temp, best_diff_string, 16, 0);
              pData.bestDifficulty = String(best_diff_string);
              }
              doc.clear();
              mPoolUpdate = millis();
              DEBUG_SERIAL_PRINTLN("\n####### Pool Data OK!");               
          } else {
              DEBUG_SERIAL_PRINTLN("\n####### Pool Data HTTP Error!");    
              /* DEBUG_SERIAL_PRINTLN(httpCode);
              String payload = http.getString();
              DEBUG_SERIAL_PRINTLN(payload); */
              // mPoolUpdate = millis();
              pData.bestDifficulty = "P";
              pData.workersHash = "E";
              pData.workersCount = 0;
              http.end();
              return pData; 
          }
          http.end();
        } catch(...) {
          DEBUG_SERIAL_PRINTLN("####### Pool Error!");          
          // mPoolUpdate = millis();
          pData.bestDifficulty = "P";
          pData.workersHash = "Error";
          pData.workersCount = 0;
          http.end();
          return pData;
        } 
    }
    return pData;
}
