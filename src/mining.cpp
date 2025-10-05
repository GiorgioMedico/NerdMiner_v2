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
#define NONCE_PER_JOB_SW 16384   // Doubled for better throughput
#define NONCE_PER_JOB_HW 64*1024  // Doubled for better throughput

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
std::atomic<uint32_t> elapsedKHs{0};
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

IPAddress serverIP(1, 1, 1, 1); //Temporally save poolIPaddres

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
  // Fast path: WiFi check
  if (WiFi.status() != WL_CONNECTED) {
    s_client_connected.store(false, std::memory_order_release);
    isMinerSuscribed.store(false, std::memory_order_release);
    return false;
  }

  // Fast path: Already connected
  if (client.connected()) {
    s_client_connected.store(true, std::memory_order_release);
    return true;
  }

  // Slow path: Disconnected, attempt reconnection
  s_client_connected.store(false, std::memory_order_release);
  isMinerSuscribed.store(false, std::memory_order_release);

  DEBUG_SERIAL_PRINTLN("Client not connected, trying to connect...");

  // DNS resolution with caching (1 hour timeout - pools rarely change IPs)
  static bool dns_resolved = false;
  static uint32_t last_dns_resolve = 0;
  uint32_t now = millis();

  if (!dns_resolved || (now - last_dns_resolve > DNS_CACHE_DURATION_MS)) {
    if (WiFi.hostByName(Settings.PoolAddress.c_str(), serverIP) != 1) {
      DEBUG_SERIAL_PRINTLN("DNS resolution failed: " + Settings.PoolAddress);
      dns_resolved = false;
      return false;
    }
    dns_resolved = true;
    last_dns_resolve = now;
    DEBUG_SERIAL_PRINTF("Resolved DNS: %s\n", serverIP.toString().c_str());
  }

  // Connection attempt (ensure clean socket state)
  client.stop();
  if (!client.connect(serverIP, Settings.PoolPort)) {
    DEBUG_SERIAL_PRINTLN("Connection failed: " + Settings.PoolAddress);
    return false;
  }

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
    unsigned long elapsedKHs_local = elapsedKHs.load(std::memory_order_relaxed);

    uint32_t time_now = millis();

    // If no shares sent to pool, send something to pool to hold socket open
    adjust_for_wraparound(time_now, mLastTXtoPool);
    if ( time_now > mLastTXtoPool + keepAliveTime)
    {
      mLastTXtoPool = time_now;
      DEBUG_SERIAL_PRINTLN("  Sending  : KeepAlive suggest_difficulty");
      tx_suggest_difficulty(client, DEFAULT_DIFFICULTY);
      DEBUG_SERIAL_PRINTLN("  Sending keepAlive to pool -> Detected client disconnected");
    }

    if(elapsedKHs_local == 0)
    {
      //Check if hashrate is 0 during inactivityTIme
      if(mStart0Hashrate == 0) mStart0Hashrate  = time_now;
      if((time_now-mStart0Hashrate) > inactivityTime) { mStart0Hashrate=0; return true;}
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
};

struct JobResult
{
  uint32_t id;
  uint32_t nonce;
  uint32_t nonce_count;        // Successfully processed nonces
  uint32_t nonces_skipped;     // Nonces skipped due to SHA hardware timeout
  double difficulty;
  uint8_t hash[32];
};

static std::mutex s_job_mutex;
std::list<std::shared_ptr<JobRequest>> s_job_request_list_sw;
#ifdef HARDWARE_SHA256
std::list<std::shared_ptr<JobRequest>> s_job_request_list_hw;
#endif
std::list<std::shared_ptr<JobResult>> s_job_result_list;

// Atomic job ID for cross-thread work cancellation (proper memory barriers)
static std::atomic<uint8_t> s_working_current_job_id{0xFF};

