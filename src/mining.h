
#ifndef MINING_H
#define MINING_H

// Mining
#define MAX_NONCE_STEP  5000000U
#define MAX_NONCE       25000000U
#define TARGET_NONCE    471136297U
#define DEFAULT_DIFFICULTY  0.00015
#define KEEPALIVE_TIME_ms       30000
#define POOLINACTIVITY_TIME_ms  60000

// Network safety
#define MAX_POOL_LINE_SIZE      4096

// Hardware SHA timeout (reduced for faster recovery from stuck hardware)
#define SHA_HARDWARE_TIMEOUT_CYCLES 50000

// Job queue sizes (increased for better miner throughput)
#define JOB_QUEUE_SIZE          8
#define RESULT_LIST_SIZE        16
#define SUBMISSION_MAP_MAX       16

//#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
#ifndef HARDWARE_SHA256
#define HARDWARE_SHA256
#endif
//#endif

#define TARGET_BUFFER_SIZE 64

// Nonce start values for different mining modes
#define NONCE_START_RANDOM     0xDA54E700  // Random start nonce (non-zero for compatibility)

// === Stratum Worker Performance Optimizations ===
// These constants tune the stratum task's responsiveness vs CPU usage tradeoff

// Adaptive task delay based on work queue state (reduces CPU spinning)
#define STRATUM_DELAY_ACTIVE_MS    1   // High priority: Work pending or queues low
#define STRATUM_DELAY_NORMAL_MS    10  // Normal operation: Queues healthy
#define STRATUM_DELAY_IDLE_MS      20  // Low priority: Queues full, conserve CPU

// Connection state caching (reduces expensive WiFiClient::connected() syscalls)
#define CONN_CHECK_CACHE_MS        100 // Cache pool connection status for 100ms

// Memory management tuning
#define CLEANUP_BATCH_INTERVAL     10  // Lazy cleanup: only run every Nth insertion
#define JOB_REFILL_BATCH           4   // Incremental refill: jobs created per batch

// Network I/O configuration
#define CHUNK_READ_SIZE            256 // Bytes per network read (aligns with typical stratum messages)
#define DNS_CACHE_DURATION_MS      (60*60*1000)  // 1 hour DNS cache (pools rarely change IPs)
#define JOB_TIMEOUT_MS             (10*60*1000)  // 10 minutes without new job triggers reconnect
#define SUBMISSION_TIMEOUT_MS      60000  // 60 second pool response timeout for share submissions

void runMonitor(void *name);

void runStratumWorker(void *name);

void minerWorkerSw(void * task_id);
void minerWorkerHw(void * task_id);

String printLocalTime(void);

void resetStat();
void closeStat();

struct miner_data {
  uint8_t bytearray_target[32];
  uint8_t bytearray_pooltarget[32];
  uint8_t merkle_result[32];
  uint8_t bytearray_blockheader[128];

  // Constructor to initialize all members to zero
  miner_data()
    : bytearray_target{0}
    , bytearray_pooltarget{0}
    , merkle_result{0}
    , bytearray_blockheader{0}
  {}
};


#endif // MINING_H
