
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
#ifndef HARDWARE_SHA265
#define HARDWARE_SHA265
#endif
//#endif

#define TARGET_BUFFER_SIZE 64

// Nonce start values for different mining modes
#define NONCE_START_RANDOM     0xDA54E700  // Random start nonce (non-zero for compatibility)
#define NONCE_START_I2C_SLAVE  0x10000000  // Start nonce for I2C slave workers
#define NONCE_START_I2C_FEED   0x20000000  // Start nonce for feeding I2C slaves

void runMonitor(void *name);

void runStratumWorker(void *name);
void runMiner(void *name);

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