static void JobPush(std::list<std::shared_ptr<JobRequest>> &job_list,  uint32_t id, uint32_t nonce_start, uint32_t nonce_count, double difficulty,
                    const uint8_t* sha_buffer, const uint32_t* midstate, const uint32_t* bake)
{
  std::shared_ptr<JobRequest> job = std::make_shared<JobRequest>();
  job->id = id;
  job->nonce_start = nonce_start;
  job->nonce_count = nonce_count;
  job->difficulty = difficulty;
  memcpy(job->sha_buffer, sha_buffer, sizeof(job->sha_buffer));
  memcpy(job->midstate, midstate, sizeof(job->midstate));
  memcpy(job->bake, bake, sizeof(job->bake));
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

  #ifdef DEBUG_MEMORY
  DEBUG_SERIAL_PRINTF("### [Total Heap / Free heap / Min free heap]: %d / %d / %d \n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
  #endif

  std::map<uint32_t, std::shared_ptr<Submission>> s_submission_map;

  // connect to pool
  double currentPoolDifficulty = DEFAULT_DIFFICULTY;
  uint32_t nonce_pool = 0;
  uint32_t job_pool = 0xFFFFFFFF;
  uint32_t last_job_time = millis();

  // Static buffer for accumulating partial pool responses (eliminates heap fragmentation)
  // Size must match MAX_POOL_LINE_SIZE to handle all valid stratum messages
  static char pending_buffer[MAX_POOL_LINE_SIZE + 1];
  static uint16_t pending_len = 0;

  while(true) 
  {
      
    if(WiFi.status() != WL_CONNECTED)
    {
      // WiFi is disconnected, so reconnect now
      mMonitor.NerdStatus.store(NM_Connecting, std::memory_order_release);
      s_client_connected.store(false, std::memory_order_release);
      MiningJobStop(job_pool, s_submission_map);
      pending_len = 0; // Drop any partial data from the old socket
      WiFi.reconnect();
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      continue;
    } 

    // Cache connection checks to reduce overhead (only recheck every CONN_CHECK_CACHE_MS)
    // TELEMETRY: Track cache hit rate by counting else branch vs total checks
    // Expected: 90%+ cache hits (100ms cache, 20ms loop = ~5 checks per cache refresh)
    static bool cached_connected = false;
    static uint32_t last_conn_check = 0;
    uint32_t now = millis();

    adjust_for_wraparound(now, last_conn_check);

    bool wasDisconnected = false;
    bool need_reconnect = false;

    // Only perform expensive connection check if cache expired or last check failed
    if (!cached_connected || (now - last_conn_check) >= CONN_CHECK_CACHE_MS) {
      wasDisconnected = !client.connected();
      cached_connected = checkPoolConnection();
      last_conn_check = now;
      need_reconnect = !cached_connected;
      // TELEMETRY: Increment cache_miss counter here
    } else {
      // Use cached result
      need_reconnect = !cached_connected;
      // TELEMETRY: Increment cache_hit counter here
    }

    if(need_reconnect)
    {
      //If server is not reachable add random delay for connection retries
      //Generate value between 1 and 60 secs
      MiningJobStop(job_pool, s_submission_map);
      vTaskDelay(((1 + rand() % 60) * 1000) / portTICK_PERIOD_MS);
      continue;
    }

    if (wasDisconnected) 
    {
      pending_len = 0; // Clear stale partial response after reconnecting
    }

    if(!isMinerSuscribed.load(std::memory_order_acquire))
    {
      //Stop miner current jobs
      mWorker = init_mining_subscribe();

      // STEP 1: Pool server connection (SUBSCRIBE)
      if(!tx_mining_subscribe(client, mWorker))
      {
        client.stop();
        cached_connected = false;  // Invalidate connection cache
        pending_len = 0;
        MiningJobStop(job_pool, s_submission_map);
        continue;
      }
      
      strcpy(mWorker.wName, Settings.BtcWallet);
      strcpy(mWorker.wPass, Settings.PoolPassword);
      // STEP 2: Pool authorize work (Block Info)
      tx_mining_auth(client, mWorker.wName, mWorker.wPass); //Don't verifies authoritzation, TODO
      //tx_mining_auth2(client, mWorker.wName, mWorker.wPass); //Don't verifies authoritzation, TODO

      // STEP 3: Suggest pool difficulty
      tx_suggest_difficulty(client, currentPoolDifficulty);

      isMinerSuscribed.store(true, std::memory_order_release);
      uint32_t time_now = millis();
      mLastTXtoPool = time_now;
      last_job_time = time_now;
    }

    //Check if pool is down for almost 5minutes and then restart connection with pool (1min=600000ms)
    if(checkPoolInactivity(KEEPALIVE_TIME_ms, POOLINACTIVITY_TIME_ms))
    {
      //Restart connection
      DEBUG_SERIAL_PRINTLN("  Detected more than 2 min without data form stratum server. Closing socket and reopening...");
      client.stop();
      cached_connected = false;  // Invalidate connection cache
      pending_len = 0;
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
        client.stop();
        cached_connected = false;  // Invalidate connection cache
        pending_len = 0;
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
    int available = client.available();
    while(client.connected() && available > 0)
    {
      // Accumulate data until we get a full line (preserve partial reads across socket timeouts)
      bool foundNewline = false;

      // Optimized bulk read: read chunks instead of byte-by-byte to reduce syscalls
      // Chunks align with typical stratum message sizes (200-800 bytes)
      while (available > 0 && pending_len < sizeof(pending_buffer) - 1)
      {
        size_t space_available = sizeof(pending_buffer) - pending_len - 1;
        size_t chunk_size = min((size_t)available, min(space_available, (size_t)CHUNK_READ_SIZE));

        uint8_t chunk[CHUNK_READ_SIZE];
        int bytes_read = client.readBytes(chunk, chunk_size);
        available = client.available();  // Refresh after reading

        for (int i = 0; i < bytes_read; i++)
        {
          char c = chunk[i];
          if (c == '\n')
          {
            foundNewline = true;
            break;
          }
          if (c != '\r')
          { // Skip carriage return
            // Defensive bounds check (should never overflow with correct chunk_size calculation)
            if (pending_len >= sizeof(pending_buffer) - 1) break;
            pending_buffer[pending_len++] = c;
          }
        }

        if (foundNewline) break;
      }

      // If we hit the size limit without finding newline, discard rest of line and disconnect
      if (!foundNewline && pending_len >= sizeof(pending_buffer) - 1)
      {
        DEBUG_SERIAL_PRINTF("Pool response too large (%u bytes), disconnecting\n", pending_len);
        // Consume remaining data on this line
        available = client.available();
        while (available > 0)
        {
          if (client.read() == '\n') break;
          available = client.available();
        }
        pending_len = 0;
        client.stop();
        cached_connected = false;  // Invalidate connection cache
        isMinerSuscribed.store(false, std::memory_order_release);
        MiningJobStop(job_pool, s_submission_map);
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Wait 5s before reconnecting
        break;
      }

      // Wait for more data if newline not found yet
      if (!foundNewline) 
      {
        continue;
      }

      // Null-terminate buffer (no String allocation needed)
      pending_buffer[pending_len] = '\0';
      pending_len = 0;

      // Ignore empty keepalive lines
      if (pending_buffer[0] == '\0')
      {
        continue;
      }
      //DEBUG_SERIAL_PRINTLN("  Received message from pool");

      // Fast-path message parsing: check for common patterns before full JSON parse
      // This reduces CPU overhead by avoiding unnecessary JSON deserialization
      // TELEMETRY: Track fast-path hit rate vs parse_mining_method() fallback calls
      // Expected: 90%+ fast-path (mining.notify + set_difficulty dominate traffic)
      // Alert if: fallback rate >10% (unexpected pool messages or protocol changes)
      stratum_method result;
      const char* msg = pending_buffer;

      // Quick check for method messages (most common during mining)
      if (msg[0] == '{' && strstr(msg, "\"method\"") != nullptr) {
        // Check for mining.notify (most frequent message type)
        if (strstr(msg, "mining.notify") != nullptr) {
          result = MINING_NOTIFY;
          // TELEMETRY: Increment fast_path_notify counter
        } else if (strstr(msg, "mining.set_difficulty") != nullptr) {
          result = MINING_SET_DIFFICULTY;
          // TELEMETRY: Increment fast_path_difficulty counter
        } else {
          // Uncommon method, use full parser
          result = parse_mining_method(msg);
          // TELEMETRY: Increment fast_path_fallback counter
        }
      } else if (msg[0] == '{' && strstr(msg, "\"result\"") != nullptr) {
        // Result message - check for success/error
        if (strstr(msg, "\"error\":null") != nullptr || strstr(msg, "\"result\":true") != nullptr) {
          result = STRATUM_SUCCESS;
        } else if (strstr(msg, "\"error\":[") != nullptr || strstr(msg, "\"error\": [") != nullptr) {
          result = STRATUM_PARSE_ERROR;
        } else {
          // Ambiguous, use full parser
          result = parse_mining_method(msg);
        }
      } else {
        // Unknown format, use full parser
        result = parse_mining_method(msg);
      }

      switch (result)
      {
          case MINING_NOTIFY:         if(parse_mining_notify(pending_buffer, mJob))
                                      {
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
                                            JobPush(new_sw_jobs, job_pool, nonce_pool, NONCE_PER_JOB_SW, currentPoolDifficulty, mMiner.bytearray_blockheader, diget_mid, bake);
                                            nonce_pool += NONCE_PER_JOB_SW;  // Sequential increment (wraparound automatic)
                                            #endif
                                            #ifdef HARDWARE_SHA256
                                              #if defined(CONFIG_IDF_TARGET_ESP32)
                                                JobPush(new_hw_jobs, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, sha_buffer_swap, hw_midstate, bake);
                                              #else
                                                JobPush(new_hw_jobs, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, mMiner.bytearray_blockheader, hw_midstate, bake);
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
                                      } else
                                      {
                                        DEBUG_SERIAL_PRINTF("Mining notify parse error (line: %.100s), restarting\n", pending_buffer);
                                        client.stop();
                                        cached_connected = false;  // Invalidate connection cache
                                        pending_len = 0;
                                        isMinerSuscribed.store(false, std::memory_order_release);
                                        MiningJobStop(job_pool, s_submission_map);
                                      }
                                      break;
          case MINING_SET_DIFFICULTY:{
                                      // Useless because i am working in a solo pool, i use this method only for keep alive the connection
                                      double set_difficulty = 0.0;
                                      parse_mining_set_difficulty(pending_buffer, set_difficulty);
                                      DEBUG_SERIAL_PRINTF("Mining set difficulty %f \n", set_difficulty);
                                      break;
                                      }
          case STRATUM_SUCCESS:       {
                                        unsigned long id = parse_extract_id(pending_buffer);
                                        auto itt = s_submission_map.find(id);
                                        if (itt != s_submission_map.end())
                                        {
                                          // Update best_diff if new difficulty is higher (single writer, safe without CAS)
                                          float current = best_diff.load(std::memory_order_relaxed);
                                          if (itt->second->diff > current) 
                                          {
                                            best_diff.store(itt->second->diff, std::memory_order_relaxed);
                                          }
                                          // Also update pool difficulty - pool accepting this share means it met the current target
                                          if (itt->second->diff > currentPoolDifficulty) 
                                          {
                                            currentPoolDifficulty = itt->second->diff;
                                          }
                                          if (itt->second->is32bit)
                                            shares.fetch_add(1, std::memory_order_relaxed);
                                          if (itt->second->isValid)
                                          {
                                            DEBUG_SERIAL_PRINTLN("CONGRATULATIONS! Valid block found");
                                            valids.fetch_add(1, std::memory_order_relaxed);
                                          }
                                          s_submission_map.erase(itt);
                                        }
                                      }
                                      break;
          case STRATUM_PARSE_ERROR:   {
                                        unsigned long id = parse_extract_id(pending_buffer);
                                        auto itt = s_submission_map.find(id);
                                        if (itt != s_submission_map.end())
                                        {
                                          DEBUG_SERIAL_PRINTF("[ERROR] Pool refused submission %d (line: %.100s)\n", id, pending_buffer);
                                          s_submission_map.erase(itt);
                                        }
                                      }
                                      break;
          default:                    DEBUG_SERIAL_PRINTLN("Parsed JSON: unknown\n"); break;

      }

      // Refresh available bytes for next iteration
      available = client.available();
    }

    std::list<std::shared_ptr<JobResult>> job_result_list;

    // Adaptive delay: use shorter delays when work is available for better responsiveness
    // TELEMETRY: Monitor delay_ms distribution to tune responsiveness vs CPU usage
    // Expected: ACTIVE_MS when queue < 50%, NORMAL_MS when queue 75%+, IDLE_MS otherwise
    uint32_t delay_ms;
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      bool has_pending_results = !s_job_result_list.empty();
      bool sw_queue_low = s_job_request_list_sw.size() < JOB_QUEUE_SIZE / 2;
      #ifdef HARDWARE_SHA256
      bool hw_queue_low = s_job_request_list_hw.size() < JOB_QUEUE_SIZE / 2;
      #else
      bool hw_queue_low = false;
      #endif

      // TELEMETRY: Track queue depths: s_job_request_list_sw.size(), s_job_request_list_hw.size()
      // Healthy range: 4-8 jobs queued (50-100% of JOB_QUEUE_SIZE)
      if (has_pending_results || sw_queue_low || hw_queue_low) {
        delay_ms = STRATUM_DELAY_ACTIVE_MS;  // Work available - high priority
      } else if (s_job_request_list_sw.size() >= JOB_QUEUE_SIZE * 3 / 4) {
        delay_ms = STRATUM_DELAY_NORMAL_MS;  // Queues healthy - normal operation
      } else {
        delay_ms = STRATUM_DELAY_IDLE_MS;    // Idle - conserve CPU
      }
    }
    vTaskDelay(delay_ms / portTICK_PERIOD_MS);


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
          JobPush(new_jobs_sw, job_pool, nonce_pool, NONCE_PER_JOB_SW, currentPoolDifficulty, mMiner.bytearray_blockheader, diget_mid, bake);
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
            JobPush(new_jobs_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, sha_buffer_swap, hw_midstate, bake);
          #else
            JobPush(new_jobs_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, mMiner.bytearray_blockheader, hw_midstate, bake);
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

    while (!job_result_list.empty())
    {
      std::shared_ptr<JobResult> res = job_result_list.front();
      job_result_list.pop_front();

      // Only count actually processed nonces in hashrate (skipped nonces weren't computed)
      if (res->nonce_count > res->nonces_skipped) 
      {
        total_hashes_processed += (res->nonce_count - res->nonces_skipped);
      }
      if (res->difficulty > currentPoolDifficulty && job_pool == res->id && res->nonce != 0xFFFFFFFF)
      {
        if (!client.connected())
          break;
        unsigned long sumbit_id = 0;
        tx_mining_submit(client, mWorker, mJob, res->nonce, sumbit_id);
        DEBUG_SERIAL_PRINT("   - Current diff share: "); DEBUG_SERIAL_PRINTLN(res->difficulty,12);
        DEBUG_SERIAL_PRINT("   - Current pool diff : "); DEBUG_SERIAL_PRINTLN(currentPoolDifficulty,12);
        DEBUG_SERIAL_PRINT("   - TX SHARE: ");
        for (size_t i = 0; i < 32; i++)
            DEBUG_SERIAL_PRINTF("%02x", res->hash[i]);
        DEBUG_SERIAL_PRINTLN("");
        mLastTXtoPool = millis();

        std::shared_ptr<Submission> submission = std::make_shared<Submission>();
        submission->diff = res->difficulty;
        submission->is32bit = (res->hash[29] == 0 && res->hash[28] == 0);
        submission->timestamp_ms = millis();  // Record submission time
        if (submission->is32bit)
        {
          submission->isValid = checkValid(res->hash, mMiner.bytearray_target);
        } else
          submission->isValid = false;

        {

          // Check for ID collision (rare but possible after wraparound)
          auto existing = s_submission_map.find(sumbit_id);
          if (existing != s_submission_map.end()) {
            DEBUG_SERIAL_PRINTF("[WARN] Submission ID collision: %lu already pending\n", sumbit_id);
            DEBUG_SERIAL_PRINTF("[WARN] Overwriting old submission (ID wraparound detected)\n");
            s_submission_map.erase(existing);
          }

          s_submission_map.insert(std::make_pair(sumbit_id, submission));

          // Cleanup strategy: First evict old entries (60s timeout), then FIFO if still oversized
          // Only scan for timeouts if map is getting full (avoid hot path overhead)
          // Use 1/3 threshold instead of 1/2 to trigger cleanup earlier and prevent OOM
          // Use lazy evaluation: only run cleanup every Nth insertion to reduce overhead
          // TELEMETRY: Monitor cleanup frequency and removed_count
          // Expected: Cleanup runs rarely (<1% of submissions), removes 0-2 entries typically
          // Alert if: cleanup_counter hits CLEANUP_BATCH_INTERVAL frequently (map pressure)
          static uint32_t cleanup_counter = 0;
          constexpr size_t CLEANUP_THRESHOLD = SUBMISSION_MAP_MAX / 3;
          constexpr size_t TARGET_SIZE = CLEANUP_THRESHOLD / 2;  // Clean down to this size

          if (s_submission_map.size() >= CLEANUP_THRESHOLD &&
              ++cleanup_counter >= CLEANUP_BATCH_INTERVAL) {
            cleanup_counter = 0;  // Reset counter
            uint32_t now = millis();
            size_t removed_count = 0;

            // First pass: Remove timed-out submissions
            // Early exit once we've cleaned enough to get back below target
            for (auto it = s_submission_map.begin();
                 it != s_submission_map.end() && s_submission_map.size() > TARGET_SIZE; ) {
              uint32_t age = time_elapsed(now, it->second->timestamp_ms);

              if (age > SUBMISSION_TIMEOUT_MS) {
                DEBUG_SERIAL_PRINTF("[WARN] Evicting timed-out submission ID=%lu (age=%lums)\n",
                                    it->first, age);
                it = s_submission_map.erase(it);
                removed_count++;
              } else {
                ++it;
              }
            }

            if (removed_count > 0) {
              DEBUG_SERIAL_PRINTF("[INFO] Cleaned %zu timed-out submissions, size now %zu\n",
                                  removed_count, s_submission_map.size());
            }
          }

          // Second pass: If still oversized, evict oldest entries (shouldn't happen often)
          while (s_submission_map.size() > SUBMISSION_MAP_MAX) {
            DEBUG_SERIAL_PRINTF("[WARN] Evicting oldest submission ID=%lu (map full)\n",
                                s_submission_map.begin()->first);
            s_submission_map.erase(s_submission_map.begin());
          }
        }
      }
    }

    // Apply batched hash update (single atomic operation instead of per-result)
    if (total_hashes_processed > 0) {
      hashes.fetch_add(total_hashes_processed, std::memory_order_relaxed);
    }
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
      result->difficulty = job->difficulty;
      result->nonce = 0xFFFFFFFF;
      result->id = job->id;
      result->nonce_count = job->nonce_count;
      result->nonces_skipped = 0;  // SW workers don't skip nonces (no SHA hardware)
      uint8_t job_in_work = job->id & 0xFF;
      for (uint32_t n = 0; n < job->nonce_count; ++n)
      {
        uint32_t nonce = job->nonce_start + n;
        if (nerd_sha256d_baked_nonce(job->midstate, job->bake, __builtin_bswap32(nonce), hash))
        {
          double diff_hash = diff_from_target(hash);
          // DEBUG_SERIAL_PRINTF("[DIFFICULTY] Diff: %.8f | Nonce: %u \n", diff_hash, nonce);
          if (diff_hash > result->difficulty)
          {
            result->difficulty = diff_hash;
            result->nonce = nonce;
            memcpy(result->hash, hash, 32);
          }
        }

        if ( (uint16_t)(n & 0xFF) == 0 && s_working_current_job_id.load(std::memory_order_acquire) != job_in_work)
        {
          result->nonce_count = n+1;
          break;
        }
      }
    } else
      vTaskDelay(10 / portTICK_PERIOD_MS);

    wdt_counter++;
    if (wdt_counter >= 8)
    {
      wdt_counter = 0;
      esp_task_wdt_reset();
    }
  }
}

