#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <nvs_flash.h>
#include <nvs.h>
//#include "ShaTests/nerdSHA256.h"
#include "ShaTests/nerdSHA256plus.h"
#include "stratum.h"
#include "mining.h"
#include "utils.h"
#include "monitor.h"
#include "logging.h"
#include "timeconst.h"
#include "drivers/displays/display.h"
#include "drivers/storage/storage.h"
#include <mutex>
#include <list>
#include <map>
#include <atomic>
#include "mbedtls/sha256.h"
#include "esp_random.h"

//10 Jobs per second
// #define NONCE_PER_JOB_SW 4096
// #define NONCE_PER_JOB_HW 16*1024
#define NONCE_PER_JOB_SW 2*16384
#define NONCE_PER_JOB_HW 4*64*1024

//#define SHA256_VALIDATE
#define RANDOM_NONCE


#ifdef HARDWARE_SHA256
#include <sha/sha_dma.h>
#include <hal/sha_hal.h>
#include <hal/sha_ll.h>

#if defined(CONFIG_IDF_TARGET_ESP32)
#include <sha/sha_parallel_engine.h>
#endif

#endif

nvs_handle_t stat_handle;

std::atomic<uint32_t> templates{0};
std::atomic<uint32_t> hashes{0};
std::atomic<uint32_t> Mhashes{0};
std::atomic<uint32_t> totalKHashes{0};
std::atomic<float> elapsedKHs{0.0f};
std::atomic<uint64_t> upTime{0};

std::atomic<uint32_t> shares{0}; // increase if blockhash has 32 bits of zeroes
std::atomic<uint32_t> valids{0}; // increased if blockhash <= target

// Track best diff using lock-free atomic float
std::atomic<float> best_diff(0.0f);

// Track SHA hardware timeout events for diagnostics
std::atomic<uint32_t> sha_timeout_count{0};

// Variables to hold data from custom textboxes
//Track mining stats in non volatile memory
extern TSettings Settings;

IPAddress serverIP(1, 1, 1, 1); //Temporarily save poolIPaddres

//Global work data 
static WiFiClient client;
static std::atomic<bool> s_client_connected{false};  // Thread-safe proxy for client.connected()
static miner_data mMiner; //Global miner data (Create a miner class TODO)
mining_subscribe mWorker;
mining_job mJob;
monitor_data mMonitor;
static std::atomic<bool> isMinerSuscribed{false};
uint32_t mLastTXtoPool = millis();

int saveIntervals[7] = {5 * 60, 15 * 60, 30 * 60, 1 * 3600, 3 * 3600, 6 * 3600, 12 * 3600};
int saveIntervalsSize = sizeof(saveIntervals)/sizeof(saveIntervals[0]);
int currentIntervalIndex = 0;

bool checkPoolConnection(void)
{

  // Fast path: Already connected
  if (client.connected()) 
  {
    s_client_connected.store(true, std::memory_order_release);
    return true;
  }

  // Slow path: Disconnected, attempt reconnection
  s_client_connected.store(false, std::memory_order_release);
  isMinerSuscribed.store(false, std::memory_order_release);

  DEBUG_SERIAL_PRINTF("[POOL] Client not connected, attempting to connect to %s:%d\n", Settings.PoolAddress.c_str(), Settings.PoolPort);

  //Resolve first time pool DNS and save IP
  if(serverIP == IPAddress(1,1,1,1)) 
  {
    DEBUG_SERIAL_PRINTF("[POOL] Resolving DNS for %s (first time)\n", Settings.PoolAddress.c_str());
    int dns_result = WiFi.hostByName(Settings.PoolAddress.c_str(), serverIP);
    if (dns_result == 1)
      DEBUG_SERIAL_PRINTF("[POOL] ✓ DNS resolved: %s -> %s\n", Settings.PoolAddress.c_str(), serverIP.toString().c_str());
    else
      DEBUG_SERIAL_PRINTF("[POOL] ❌ DNS resolution FAILED for %s (serverIP remains %s)\n", Settings.PoolAddress.c_str(), serverIP.toString().c_str());
  }

  // Check if we have a valid IP
  if (serverIP == IPAddress(1,1,1,1)) 
  {
    DEBUG_SERIAL_PRINTF("[POOL] No valid serverIP available, cannot connect\n");
    return false;
  }

  //Try connecting pool IP
  DEBUG_SERIAL_PRINTF("[POOL] Attempting TCP connect to %s:%d (timeout 5s)\n", serverIP.toString().c_str(), Settings.PoolPort);
  if (!client.connect(serverIP, Settings.PoolPort, 5000)) {
    DEBUG_SERIAL_PRINTF("[POOL] ❌ TCP connection FAILED to %s:%d\n", serverIP.toString().c_str(), Settings.PoolPort);
    DEBUG_SERIAL_PRINTF("[POOL] Retrying DNS resolution for %s\n", Settings.PoolAddress.c_str());
    int dns_retry = WiFi.hostByName(Settings.PoolAddress.c_str(), serverIP);
    if (dns_retry == 1)
      DEBUG_SERIAL_PRINTF("[POOL] ✓ DNS retry succeeded: %s -> %s\n", Settings.PoolAddress.c_str(), serverIP.toString().c_str());
    else
      DEBUG_SERIAL_PRINTF("[POOL] ❌ DNS retry FAILED for %s\n", Settings.PoolAddress.c_str());
      
    return false;
  }

  DEBUG_SERIAL_PRINTF("[POOL] ✓ TCP connection established to %s:%d\n", Settings.PoolAddress.c_str(), Settings.PoolPort);
  s_client_connected.store(true, std::memory_order_release);
  return true;
}

// Helper: Handle 32-bit timestamp wraparound (occurs every ~49 days)
static inline void adjust_for_wraparound(uint32_t now, uint32_t &old_time) 
{
  if (now < old_time) {
    old_time = now;
  }
}

// Helper: Calculate time difference with wraparound handling
static inline uint32_t time_elapsed(uint32_t now, uint32_t start) 
{
  return (now >= start) ? (now - start) : (UINT32_MAX - start + now);
}

//Implements a socketKeepAlive function and
//checks if pool is not sending any data to reconnect again.
//Even connection could be alive, pool could stop sending new job NOTIFY
uint32_t mStart0Hashrate = 0;
bool checkPoolInactivity(uint32_t keepAliveTime, uint32_t inactivityTime)
{
    // Read hashrate calculated by runMonitor (no race condition, no wraparound issues)
    float elapsedKHs_local = elapsedKHs.load(std::memory_order_relaxed);

    uint32_t time_now = millis();

    // If no shares sent to pool, send something to pool to hold socket open
    adjust_for_wraparound(time_now, mLastTXtoPool);
    if (time_now > mLastTXtoPool + keepAliveTime)
    {
      mLastTXtoPool = time_now;
      DEBUG_SERIAL_PRINTLN("  Sending  : KeepAlive suggest_difficulty");
      tx_suggest_difficulty(client, DEFAULT_DIFFICULTY);
    }

    if(elapsedKHs_local <= 0.0f)
    {
      //Check if hashrate is 0 during inactivityTIme
      if(mStart0Hashrate == 0) 
        mStart0Hashrate  = time_now;
      if((time_now-mStart0Hashrate) > inactivityTime) 
      { 
        mStart0Hashrate=0; 
        return true;
      }
      return false;
    }

  mStart0Hashrate = 0;
  return false;
}

struct JobRequest
{
  uint32_t id;
  uint32_t nonce_start;
  uint32_t nonce_count;
  double difficulty;
  uint8_t sha_buffer[128];
  uint32_t midstate[8];
  uint32_t bake[16];
  char job_id[64];       // Pool's job ID
  char ntime[9];         // Job's ntime (8 hex chars + null)
  char extranonce2[17];  // Extranonce2 (up to 16 hex chars + null)
};

struct JobResult
{
  uint32_t id;
  uint32_t nonce;
  uint32_t nonce_count;        // Successfully processed nonces
  uint32_t nonces_skipped;     // Nonces skipped due to SHA hardware timeout
  double difficulty;
  uint8_t best_hash[32];
  uint8_t hash[32];
  char job_id[64];       // Pool's job ID
  char ntime[9];         // Job's ntime
  char extranonce2[17];  // Extranonce2
};

static inline bool hash_is_better(const uint8_t* candidate, const uint8_t* reference)
{
  if (!candidate || !reference)
    return false;

  for (int i = 31; i >= 0; --i)
  {
    if (candidate[i] < reference[i])
      return true;
    if (candidate[i] > reference[i])
      return false;
  }
  return false;
}

