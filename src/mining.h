
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

#define SHA_HARDWARE_TIMEOUT_CYCLES 100000

// Job cancellation check frequency (power of 2 for efficient masking)
// Check every N nonces for new job - higher = less overhead, slower response
// 0x3FF = 1024 nonces (optimized), 0xFF = 256 nonces (original)
#define JOB_CANCELLATION_CHECK_MASK 0x3FF

// Job queue sizes (increased for better miner throughput)
#define JOB_QUEUE_SIZE          10
#define RESULT_LIST_SIZE        16
#define SUBMISSION_MAP_MAX       32

#define WDT_COUNTER 10 //8

//#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
#ifndef HARDWARE_SHA256
#define HARDWARE_SHA256
#endif
//#endif

#define TARGET_BUFFER_SIZE 64

// Nonce start values for different mining modes
#define NONCE_START_RANDOM     0xDA54E700  // Random start nonce (non-zero for compatibility)

#define JOB_REFILL_BATCH           6   // Incremental refill: jobs created per batch
#define JOB_TIMEOUT_MS             (10*60*1000)  // 10 minutes without new job triggers reconnect



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