#ifdef HARDWARE_SHA256

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)

static inline void nerd_sha_ll_fill_text_block_sha256(const void *input_text, uint32_t nonce)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    REG_WRITE(&reg_addr_buf[0], data_words[0]);
    REG_WRITE(&reg_addr_buf[1], data_words[1]);
    REG_WRITE(&reg_addr_buf[2], data_words[2]);
    REG_WRITE(&reg_addr_buf[3], nonce);
    REG_WRITE(&reg_addr_buf[4], 0x00000080);
    REG_WRITE(&reg_addr_buf[5], 0x00000000);
    REG_WRITE(&reg_addr_buf[6], 0x00000000);
    REG_WRITE(&reg_addr_buf[7], 0x00000000);
    REG_WRITE(&reg_addr_buf[8], 0x00000000);
    REG_WRITE(&reg_addr_buf[9], 0x00000000);
    REG_WRITE(&reg_addr_buf[10], 0x00000000);
    REG_WRITE(&reg_addr_buf[11], 0x00000000);
    REG_WRITE(&reg_addr_buf[12], 0x00000000);
    REG_WRITE(&reg_addr_buf[13], 0x00000000);
    REG_WRITE(&reg_addr_buf[14], 0x00000000);
    REG_WRITE(&reg_addr_buf[15], 0x80020000);
}