static inline void initialize_best_hash(JobResult& result)
{
  if (!difficulty_to_target(result.difficulty, result.best_hash))
  {
    memset(result.best_hash, 0xFF, sizeof(result.best_hash));
  }
}

static std::mutex s_job_mutex;
std::list<std::shared_ptr<JobRequest>> s_job_request_list_sw;
#ifdef HARDWARE_SHA256
std::list<std::shared_ptr<JobRequest>> s_job_request_list_hw;
#endif
std::list<std::shared_ptr<JobResult>> s_job_result_list;

// Atomic job ID for cross-thread work cancellation (proper memory barriers)
static std::atomic<uint8_t> s_working_current_job_id{0xFF};

static void JobPush(std::list<std::shared_ptr<JobRequest>> &job_list,  uint32_t id, uint32_t nonce_start, uint32_t nonce_count, double difficulty,
                    const uint8_t* sha_buffer, const uint32_t* midstate, const uint32_t* bake,
                    const char* job_id, const char* ntime, const char* extranonce2)
{
  std::shared_ptr<JobRequest> job = std::make_shared<JobRequest>();
  memset(job.get(), 0, sizeof(JobRequest));
  job->id = id;
  job->nonce_start = nonce_start;
  job->nonce_count = nonce_count;
  job->difficulty = difficulty;
  memcpy(job->sha_buffer, sha_buffer, sizeof(job->sha_buffer));
  memcpy(job->midstate, midstate, sizeof(job->midstate));
  memcpy(job->bake, bake, sizeof(job->bake));
  strncpy(job->job_id, job_id, sizeof(job->job_id) - 1);
  strncpy(job->ntime, ntime, sizeof(job->ntime) - 1);
  strncpy(job->extranonce2, extranonce2, sizeof(job->extranonce2) - 1);
  job_list.push_back(job);
}

struct Submission
{
  double diff;
  bool is32bit;
  bool isValid;
  uint32_t timestamp_ms;  // Timestamp when submission was created (for timeout cleanup)
};

static void MiningJobStop(uint32_t &job_pool, std::map<uint32_t, std::shared_ptr<Submission>> &submission_map)
{
  {
    std::lock_guard<std::mutex> lock(s_job_mutex);
    s_job_result_list.clear();
    s_job_request_list_sw.clear();
    #ifdef HARDWARE_SHA256
    s_job_request_list_hw.clear();
    #endif
  }
  s_working_current_job_id.store(0xFF, std::memory_order_release);
  job_pool = 0xFFFFFFFF;

  // Clear submission map
  submission_map.clear();

}

