
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

#define SHA_HARDWARE_TIMEOUT_CYCLES 200000

// Hardware SHA batch size - process this many nonces before checking job cancellation
// Should be a power of 2 for optimal performance. Larger batch = less overhead, slower job switch
// Typical values: 256 (responsive), 512 (balanced), 1024 (maximum throughput)
#define BATCH_HW_SIZE 1024

// Software SHA batch size - smaller than HW since SW mining is slower
// Matches original check frequency (every 256 nonces) for responsive job switching
#define BATCH_SW_SIZE 256

// Job queue sizes (increased for better miner throughput)
#define JOB_QUEUE_SIZE          14
#define RESULT_LIST_SIZE        20
#define SUBMISSION_MAP_MAX       40

#define WDT_COUNTER 8

//#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
#ifndef HARDWARE_SHA256
#define HARDWARE_SHA256
#endif
//#endif

// Nonce start values for different mining modes
#define NONCE_START_RANDOM     0xDA54E700  // Random start nonce (non-zero for compatibility)

#define JOB_REFILL_BATCH           8   // Incremental refill: jobs created per batch
#define JOB_TIMEOUT_MS             (15*60*1000)  // 15 minutes without new job triggers reconnect



void runMonitor(void *name);

void runStratumWorker(void *name);

void minerWorkerSw(void * task_id);
void minerWorkerHw(void * task_id);

String printLocalTime(void);

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