static inline void nerd_sha_ll_fill_text_block_sha256_inter()
{
  uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

  DPORT_INTERRUPT_DISABLE();
  REG_WRITE(&reg_addr_buf[0], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4));
  REG_WRITE(&reg_addr_buf[1], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4));
  REG_WRITE(&reg_addr_buf[2], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4));
  REG_WRITE(&reg_addr_buf[3], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4));
  REG_WRITE(&reg_addr_buf[4], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4));
  REG_WRITE(&reg_addr_buf[5], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4));
  REG_WRITE(&reg_addr_buf[6], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4));
  REG_WRITE(&reg_addr_buf[7], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4));
  DPORT_INTERRUPT_RESTORE();

  REG_WRITE(&reg_addr_buf[8], 0x00000080);
  REG_WRITE(&reg_addr_buf[9], 0x00000000);
  REG_WRITE(&reg_addr_buf[10], 0x00000000);
  REG_WRITE(&reg_addr_buf[11], 0x00000000);
  REG_WRITE(&reg_addr_buf[12], 0x00000000);
  REG_WRITE(&reg_addr_buf[13], 0x00000000);
  REG_WRITE(&reg_addr_buf[14], 0x00000000);
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


static inline bool nerd_sha_ll_read_digest_if(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  uint32_t last = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4);
  #if 1
  if ( (uint16_t)(last >> 16) != 0)
  {
    DPORT_INTERRUPT_RESTORE();
    return false;
  }
  #endif

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