#ifdef RANDOM_NONCE
// Initialize with hardware RNG for better entropy
uint64_t s_random_state = ((uint64_t)esp_random() << 32) | esp_random();
static uint32_t RandomGet()
{
    s_random_state += 0x9E3779B97F4A7C15ull;
    uint64_t z = s_random_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

#endif

void runStratumWorker(void *name)
{
// TEST: https://bitcoin.stackexchange.com/questions/22929/full-example-data-for-scrypt-stratum-client

  DEBUG_SERIAL_PRINTLN("");
  DEBUG_SERIAL_PRINTF("\n[WORKER] Started. Running %s on core %d\n", (char *)name, xPortGetCoreID());
  DEBUG_SERIAL_PRINTLN("========================================");
  DEBUG_SERIAL_PRINTF("[CONFIG] Pool:   %s:%d\n", Settings.PoolAddress.c_str(), Settings.PoolPort);
  DEBUG_SERIAL_PRINTF("[CONFIG] Wallet: %s\n", Settings.BtcWallet);
  DEBUG_SERIAL_PRINTF("[CONFIG] Pass:   %s\n", Settings.PoolPassword);
  DEBUG_SERIAL_PRINTLN("========================================");

  #ifdef DEBUG_MEMORY
  DEBUG_SERIAL_PRINTF("### [Total Heap / Free heap / Min free heap]: %d / %d / %d \n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
  #endif

  std::map<uint32_t, std::shared_ptr<Submission>> s_submission_map;

  // connect to pool
  double currentPoolDifficulty = DEFAULT_DIFFICULTY;
  uint32_t nonce_pool = 0;
  uint32_t job_pool = 0xFFFFFFFF;
  uint32_t last_job_time = millis();
  bool first_job_received = false;

  while(true) 
  {
      
    if(WiFi.status() != WL_CONNECTED)
    {
      // WiFi is disconnected, so reconnect now
      DEBUG_SERIAL_PRINTLN("[WIFI] ❌ WiFi disconnected - attempting reconnection");
      mMonitor.NerdStatus.store(NM_Connecting, std::memory_order_release);
      s_client_connected.store(false, std::memory_order_release);
      MiningJobStop(job_pool, s_submission_map);
      WiFi.reconnect();
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      continue;
    } 

    if(!checkPoolConnection())
    {
      //If server is not reachable add random delay for connection retries
      //Generate value between 1 and 60 secs
      int retry_delay_sec = 1 + esp_random() % 60;
      DEBUG_SERIAL_PRINTF("[POOL] Connection failed - retrying in %d seconds (esp_random check)\n", retry_delay_sec);
      MiningJobStop(job_pool, s_submission_map);
      vTaskDelay((retry_delay_sec * 1000) / portTICK_PERIOD_MS);
      continue;
    }

    if(!isMinerSuscribed.load(std::memory_order_acquire))
    {
      //Stop miner current jobs
      DEBUG_SERIAL_PRINTLN("[STRATUM] Starting subscription handshake...");
      mWorker = init_mining_subscribe();

      // STEP 1: Pool server connection (SUBSCRIBE)
      DEBUG_SERIAL_PRINTLN("[STRATUM] STEP 1/3: Subscribing to pool...");
      if(!tx_mining_subscribe(client, mWorker))
      {
        DEBUG_SERIAL_PRINTLN("[STRATUM] ❌ Subscribe FAILED - closing connection");
        client.stop();
        MiningJobStop(job_pool, s_submission_map);
        continue;
      }
      DEBUG_SERIAL_PRINTLN("[STRATUM] ✓ Subscribe successful");

      strcpy(mWorker.wName, Settings.BtcWallet);
      strcpy(mWorker.wPass, Settings.PoolPassword);

      // STEP 2: Pool authorize work (Block Info)
      DEBUG_SERIAL_PRINTLN("[STRATUM] STEP 2/3: Authorizing worker...");
      if (!tx_mining_auth(client, mWorker.wName, mWorker.wPass)) 
      {
        DEBUG_SERIAL_PRINTLN("[ERROR][STRATUM] ❌ Authorization FAILED - closing connection");
        client.stop();
        MiningJobStop(job_pool, s_submission_map);
        continue;
      }
      DEBUG_SERIAL_PRINTLN("[STRATUM] ✓ Authorization successful");

      // STEP 3: Suggest pool difficulty
      DEBUG_SERIAL_PRINTLN("[STRATUM] STEP 3/3: Suggesting difficulty...");
      tx_suggest_difficulty(client, currentPoolDifficulty);
      DEBUG_SERIAL_PRINTF("[STRATUM] ✓ Difficulty suggestion sent: %.2f\n", currentPoolDifficulty);

      isMinerSuscribed.store(true, std::memory_order_release);
      uint32_t time_now = millis();
      mLastTXtoPool = time_now;
      last_job_time = time_now;
      DEBUG_SERIAL_PRINTLN("[STRATUM] ✓✓✓ Handshake complete - waiting for jobs...");
    }

    //Check if pool is down for almost 5minutes and then restart connection with pool (1min=600000ms)
    if(checkPoolInactivity(KEEPALIVE_TIME_ms, POOLINACTIVITY_TIME_ms))
    {
      //Restart connection
      DEBUG_SERIAL_PRINTLN("[POOL] ⏱️ TIMEOUT: No data received for >60s - reconnecting...");
      client.stop();
      isMinerSuscribed.store(false, std::memory_order_release);
      MiningJobStop(job_pool, s_submission_map);
      continue;
    }

    {
      uint32_t time_now = millis();
      adjust_for_wraparound(time_now, last_job_time);

      // Reconnect if no job received for 10 minutes
      if (time_now >= last_job_time + JOB_TIMEOUT_MS)
      {
        uint32_t elapsed_sec = (time_now - last_job_time) / 1000;
        DEBUG_SERIAL_PRINTF("[POOL] ⏱️ JOB TIMEOUT: No job for %lu seconds (>600s) - reconnecting...\n", elapsed_sec);
        client.stop();
        isMinerSuscribed.store(false, std::memory_order_release);
        MiningJobStop(job_pool, s_submission_map);
        continue;
      }
    }

    uint32_t hw_midstate[8];
    uint32_t diget_mid[8];
    uint32_t bake[16];
    #if defined(CONFIG_IDF_TARGET_ESP32)
    uint8_t sha_buffer_swap[128];
    #endif

    //Read pending messages from pool
    while(client.connected() && client.available())
    {
      String line = client.readStringUntil('\n');
      DEBUG_SERIAL_PRINTLN("  Received message from pool");      
      stratum_method result = parse_mining_method(line.c_str());

      switch (result)
      {
          case MINING_NOTIFY:         if(parse_mining_notify(line.c_str(), mJob))
                                      {
                                          if (!first_job_received) 
                                          {
                                            DEBUG_SERIAL_PRINTLN("[JOB] ✓✓✓ First mining job received - starting mining!");
                                            DEBUG_SERIAL_PRINTF("[JOB] Job ID: %s\n", mJob.job_id.c_str());
                                            first_job_received = true;
                                          } 
                                          else
                                            DEBUG_SERIAL_PRINTF("[JOB] New job received (ID: %s)\n", mJob.job_id.c_str());
                                          

                                          // Clear job lists to prevent stale work
                                          {
                                            std::lock_guard<std::mutex> lock(s_job_mutex);
                                            s_job_request_list_sw.clear();
                                            #ifdef HARDWARE_SHA256
                                            s_job_request_list_hw.clear();
                                            #endif
                                          }
                                          //Increse templates readed
                                          templates.fetch_add(1, std::memory_order_relaxed);
                                          job_pool++;
                                          s_working_current_job_id.store(job_pool & 0xFF, std::memory_order_release); //Terminate current job in thread

                                          last_job_time = millis();
                                          mLastTXtoPool = last_job_time;

                                          uint32_t h = hashes.load(std::memory_order_relaxed);
                                          uint32_t mh = h / 1000000;
                                          Mhashes.fetch_add(mh, std::memory_order_relaxed);
                                          hashes.fetch_sub(mh * 1000000, std::memory_order_relaxed);

                                          //Prepare data for new jobs
                                          mMiner=calculateMiningData(mWorker, mJob);

                                          memset(mMiner.bytearray_blockheader+80, 0, 128-80);
                                          mMiner.bytearray_blockheader[80] = 0x80;
                                          mMiner.bytearray_blockheader[126] = 0x02;
                                          mMiner.bytearray_blockheader[127] = 0x80;

                                          nerd_mids(diget_mid, mMiner.bytearray_blockheader);
                                          nerd_sha256_bake(diget_mid, mMiner.bytearray_blockheader+64, bake);

                                          #ifdef HARDWARE_SHA256
                                          #if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
                                            esp_sha_acquire_hardware();
                                            sha_hal_hash_block(SHA2_256,  mMiner.bytearray_blockheader, 64/4, true);
                                            sha_hal_read_digest(SHA2_256, hw_midstate);
                                            esp_sha_release_hardware();
                                          #endif
                                          #endif

                                          #if defined(CONFIG_IDF_TARGET_ESP32)
                                          for (int i = 0; i < 32; ++i)
                                            ((uint32_t*)sha_buffer_swap)[i] = __builtin_bswap32(((const uint32_t*)(mMiner.bytearray_blockheader))[i]);
                                          #endif

                                          #ifdef RANDOM_NONCE
                                          nonce_pool = RandomGet();  // Random start with full 32-bit coverage
                                          #else
                                          nonce_pool = NONCE_START_RANDOM;
                                          #endif


                                          // Prepare jobs outside lock to reduce mutex hold time
                                          std::list<std::shared_ptr<JobRequest>> new_sw_jobs;
                                          #ifdef HARDWARE_SHA256
                                          std::list<std::shared_ptr<JobRequest>> new_hw_jobs;
                                          #endif

                                          for (int i = 0; i < 4; ++ i)
                                          {
                                            #if 1
                                            JobPush(new_sw_jobs, job_pool, nonce_pool, NONCE_PER_JOB_SW, currentPoolDifficulty, mMiner.bytearray_blockheader, diget_mid, bake,
                                                    mJob.job_id.c_str(), mJob.ntime.c_str(), mWorker.extranonce2.c_str());
                                            nonce_pool += NONCE_PER_JOB_SW;  // Sequential increment (wraparound automatic)
                                            #endif
                                            #ifdef HARDWARE_SHA256
                                              #if defined(CONFIG_IDF_TARGET_ESP32)
                                                JobPush(new_hw_jobs, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, sha_buffer_swap, hw_midstate, bake,
                                                        mJob.job_id.c_str(), mJob.ntime.c_str(), mWorker.extranonce2.c_str());
                                              #else
                                                JobPush(new_hw_jobs, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, mMiner.bytearray_blockheader, hw_midstate, bake,
                                                        mJob.job_id.c_str(), mJob.ntime.c_str(), mWorker.extranonce2.c_str());
                                              #endif
                                            nonce_pool += NONCE_PER_JOB_HW;  // Sequential increment (wraparound automatic)
                                            #endif
                                          }

                                          // Quick batch insert under lock
                                          {
                                            std::lock_guard<std::mutex> lock(s_job_mutex);
                                            s_job_request_list_sw.splice(s_job_request_list_sw.end(), new_sw_jobs);
                                            #ifdef HARDWARE_SHA256
                                            s_job_request_list_hw.splice(s_job_request_list_hw.end(), new_hw_jobs);
                                            #endif
                                          }
                                      } 
                                      else
                                      {
                                        DEBUG_SERIAL_PRINTF("[ERROR][JOB] ❌ Mining notify parse FAILED (line: %.100s) - reconnecting\n", line.c_str());
                                        client.stop();
                                        isMinerSuscribed.store(false, std::memory_order_release);
                                        MiningJobStop(job_pool, s_submission_map);
                                        first_job_received = false;
                                      }
                                      break;
          case MINING_SET_DIFFICULTY:{
                                      parse_mining_set_difficulty(line.c_str(), currentPoolDifficulty);
                                      DEBUG_SERIAL_PRINTF("[DIFFICULTY] Pool set difficulty: %.2f\n", currentPoolDifficulty);

                                      break;
                                      }
          case STRATUM_SUCCESS:       {
                                        unsigned long id = parse_extract_id(line.c_str());
                                        auto itt = s_submission_map.find(id);
                                        if (itt != s_submission_map.end())
                                        {
                                          DEBUG_SERIAL_PRINTF("[SHARE] ✓ Share accepted (diff: %f)\n", itt->second->diff);
                                          // Update best_diff if new difficulty is higher (single writer, safe without CAS)
                                          float current = best_diff.load(std::memory_order_relaxed);
                                          if (itt->second->diff > current)
                                          {
                                            best_diff.store(itt->second->diff, std::memory_order_relaxed);
                                          }
                                          if (itt->second->is32bit)
                                            shares.fetch_add(1, std::memory_order_relaxed);
                                          if (itt->second->isValid)
                                          {
                                            DEBUG_SERIAL_PRINTLN("🎉 CONGRATULATIONS! Valid block found! 🎉");
                                            valids.fetch_add(1, std::memory_order_relaxed);
                                          }
                                          s_submission_map.erase(itt);
                                        } 
                                        else 
                                          DEBUG_SERIAL_PRINTF("[SHARE] ✓ Success response (ID: %lu, not tracked)\n", id);
                                      }
                                      break;
          case STRATUM_PARSE_ERROR:   {
                                        unsigned long id = parse_extract_id(line.c_str());
                                        auto itt = s_submission_map.find(id);
                                        if (itt != s_submission_map.end())
                                        {
                                          DEBUG_SERIAL_PRINTF("[ERROR] ❌ Pool rejected submission %lu (msg: %.150s)\n", id, line.c_str());
                                          s_submission_map.erase(itt);
                                        } 
                                        else
                                          DEBUG_SERIAL_PRINTF("[ERROR] ❌ Parse error (msg: %.150s)\n", line.c_str());
                                      }
                                      break;

          default:                    DEBUG_SERIAL_PRINTF("[MSG] Unhandled result type: %d\n", result); break;

      }

    }

    std::list<std::shared_ptr<JobResult>> job_result_list;

    vTaskDelay(200 / portTICK_PERIOD_MS);


    if (job_pool != 0xFFFFFFFF)
    {
      // Prepare jobs outside lock to minimize contention
      std::list<std::shared_ptr<JobRequest>> new_jobs_sw;
      #ifdef HARDWARE_SHA256
      std::list<std::shared_ptr<JobRequest>> new_jobs_hw;
      #endif

      size_t current_sw_size, current_hw_size = 0;
      {
        // Quick check of queue sizes
        std::lock_guard<std::mutex> lock(s_job_mutex);
        current_sw_size = s_job_request_list_sw.size();
        #ifdef HARDWARE_SHA256
        current_hw_size = s_job_request_list_hw.size();
        #endif
      }

      // Incremental refill SW: Create jobs outside lock
      constexpr size_t REFILL_THRESHOLD_SW = JOB_QUEUE_SIZE / 2;
      if (current_sw_size < REFILL_THRESHOLD_SW)
      {
        size_t space_available = JOB_QUEUE_SIZE - current_sw_size;
        size_t jobs_to_create = (space_available < JOB_REFILL_BATCH) ? space_available : JOB_REFILL_BATCH;
        for (size_t i = 0; i < jobs_to_create; ++i)
        {
          JobPush(new_jobs_sw, job_pool, nonce_pool, NONCE_PER_JOB_SW, currentPoolDifficulty, mMiner.bytearray_blockheader, diget_mid, bake,
                  mJob.job_id.c_str(), mJob.ntime.c_str(), mWorker.extranonce2.c_str());
          nonce_pool += NONCE_PER_JOB_SW;  // Sequential increment (wraparound automatic)
        }
      }

      #ifdef HARDWARE_SHA256
      // Incremental refill HW: Create jobs outside lock
      constexpr size_t REFILL_THRESHOLD_HW = JOB_QUEUE_SIZE / 2;
      if (current_hw_size < REFILL_THRESHOLD_HW)
      {
        size_t space_available = JOB_QUEUE_SIZE - current_hw_size;
        size_t jobs_to_create = (space_available < JOB_REFILL_BATCH) ? space_available : JOB_REFILL_BATCH;
        for (size_t i = 0; i < jobs_to_create; ++i)
        {
          #if defined(CONFIG_IDF_TARGET_ESP32)
            JobPush(new_jobs_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, sha_buffer_swap, hw_midstate, bake,
                    mJob.job_id.c_str(), mJob.ntime.c_str(), mWorker.extranonce2.c_str());
          #else
            JobPush(new_jobs_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, mMiner.bytearray_blockheader, hw_midstate, bake,
                    mJob.job_id.c_str(), mJob.ntime.c_str(), mWorker.extranonce2.c_str());
          #endif
          nonce_pool += NONCE_PER_JOB_HW;  // Sequential increment (wraparound automatic)
        }
      }
      #endif

      // Fast insertion under lock using splice
      {
        std::lock_guard<std::mutex> lock(s_job_mutex);
        job_result_list.insert(job_result_list.end(), s_job_result_list.begin(), s_job_result_list.end());
        s_job_result_list.clear();

        if (!new_jobs_sw.empty()) {
          s_job_request_list_sw.splice(s_job_request_list_sw.end(), new_jobs_sw);
        }
        #ifdef HARDWARE_SHA256
        if (!new_jobs_hw.empty()) {
          s_job_request_list_hw.splice(s_job_request_list_hw.end(), new_jobs_hw);
        }
        #endif
      }
    }

    // Batch atomic hash updates: accumulate locally, update once at end
    uint32_t total_hashes_processed = 0;

    // Convert current pool difficulty to target for precise comparison (avoids float precision errors)
    uint8_t pool_target[32];
    if (!difficulty_to_target(currentPoolDifficulty, pool_target)) {
        memset(pool_target, 0xFF, 32);  // Fallback to max target if conversion fails
    }

    while (!job_result_list.empty())
    {
      std::shared_ptr<JobResult> res = job_result_list.front();
      job_result_list.pop_front();

      // Only count actually processed nonces in hashrate (skipped nonces weren't computed)
      if (res->nonce_count > res->nonces_skipped) 
      {
        total_hashes_processed += (res->nonce_count - res->nonces_skipped);
      }
      if (checkValid(res->hash, pool_target) && job_pool == res->id && res->nonce != 0xFFFFFFFF)
      {
        if (!client.connected())
          break;
        unsigned long sumbit_id = 0;
        tx_mining_submit(client, mWorker.wName, res->job_id, res->extranonce2, res->ntime, res->nonce, sumbit_id);

        #ifdef DEBUG_MINING
        DEBUG_SERIAL_PRINT("   - Current diff share: "); DEBUG_SERIAL_PRINTLN(res->difficulty,12);
        DEBUG_SERIAL_PRINT("   - Current pool diff : "); DEBUG_SERIAL_PRINTLN(currentPoolDifficulty,12);
        DEBUG_SERIAL_PRINT("   - TX SHARE: ");
        for (size_t i = 0; i < 32; i++)
            DEBUG_SERIAL_PRINTF("%02x", res->hash[i]);
        DEBUG_SERIAL_PRINTLN("");
        #endif
        
        mLastTXtoPool = millis();

        std::shared_ptr<Submission> submission = std::make_shared<Submission>();
        submission->diff = res->difficulty;
        submission->is32bit = (res->hash[29] == 0 && res->hash[28] == 0);
        submission->timestamp_ms = millis();  // Record submission time
        if (submission->is32bit)
        {
          submission->isValid = checkValid(res->hash, mMiner.bytearray_target);
        } 
        else
          submission->isValid = false;

          s_submission_map.insert(std::make_pair(sumbit_id, submission));

          // Second pass: If still oversized, evict oldest entries (shouldn't happen often)
          while (s_submission_map.size() > SUBMISSION_MAP_MAX) 
          {
            DEBUG_SERIAL_PRINTF("[WARN] Evicting oldest submission ID=%lu (map full)\n", s_submission_map.begin()->first);
            s_submission_map.erase(s_submission_map.begin());
          }
      }
    }

    // Apply batched hash update (single atomic operation instead of per-result)
    if (total_hashes_processed > 0)
      hashes.fetch_add(total_hashes_processed, std::memory_order_relaxed);
  }
}

//////////////////THREAD CALLS///////////////////

void minerWorkerSw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  DEBUG_SERIAL_PRINTF("[MINER] %d Started minerWorkerSw Task!\n", miner_id);

  std::shared_ptr<JobRequest> job;
  std::shared_ptr<JobResult> result;
  uint8_t hash[32];
  uint32_t wdt_counter = 0;
  while (1)
  {
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (result)
      {
        if (s_job_result_list.size() < RESULT_LIST_SIZE) 
        {
            s_job_result_list.push_back(result);
            // DEBUG_SERIAL_PRINTF("[RESULT] ✅ Diff: %.8f | Nonce: %u | Stored (%u/%u)\n", result->difficulty, result->nonce, (unsigned)s_job_result_list.size(), RESULT_LIST_SIZE);
        }

        result.reset();
      }
      if (!s_job_request_list_sw.empty())
      {
        job = s_job_request_list_sw.front();
        s_job_request_list_sw.pop_front();
      } else
        job.reset();
    }
    if (job)
    {
      result = std::make_shared<JobResult>();
      memset(result.get(), 0, sizeof(JobResult));
      result->difficulty = job->difficulty;
      result->nonce = 0xFFFFFFFF;
      result->id = job->id;
      result->nonce_count = job->nonce_count;
      result->nonces_skipped = 0;  // SW workers don't skip nonces (no SHA hardware)
      initialize_best_hash(*result);
      strncpy(result->job_id, job->job_id, sizeof(result->job_id) - 1);
      strncpy(result->ntime, job->ntime, sizeof(result->ntime) - 1);
      strncpy(result->extranonce2, job->extranonce2, sizeof(result->extranonce2) - 1);
      uint8_t job_in_work = job->id & 0xFF;
      for (uint32_t n = 0; n < job->nonce_count; ++n)
      {
        uint32_t nonce = job->nonce_start + n;
        if (nerd_sha256d_baked_nonce(job->midstate, job->bake, __builtin_bswap32(nonce), hash))
        {
          if (isSha256Valid(hash) && hash_is_better(hash, result->best_hash))
          {
            double diff_hash = diff_from_target(hash);
            result->difficulty = diff_hash;
            result->nonce = nonce;
            memcpy(result->hash, hash, 32);
            memcpy(result->best_hash, hash, 32);
          }
        }

        if ( (uint16_t)(n & 0xFF) == 0 && s_working_current_job_id.load(std::memory_order_acquire) != job_in_work)
        {
          result->nonce_count = n+1;
          break;
        }
      }
    } 
    else
      vTaskDelay(2 / portTICK_PERIOD_MS);

    wdt_counter++;
    if (wdt_counter >= WDT_COUNTER)
    {
      wdt_counter = 0;
      esp_task_wdt_reset();
    }
  }
}