static inline void nerd_sha_ll_write_digest(void *digest_state)
{
    uint32_t *digest_state_words = (uint32_t *)digest_state;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_H_BASE);

    REG_WRITE(&reg_addr_buf[0], digest_state_words[0]);
    REG_WRITE(&reg_addr_buf[1], digest_state_words[1]);
    REG_WRITE(&reg_addr_buf[2], digest_state_words[2]);
    REG_WRITE(&reg_addr_buf[3], digest_state_words[3]);
    REG_WRITE(&reg_addr_buf[4], digest_state_words[4]);
    REG_WRITE(&reg_addr_buf[5], digest_state_words[5]);
    REG_WRITE(&reg_addr_buf[6], digest_state_words[6]);
    REG_WRITE(&reg_addr_buf[7], digest_state_words[7]);
}

static inline bool nerd_sha_hal_wait_idle()
{
    uint32_t timeout = SHA_HARDWARE_TIMEOUT_CYCLES;
    while (REG_READ(SHA_BUSY_REG) && --timeout > 0)
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
    // Set SHA mode to reset the state machine
    REG_WRITE(SHA_MODE_REG, SHA2_256);
    // Give hardware time to reset
    asm volatile("nop");
    asm volatile("nop");
    asm volatile("nop");
    asm volatile("nop");
}