#ifdef HARDWARE_SHA256

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)

// Optimized register write - minimize overhead with inline and compiler hints
static inline __attribute__((always_inline, hot)) void nerd_sha_ll_fill_text_block_sha256(const void *input_text, uint32_t nonce)
{
    const uint32_t *data_words = (const uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    // Write variable data (first 4 words)
    REG_WRITE(&reg_addr_buf[0], data_words[0]);
    REG_WRITE(&reg_addr_buf[1], data_words[1]);
    REG_WRITE(&reg_addr_buf[2], data_words[2]);
    REG_WRITE(&reg_addr_buf[3], nonce);

    // Write padding constants (optimized: compiler should merge these)
    REG_WRITE(&reg_addr_buf[4], 0x00000080);
    // Zero fill middle section
    REG_WRITE(&reg_addr_buf[5], 0);
    REG_WRITE(&reg_addr_buf[6], 0);
    REG_WRITE(&reg_addr_buf[7], 0);
    REG_WRITE(&reg_addr_buf[8], 0);
    REG_WRITE(&reg_addr_buf[9], 0);
    REG_WRITE(&reg_addr_buf[10], 0);
    REG_WRITE(&reg_addr_buf[11], 0);
    REG_WRITE(&reg_addr_buf[12], 0);
    REG_WRITE(&reg_addr_buf[13], 0);
    REG_WRITE(&reg_addr_buf[14], 0);
    // Final padding
    REG_WRITE(&reg_addr_buf[15], 0x80020000);
}

// Optimized intermediate hash preparation - critical for double SHA-256
static inline __attribute__((always_inline, hot)) void nerd_sha_ll_fill_text_block_sha256_inter()
{
  uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

  // Read digest from hardware in single critical section
  DPORT_INTERRUPT_DISABLE();
  uint32_t h0 = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4);
  uint32_t h1 = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4);
  uint32_t h2 = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4);
  uint32_t h3 = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4);
  uint32_t h4 = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4);
  uint32_t h5 = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4);
  uint32_t h6 = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4);
  uint32_t h7 = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4);
  DPORT_INTERRUPT_RESTORE();

  // Write digest to text buffer (outside critical section)
  REG_WRITE(&reg_addr_buf[0], h0);
  REG_WRITE(&reg_addr_buf[1], h1);
  REG_WRITE(&reg_addr_buf[2], h2);
  REG_WRITE(&reg_addr_buf[3], h3);
  REG_WRITE(&reg_addr_buf[4], h4);
  REG_WRITE(&reg_addr_buf[5], h5);
  REG_WRITE(&reg_addr_buf[6], h6);
  REG_WRITE(&reg_addr_buf[7], h7);

  // Write padding (SHA-256 padding for 256-bit message)
  REG_WRITE(&reg_addr_buf[8], 0x00000080);
  REG_WRITE(&reg_addr_buf[9], 0);
  REG_WRITE(&reg_addr_buf[10], 0);
  REG_WRITE(&reg_addr_buf[11], 0);
  REG_WRITE(&reg_addr_buf[12], 0);
  REG_WRITE(&reg_addr_buf[13], 0);
  REG_WRITE(&reg_addr_buf[14], 0);
  REG_WRITE(&reg_addr_buf[15], 0x00010000);
}

static inline void nerd_sha_ll_read_digest(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  ((uint32_t*)ptr)[0] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4);  
  ((uint32_t*)ptr)[7] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4);
  DPORT_INTERRUPT_RESTORE();
}


// Optimized early-exit hash validation - critical for performance
// Checks high-order bits first to quickly reject non-matching hashes
static inline __attribute__((always_inline, hot)) bool nerd_sha_ll_read_digest_if(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();

  // Fast rejection: check last word's top 16 bits (difficulty indicator)
  // Most hashes fail this check, allowing quick early exit
  uint32_t last = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4);
  if (__builtin_expect((last >> 16) != 0, 1))  // Likely case: not a valid share
  {
    DPORT_INTERRUPT_RESTORE();
    return false;
  }

  // Potential valid hash - read remaining digest
  // Store last word first since we already read it
  ((uint32_t*)ptr)[7] = last;
  ((uint32_t*)ptr)[0] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4);
  DPORT_INTERRUPT_RESTORE();
  return true;
}

// Optimized midstate write - writes 256-bit digest to SHA hardware
static inline __attribute__((always_inline, hot)) void nerd_sha_ll_write_digest(void *digest_state)
{
    const uint32_t *digest_state_words = (const uint32_t *)digest_state;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_H_BASE);

    // Unrolled loop for maximum performance (8x 32-bit writes)
    REG_WRITE(&reg_addr_buf[0], digest_state_words[0]);
    REG_WRITE(&reg_addr_buf[1], digest_state_words[1]);
    REG_WRITE(&reg_addr_buf[2], digest_state_words[2]);
    REG_WRITE(&reg_addr_buf[3], digest_state_words[3]);
    REG_WRITE(&reg_addr_buf[4], digest_state_words[4]);
    REG_WRITE(&reg_addr_buf[5], digest_state_words[5]);
    REG_WRITE(&reg_addr_buf[6], digest_state_words[6]);
    REG_WRITE(&reg_addr_buf[7], digest_state_words[7]);
}

// Optimized wait with fast path - most operations complete quickly
static inline __attribute__((always_inline, hot)) bool nerd_sha_hal_wait_idle()
{
    // Fast path: check if already idle (common case)
    if (__builtin_expect(!REG_READ(SHA_BUSY_REG), 1)) {
        return true;
    }

    // Progressive timeout: tight loop first, then with nops
    uint32_t fast_timeout = SHA_HARDWARE_TIMEOUT_CYCLES >> 2;  // 25% of timeout for fast check
    while (--fast_timeout > 0) {
        if (!REG_READ(SHA_BUSY_REG)) return true;
    }

    // Slower path with CPU yield
    uint32_t slow_timeout = (SHA_HARDWARE_TIMEOUT_CYCLES >> 2) * 3;  // Remaining 75%
    while (--slow_timeout > 0) {
        if (!REG_READ(SHA_BUSY_REG)) return true;
        asm volatile("nop");
    }

    // Timeout occurred
    if (__builtin_expect(slow_timeout == 0, 0)) {
        uint32_t count = sha_timeout_count.fetch_add(1, std::memory_order_relaxed);
        if (count % 1000 == 0 && count > 0) {
            DEBUG_SERIAL_PRINTF("[SHA] Hardware timeout count: %lu\n", count);
        }
        return false;
    }
    return true;
}

// Reset SHA hardware to known state after timeout or error
static inline void nerd_sha_hw_reset()
{
    // Set SHA mode to reset the state machine
    REG_WRITE(SHA_MODE_REG, SHA2_256);
    // Give hardware time to reset
    asm volatile("nop");
    asm volatile("nop");
    asm volatile("nop");
    asm volatile("nop");
}

//#define VALIDATION
// Performance-critical mining function - optimized for ESP32-S3 SHA hardware
__attribute__((hot, optimize("Ofast"))) void minerWorkerHw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  DEBUG_SERIAL_PRINTF("[MINER] %d Started minerWorkerHw Task!\n", miner_id);

  std::shared_ptr<JobRequest> job;
  std::shared_ptr<JobResult> result;

  // Align buffers to 16 bytes for optimal memory access performance
  // Maximum stack alignment on ESP32 architecture
  // This improves cache line utilization and memory access patterns
  uint8_t interResult[64];
  uint8_t hash[32];
  uint8_t digest_mid[32];
  uint8_t sha_buffer[64];

  uint32_t wdt_counter = 0;

#ifdef VALIDATION
  uint8_t doubleHash[32];
  uint32_t diget_mid[8];
  uint32_t bake[16];