//#define VALIDATION
void minerWorkerHw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  DEBUG_SERIAL_PRINTF("[MINER] %d Started minerWorkerHw Task!\n", miner_id);

  std::shared_ptr<JobRequest> job;
  std::shared_ptr<JobResult> result;
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
      result->id = job->id;
      result->nonce = 0xFFFFFFFF;
      result->nonce_count = job->nonce_count;
      result->nonces_skipped = 0;  // Track SHA hardware timeouts
      result->difficulty = job->difficulty;
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
      for (uint32_t n = job->nonce_start; n < nend; ++n)
      {
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_write_digest(digest_mid);
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256(sha_buffer, n);
        //sha_ll_continue_block(SHA2_256);
        REG_WRITE(SHA_CONTINUE_REG, 1);

        sha_ll_load(SHA2_256);
        if (!nerd_sha_hal_wait_idle()) {
          // Reset SHA hardware to known state after timeout
          DEBUG_SERIAL_PRINTF("[SHA_HW] Timeout at nonce 0x%08X (job %u, round 1, worker %u)\n",
                              n, job->id, miner_id);
          nerd_sha_hw_reset();
          result->nonces_skipped++;
          continue;
        }
        nerd_sha_ll_fill_text_block_sha256_inter();
        //sha_ll_start_block(SHA2_256);
        REG_WRITE(SHA_START_REG, 1);
        sha_ll_load(SHA2_256);
        if (!nerd_sha_hal_wait_idle()) {
          // Reset SHA hardware to known state after timeout
          DEBUG_SERIAL_PRINTF("[SHA_HW] Timeout at nonce 0x%08X (job %u, round 2, worker %u)\n",
                              n, job->id, miner_id);
          nerd_sha_hw_reset();
          result->nonces_skipped++;
          continue;
        }
        if (nerd_sha_ll_read_digest_if(hash))
        {
          //DEBUG_SERIAL_PRINTF("Hw 16bit Share, nonce=0x%X\n", n);
#ifdef VALIDATION
          //Validation
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
          //~5 per second
          double diff_hash = diff_from_target(hash);
          if (diff_hash > result->difficulty)
          {
            if (isSha256Valid(hash))
            {
              result->difficulty = diff_hash;
              result->nonce = n;
              memcpy(result->hash, hash, sizeof(hash));
            }
          }
        }
        if (
             (uint8_t)(n & 0xFF) == 0 &&
             s_working_current_job_id.load(std::memory_order_acquire) != job_in_work)
        {
          result->nonce_count = n-job->nonce_start+1;
          break;
        }
      }
      esp_sha_release_hardware();
    } else
      vTaskDelay(10 / portTICK_PERIOD_MS);

    wdt_counter++;
    if (wdt_counter >= 8)
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
      result->id = job->id;
      result->nonce = 0xFFFFFFFF;
      result->nonce_count = job->nonce_count;
      result->nonces_skipped = 0;  // Track SHA hardware timeouts
      result->difficulty = job->difficulty;
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
          double diff_hash = diff_from_target(hash);
          if (diff_hash > result->difficulty)
          {
            if (isSha256Valid(hash))
            {
              result->difficulty = diff_hash;
              result->nonce = job->nonce_start+n;
              memcpy(result->hash, hash, sizeof(hash));
            }
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
    } else
      vTaskDelay(10 / portTICK_PERIOD_MS);

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
    elapsedKHs.store(0, std::memory_order_relaxed);
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

  totalKHashes.store((Mhashes.load(std::memory_order_relaxed) * 1000) + hashes.load(std::memory_order_relaxed) / 1000, std::memory_order_relaxed);
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
      unsigned long currentKHashes = (Mhashes.load(std::memory_order_relaxed) * 1000) + hashes.load(std::memory_order_relaxed) / 1000;
      uint32_t totalKH = totalKHashes.load(std::memory_order_relaxed);
      elapsedKHs.store(currentKHashes - totalKH, std::memory_order_relaxed);
      totalKHashes.store(currentKHashes, std::memory_order_relaxed);

      uptime_frac += mElapsed;
      while (uptime_frac >= 1000)
      {
        uptime_frac -= 1000;
        upTime.fetch_add(1, std::memory_order_relaxed);
      }

      // Serial.printf("[HASHRATE] %.2f KH/s\n", (float)elapsedKHs * 1000.0 / mElapsed);

      drawCurrentScreen(mElapsed);

      // Monitor state when hashrate is 0.0
      if (elapsedKHs.load(std::memory_order_relaxed) == 0)
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