#endif

  while (1)
  {
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (result)
      {
        if (s_job_result_list.size() < RESULT_LIST_SIZE)
          s_job_result_list.push_back(result);
        result.reset();
      }
      if (!s_job_request_list_hw.empty())
      {
        job = s_job_request_list_hw.front();
        s_job_request_list_hw.pop_front();
      } else
        job.reset();
    }
    if (job)
    {
      result = std::make_shared<JobResult>();
      memset(result.get(), 0, sizeof(JobResult));
      result->id = job->id;
      result->nonce = 0xFFFFFFFF;
      result->nonce_count = job->nonce_count;
      result->nonces_skipped = 0;  // Track SHA hardware timeouts
      result->difficulty = job->difficulty;
      initialize_best_hash(*result);
      strncpy(result->job_id, job->job_id, sizeof(result->job_id) - 1);
      strncpy(result->ntime, job->ntime, sizeof(result->ntime) - 1);
      strncpy(result->extranonce2, job->extranonce2, sizeof(result->extranonce2) - 1);
      uint8_t job_in_work = job->id & 0xFF;
      memcpy(digest_mid, job->midstate, sizeof(digest_mid));
      memcpy(sha_buffer, job->sha_buffer+64, sizeof(sha_buffer));
#ifdef VALIDATION
      nerd_mids(diget_mid, job->sha_buffer);
      nerd_sha256_bake(diget_mid, job->sha_buffer+64, bake);
#endif

      esp_sha_acquire_hardware();
      REG_WRITE(SHA_MODE_REG, SHA2_256);
      uint32_t nend = job->nonce_start + job->nonce_count;

      // Cache job cancellation check value to reduce atomic loads
      uint8_t local_job_in_work = job_in_work;
      uint8_t current_job_id;

      for (uint32_t n = job->nonce_start; n < nend; ++n)
      {
        // Optimized SHA operation sequence - removed redundant waits
        nerd_sha_ll_write_digest(digest_mid);
        nerd_sha_ll_fill_text_block_sha256(sha_buffer, n);
        REG_WRITE(SHA_CONTINUE_REG, 1);
        sha_ll_load(SHA2_256);

        // Wait for first round with likely success hint
        if (__builtin_expect(!nerd_sha_hal_wait_idle(), 0)) {
          DEBUG_SERIAL_PRINTF("[SHA_HW] Timeout at nonce 0x%08X (job %u, round 1, worker %u)\n",
                              n, job->id, miner_id);
          nerd_sha_hw_reset();
          result->nonces_skipped++;
          continue;
        }

        nerd_sha_ll_fill_text_block_sha256_inter();
        REG_WRITE(SHA_START_REG, 1);
        sha_ll_load(SHA2_256);

        // Wait for second round with likely success hint
        if (__builtin_expect(!nerd_sha_hal_wait_idle(), 0)) {
          DEBUG_SERIAL_PRINTF("[SHA_HW] Timeout at nonce 0x%08X (job %u, round 2, worker %u)\n",
                              n, job->id, miner_id);
          nerd_sha_hw_reset();
          result->nonces_skipped++;
          continue;
        }

        // Early exit hash check - only processes promising results
        if (__builtin_expect(nerd_sha_ll_read_digest_if(hash), 0))
        {
#ifdef VALIDATION
          // Validation (debug only)
          nerd_sha256d_baked_nonce(diget_mid, bake, __builtin_bswap32(n), doubleHash);
          for (int i = 0; i < 32; ++i)
          {
            if (hash[i] != doubleHash[i])
            {
              DEBUG_SERIAL_PRINTLN("***HW sha256 esp32s3 bug detected***");
              break;
            }
          }
#endif
          // Valid candidate found - process it
          if (__builtin_expect(isSha256Valid(hash) && hash_is_better(hash, result->best_hash), 1))
          {
            double diff_hash = diff_from_target(hash);
            result->difficulty = diff_hash;
            result->nonce = n;
            memcpy(result->hash, hash, sizeof(hash));
            memcpy(result->best_hash, hash, sizeof(hash));
          }
        }

        // Check for job cancellation at configured frequency
        // Reduces atomic load overhead while maintaining responsiveness
        if (__builtin_expect((n & JOB_CANCELLATION_CHECK_MASK) == 0, 0))
        {
          current_job_id = s_working_current_job_id.load(std::memory_order_acquire);
          if (__builtin_expect(current_job_id != local_job_in_work, 0))
          {
            result->nonce_count = n - job->nonce_start + 1;
            break;
          }
        }
      }
      esp_sha_release_hardware();
    } 
    else
      vTaskDelay(2 / portTICK_PERIOD_MS);

    wdt_counter++;
    if (wdt_counter >= WDT_COUNTER)
    {
      wdt_counter = 0;
      esp_task_wdt_reset();
    }
  }
}

#endif  //#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)

#if defined(CONFIG_IDF_TARGET_ESP32)

static inline bool nerd_sha_ll_read_digest_swap_if(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  uint32_t fin = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4);
  if ( (uint32_t)(fin & 0xFFFF) != 0)
  {
    DPORT_INTERRUPT_RESTORE();
    return false;
  }
  ((uint32_t*)ptr)[7] = __builtin_bswap32(fin);
  ((uint32_t*)ptr)[0] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4));
  ((uint32_t*)ptr)[1] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4));
  ((uint32_t*)ptr)[2] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4));
  ((uint32_t*)ptr)[3] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4));
  ((uint32_t*)ptr)[4] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4));
  ((uint32_t*)ptr)[5] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4));
  ((uint32_t*)ptr)[6] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4));
  DPORT_INTERRUPT_RESTORE();
  return true;
}

static inline void nerd_sha_ll_read_digest(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  ((uint32_t*)ptr)[0] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4);
  ((uint32_t*)ptr)[7] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4);
  DPORT_INTERRUPT_RESTORE();
}

static inline bool nerd_sha_hal_wait_idle()
{
    uint32_t timeout = SHA_HARDWARE_TIMEOUT_CYCLES;
    while (DPORT_REG_READ(SHA_256_BUSY_REG) && --timeout > 0)
    {
        asm volatile("nop");
    }
    if (timeout == 0) {
        uint32_t count = sha_timeout_count.fetch_add(1, std::memory_order_relaxed);
        if (count % 1000 == 0 && count > 0) {
            DEBUG_SERIAL_PRINTF("[SHA] Hardware timeout count: %lu\n", count);
        }
        return false;
    }
    return true;
}

// Reset SHA hardware to known state after timeout or error
static inline void nerd_sha_hw_reset()
{
    // For ESP32 classic, we need to use DPORT access
    // Reset by reconfiguring the SHA mode
    sha_ll_start_block(SHA2_256);
    // Give hardware time to reset
    asm volatile("nop");
    asm volatile("nop");
    asm volatile("nop");
    asm volatile("nop");
}

static inline void nerd_sha_ll_fill_text_block_sha256(const void *input_text)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    reg_addr_buf[0]  = data_words[0];
    reg_addr_buf[1]  = data_words[1];
    reg_addr_buf[2]  = data_words[2];
    reg_addr_buf[3]  = data_words[3];
    reg_addr_buf[4]  = data_words[4];
    reg_addr_buf[5]  = data_words[5];
    reg_addr_buf[6]  = data_words[6];
    reg_addr_buf[7]  = data_words[7];
    reg_addr_buf[8]  = data_words[8];
    reg_addr_buf[9]  = data_words[9];
    reg_addr_buf[10] = data_words[10];
    reg_addr_buf[11] = data_words[11];
    reg_addr_buf[12] = data_words[12];
    reg_addr_buf[13] = data_words[13];
    reg_addr_buf[14] = data_words[14];
    reg_addr_buf[15] = data_words[15];
}

static inline void nerd_sha_ll_fill_text_block_sha256_upper(const void *input_text, uint32_t nonce)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    reg_addr_buf[0]  = data_words[0];
    reg_addr_buf[1]  = data_words[1];
    reg_addr_buf[2]  = data_words[2];
    reg_addr_buf[3]  = __builtin_bswap32(nonce);
    reg_addr_buf[4]  = 0x80000000;
    reg_addr_buf[5]  = 0x00000000;
    reg_addr_buf[6]  = 0x00000000;
    reg_addr_buf[7]  = 0x00000000;
    reg_addr_buf[8]  = 0x00000000;
    reg_addr_buf[9]  = 0x00000000;
    reg_addr_buf[10] = 0x00000000;
    reg_addr_buf[11] = 0x00000000;
    reg_addr_buf[12] = 0x00000000;
    reg_addr_buf[13] = 0x00000000;
    reg_addr_buf[14] = 0x00000000;
    reg_addr_buf[15] = 0x00000280;
}

static inline void nerd_sha_ll_fill_text_block_sha256_double()
{
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    // First 8 words remain unchanged from previous hash
    reg_addr_buf[8]  = 0x80000000;
    reg_addr_buf[9]  = 0x00000000;
    reg_addr_buf[10] = 0x00000000;
    reg_addr_buf[11] = 0x00000000;
    reg_addr_buf[12] = 0x00000000;
    reg_addr_buf[13] = 0x00000000;
    reg_addr_buf[14] = 0x00000000;
    reg_addr_buf[15] = 0x00000100;
}

void minerWorkerHw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  DEBUG_SERIAL_PRINTF("[MINER] %d Started minerWorkerHwEsp32D Task!\n", miner_id);

  std::shared_ptr<JobRequest> job;
  std::shared_ptr<JobResult> result;
  uint8_t hash[32];
  uint8_t sha_buffer[128];

  while (1)
  {
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (result)
      {
        if (s_job_result_list.size() < RESULT_LIST_SIZE)
          s_job_result_list.push_back(result);
        result.reset();
      }
      if (!s_job_request_list_hw.empty())
      {
        job = s_job_request_list_hw.front();
        s_job_request_list_hw.pop_front();
      } else
        job.reset();
    }
    if (job)
    {
      result = std::make_shared<JobResult>();
      memset(result.get(), 0, sizeof(JobResult));
      result->id = job->id;
      result->nonce = 0xFFFFFFFF;
      result->nonce_count = job->nonce_count;
      result->nonces_skipped = 0;  // Track SHA hardware timeouts
      result->difficulty = job->difficulty;
      initialize_best_hash(*result);
      strncpy(result->job_id, job->job_id, sizeof(result->job_id) - 1);
      strncpy(result->ntime, job->ntime, sizeof(result->ntime) - 1);
      strncpy(result->extranonce2, job->extranonce2, sizeof(result->extranonce2) - 1);
      uint8_t job_in_work = job->id & 0xFF;
      memcpy(sha_buffer, job->sha_buffer, 80);

      esp_sha_lock_engine(SHA2_256);
      for (uint32_t n = 0; n < job->nonce_count; ++n)
      {
        //((uint32_t*)(sha_buffer+64+12))[0] = __builtin_bswap32(job->nonce_start+n);

        //sha_hal_hash_block(SHA2_256, s_test_buffer, 64/4, true);
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256(sha_buffer);
        sha_ll_start_block(SHA2_256);

        //sha_hal_hash_block(SHA2_256, s_test_buffer+64, 64/4, false);
        if (!nerd_sha_hal_wait_idle()) {
          // Reset SHA hardware to known state after timeout
          DEBUG_SERIAL_PRINTF("[SHA_HW_ESP32] Timeout at nonce 0x%08X (job %u, stage 1, worker %u)\n",
                              job->nonce_start+n, job->id, miner_id);
          nerd_sha_hw_reset();
          result->nonces_skipped++;
          continue;
        }
        nerd_sha_ll_fill_text_block_sha256_upper(sha_buffer+64, job->nonce_start+n);
        sha_ll_continue_block(SHA2_256);

        if (!nerd_sha_hal_wait_idle()) {
          // Reset SHA hardware to known state after timeout
          DEBUG_SERIAL_PRINTF("[SHA_HW_ESP32] Timeout at nonce 0x%08X (job %u, stage 2, worker %u)\n",
                              job->nonce_start+n, job->id, miner_id);
          nerd_sha_hw_reset();
          result->nonces_skipped++;
          continue;
        }
        sha_ll_load(SHA2_256);

        //sha_hal_hash_block(SHA2_256, interResult, 64/4, true);
        if (!nerd_sha_hal_wait_idle()) {
          // Reset SHA hardware to known state after timeout
          DEBUG_SERIAL_PRINTF("[SHA_HW_ESP32] Timeout at nonce 0x%08X (job %u, stage 3, worker %u)\n",
                              job->nonce_start+n, job->id, miner_id);
          nerd_sha_hw_reset();
          result->nonces_skipped++;
          continue;
        }
        nerd_sha_ll_fill_text_block_sha256_double();
        sha_ll_start_block(SHA2_256);

        if (!nerd_sha_hal_wait_idle()) {
          // Reset SHA hardware to known state after timeout
          DEBUG_SERIAL_PRINTF("[SHA_HW_ESP32] Timeout at nonce 0x%08X (job %u, stage 4, worker %u)\n",
                              job->nonce_start+n, job->id, miner_id);
          nerd_sha_hw_reset();
          result->nonces_skipped++;
          continue;
        }
        sha_ll_load(SHA2_256);
        if (nerd_sha_ll_read_digest_swap_if(hash))
        {
          //~5 per second
          if (isSha256Valid(hash) && hash_is_better(hash, result->best_hash))
          {
            double diff_hash = diff_from_target(hash);
            result->difficulty = diff_hash;
            result->nonce = job->nonce_start+n;
            memcpy(result->hash, hash, sizeof(hash));
            memcpy(result->best_hash, hash, sizeof(hash));
          }
        }
        if (
             (uint8_t)(n & 0xFF) == 0 &&
             s_working_current_job_id.load(std::memory_order_acquire) != job_in_work)
        {
          result->nonce_count = n+1;
          break;
        }
      }
      esp_sha_unlock_engine(SHA2_256);
    } 
    else
      vTaskDelay(2 / portTICK_PERIOD_MS);

    esp_task_wdt_reset();
  }
}

#endif  //CONFIG_IDF_TARGET_ESP32

#endif  //HARDWARE_SHA256


#define DELAY 2000  // Reduced from 1000ms to 2000ms (1Hz -> 0.5Hz) to save CPU cycles for mining
#define REDRAW_EVERY 10

void restoreStat() 
{
  if(!Settings.saveStats) return;
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    DEBUG_SERIAL_PRINTF("[MONITOR] NVS partition is full or has invalid version, erasing...\n");
    nvs_flash_init();
  }

  ret = nvs_open("state", NVS_READWRITE, &stat_handle);

  // Initialize all variables to prevent using garbage data if nvs_get_* fails
  float local_best_diff = 0.0f;
  uint32_t nv_Mhashes = 0, nv_templates = 0;
  uint64_t nv_upTime = 0;
  uint32_t nv_shares = 0, nv_valids = 0;

  // Read all values and check for errors
  size_t required_size = sizeof(float);
  esp_err_t err_diff = nvs_get_blob(stat_handle, "best_diff", &local_best_diff, &required_size);
  esp_err_t err_mhashes = nvs_get_u32(stat_handle, "Mhashes", &nv_Mhashes);
  esp_err_t err_shares = nvs_get_u32(stat_handle, "shares", &nv_shares);
  esp_err_t err_valids = nvs_get_u32(stat_handle, "valids", &nv_valids);
  esp_err_t err_templates = nvs_get_u32(stat_handle, "templates", &nv_templates);
  esp_err_t err_uptime = nvs_get_u64(stat_handle, "upTime", &nv_upTime);

  // Only validate CRC if all reads succeeded
  bool all_reads_ok = (err_diff == ESP_OK && err_mhashes == ESP_OK &&
                       err_shares == ESP_OK && err_valids == ESP_OK &&
                       err_templates == ESP_OK && err_uptime == ESP_OK);

  uint32_t crc = crc32_reset();
  crc = crc32_add(crc, &local_best_diff, sizeof(local_best_diff));
  crc = crc32_add(crc, &nv_Mhashes, sizeof(nv_Mhashes));
  crc = crc32_add(crc, &nv_shares, sizeof(nv_shares));
  crc = crc32_add(crc, &nv_valids, sizeof(nv_valids));
  crc = crc32_add(crc, &nv_templates, sizeof(nv_templates));
  crc = crc32_add(crc, &nv_upTime, sizeof(nv_upTime));
  crc = crc32_finish(crc);

  uint32_t nv_crc = 0;
  esp_err_t crc_err = nvs_get_u32(stat_handle, "crc32", &nv_crc);
  if (!all_reads_ok || crc_err != ESP_OK || nv_crc != crc)
  {
    DEBUG_SERIAL_PRINTF("[MONITOR] CRC validation failed (err=%d), resetting stats\n", crc_err);
    best_diff.store(0.0f, std::memory_order_relaxed);
    Mhashes.store(0, std::memory_order_relaxed);
    shares.store(0, std::memory_order_relaxed);
    valids.store(0, std::memory_order_relaxed);
    templates.store(0, std::memory_order_relaxed);
    upTime.store(0, std::memory_order_relaxed);
  }
  else
  {
    // CRC valid, apply loaded values to atomics and best_diff
    best_diff.store(local_best_diff, std::memory_order_relaxed);
    Mhashes.store(nv_Mhashes, std::memory_order_relaxed);
    shares.store(nv_shares, std::memory_order_relaxed);
    valids.store(nv_valids, std::memory_order_relaxed);
    templates.store(nv_templates, std::memory_order_relaxed);
    upTime.store(nv_upTime, std::memory_order_relaxed);
  }
}

void saveStat() 
{
  if(!Settings.saveStats) return;
  DEBUG_SERIAL_PRINTF("[MONITOR] Saving stats\n");

  float local_best_diff = best_diff.load(std::memory_order_relaxed);

  // Load atomic values for saving
  uint32_t nv_Mhashes = Mhashes.load(std::memory_order_relaxed);
  uint32_t nv_shares = shares.load(std::memory_order_relaxed);
  uint32_t nv_valids = valids.load(std::memory_order_relaxed);
  uint32_t nv_templates = templates.load(std::memory_order_relaxed);
  uint64_t nv_upTime = upTime.load(std::memory_order_relaxed);

  esp_err_t err;
  err = nvs_set_blob(stat_handle, "best_diff", &local_best_diff, sizeof(local_best_diff));
  if (err != ESP_OK) DEBUG_SERIAL_PRINTF("[MONITOR] Failed to save best_diff: %d\n", err);

  err = nvs_set_u32(stat_handle, "Mhashes", nv_Mhashes);
  if (err != ESP_OK) DEBUG_SERIAL_PRINTF("[MONITOR] Failed to save Mhashes: %d\n", err);

  err = nvs_set_u32(stat_handle, "shares", nv_shares);
  if (err != ESP_OK) DEBUG_SERIAL_PRINTF("[MONITOR] Failed to save shares: %d\n", err);

  err = nvs_set_u32(stat_handle, "valids", nv_valids);
  if (err != ESP_OK) DEBUG_SERIAL_PRINTF("[MONITOR] Failed to save valids: %d\n", err);

  err = nvs_set_u32(stat_handle, "templates", nv_templates);
  if (err != ESP_OK) DEBUG_SERIAL_PRINTF("[MONITOR] Failed to save templates: %d\n", err);

  err = nvs_set_u64(stat_handle, "upTime", nv_upTime);
  if (err != ESP_OK) DEBUG_SERIAL_PRINTF("[MONITOR] Failed to save upTime: %d\n", err);

  uint32_t crc = crc32_reset();
  crc = crc32_add(crc, &local_best_diff, sizeof(local_best_diff));
  crc = crc32_add(crc, &nv_Mhashes, sizeof(nv_Mhashes));
  crc = crc32_add(crc, &nv_shares, sizeof(nv_shares));
  crc = crc32_add(crc, &nv_valids, sizeof(nv_valids));
  crc = crc32_add(crc, &nv_templates, sizeof(nv_templates));
  crc = crc32_add(crc, &nv_upTime, sizeof(nv_upTime));
  crc = crc32_finish(crc);

  err = nvs_set_u32(stat_handle, "crc32", crc);
  if (err != ESP_OK) DEBUG_SERIAL_PRINTF("[MONITOR] Failed to save crc32: %d\n", err);

  err = nvs_commit(stat_handle);
  if (err != ESP_OK) DEBUG_SERIAL_PRINTF("[MONITOR] Failed to commit NVS: %d\n", err);
}

void closeStat() 
{
    if (stat_handle != 0) {
        nvs_commit(stat_handle);
        nvs_close(stat_handle);
        stat_handle = 0;
        DEBUG_SERIAL_PRINTF("[MONITOR] NVS handle closed\n");
    }
}

void resetStat() 
{
    DEBUG_SERIAL_PRINTF("[MONITOR] Resetting NVS stats\n");
    templates.store(0, std::memory_order_relaxed);
    hashes.store(0, std::memory_order_relaxed);
    Mhashes.store(0, std::memory_order_relaxed);
    totalKHashes.store(0, std::memory_order_relaxed);
    elapsedKHs.store(0.0f, std::memory_order_relaxed);
    upTime.store(0, std::memory_order_relaxed);
    shares.store(0, std::memory_order_relaxed);
    valids.store(0, std::memory_order_relaxed);
    best_diff.store(0.0f, std::memory_order_relaxed);
    saveStat();
}

void runMonitor(void *name)
{

  DEBUG_SERIAL_PRINTLN("[MONITOR] started");
  restoreStat();

  uint32_t mLastCheck = 0;

  resetToFirstScreen();

  unsigned long frame = 0;

  uint32_t seconds_elapsed = 0;
  uint64_t lastTotalHashes =
      (static_cast<uint64_t>(Mhashes.load(std::memory_order_relaxed)) * 1000000ULL) +
      static_cast<uint64_t>(hashes.load(std::memory_order_relaxed));
  totalKHashes.store(static_cast<uint32_t>(lastTotalHashes / 1000ULL), std::memory_order_relaxed);
  elapsedKHs.store(0.0f, std::memory_order_relaxed);
  uint32_t last_update_millis = millis();
  uint32_t uptime_frac = 0;

  while (1)
  {
    uint32_t now_millis = millis();
    if (now_millis < last_update_millis)
      now_millis = last_update_millis;
    
    uint32_t mElapsed = now_millis - mLastCheck;
    if (mElapsed >= 1000)
    { 
      mLastCheck = now_millis;
      last_update_millis = now_millis;
      const uint64_t currentTotalHashes =
          (static_cast<uint64_t>(Mhashes.load(std::memory_order_relaxed)) * 1000000ULL) +
          static_cast<uint64_t>(hashes.load(std::memory_order_relaxed));
      uint64_t deltaHashes;
      if (currentTotalHashes >= lastTotalHashes)
        deltaHashes = currentTotalHashes - lastTotalHashes;
      else
        deltaHashes = currentTotalHashes;
      const float deltaKH = static_cast<float>(static_cast<double>(deltaHashes) / 1000.0);
      elapsedKHs.store(deltaKH, std::memory_order_relaxed);
      totalKHashes.store(static_cast<uint32_t>(currentTotalHashes / 1000ULL), std::memory_order_relaxed);
      lastTotalHashes = currentTotalHashes;

      uptime_frac += mElapsed;
      while (uptime_frac >= 1000)
      {
        uptime_frac -= 1000;
        upTime.fetch_add(1, std::memory_order_relaxed);
      }

      // Serial.printf("[HASHRATE] %.2f KH/s\n", (float)elapsedKHs * 1000.0 / mElapsed);

      drawCurrentScreen(mElapsed);

      // Monitor state when hashrate is 0.0
      if (elapsedKHs.load(std::memory_order_relaxed) <= 0.0f)
      {
        bool subscribed = isMinerSuscribed.load(std::memory_order_acquire);
        bool connected = s_client_connected.load(std::memory_order_acquire);
        DEBUG_SERIAL_PRINTF(">>> [i] Miner: newJob>%s / inRun>%s) - Client: connected>%s / subscribed>%s / wificonnected>%s\n",
            "true",//(1) ? "true" : "false",
            subscribed ? "true" : "false",
            connected ? "true" : "false", subscribed ? "true" : "false", WiFi.status() == WL_CONNECTED ? "true" : "false");
      }

      #ifdef DEBUG_MEMORY
      DEBUG_SERIAL_PRINTF("### [Total Heap / Free heap / Min free heap]: %d / %d / %d \n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
      DEBUG_SERIAL_PRINTF("### Max stack usage: %d\n", uxTaskGetStackHighWaterMark(NULL));
      #endif

      seconds_elapsed++;

      if(seconds_elapsed % (saveIntervals[currentIntervalIndex]) == 0){
        saveStat();
        seconds_elapsed = 0;
        if(currentIntervalIndex < saveIntervalsSize - 1)
          currentIntervalIndex++;
      }
    }
    // animateCurrentScreen has empty implementations in all current drivers
    // animateCurrentScreen(frame);
    // doLedStuff needed for ESP32-2432S028R backlight/touch control
    doLedStuff(frame);

    vTaskDelay(DELAY / portTICK_PERIOD_MS);
    frame++;
  }
}
