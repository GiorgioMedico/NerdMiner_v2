# NerdMiner v2 - Mining Values Reference

This document provides a comprehensive reference of all important mining-related values, constants, and data structures in the NerdMiner v2 codebase.

---

## Table of Contents
1. [Mining Performance Constants](#mining-performance-constants)
2. [Difficulty & Target Configuration](#difficulty--target-configuration)
3. [Hash Endianness](#hash-endianness)
4. [Timing & Timeout Values](#timing--timeout-values)
5. [SHA256 Hardware Configuration](#sha256-hardware-configuration)
6. [Queue & Buffer Sizes](#queue--buffer-sizes)
7. [Nonce Start Values](#nonce-start-values)
8. [Global Mining Statistics](#global-mining-statistics)
9. [Critical Data Structures](#critical-data-structures)

---

## Mining Performance Constants

### `MAX_NONCE_STEP`
- **Value**: `5000000U` (5 million)
- **Location**: `src/mining.h:6`
- **Type**: Unsigned 32-bit integer
- **Description**: Maximum nonce range processed per mining iteration. This defines the upper bound for how many nonces can be tested in a single mining pass.
- **Impact**: Affects mining batch sizes and memory usage. Larger values mean fewer iterations but more memory required.

### `MAX_NONCE`
- **Value**: `25000000U` (25 million)
- **Location**: `src/mining.h:7`
- **Type**: Unsigned 32-bit integer
- **Description**: Total maximum nonce value for mining operations. Defines the overall search space boundary.
- **Impact**: Sets the upper limit for nonce exploration before wrapping or restarting.

### `NONCE_PER_JOB_SW`
- **Value**: `8192` (8K nonces)
- **Location**: `src/mining.cpp:28`
- **Type**: Integer constant
- **Description**: Number of nonces processed per software mining job. Software mining is slower than hardware acceleration, so smaller batches ensure responsiveness.
- **Impact**: Lower values = more frequent job checks and better cancellation responsiveness. Higher values = better efficiency but less responsive to new jobs.
- **Tuning**: Increase for better SW mining throughput, decrease for faster job switching.

### `NONCE_PER_JOB_HW`
- **Value**: `32768` (32K nonces, 32 × 1024)
- **Location**: `src/mining.cpp:29`
- **Type**: Integer constant
- **Description**: Number of nonces processed per hardware-accelerated mining job. Hardware SHA256 acceleration is ~4x faster than software, allowing larger batches.
- **Impact**: Higher values maximize hardware utilization. Must balance with job cancellation latency.
- **Tuning**: ESP32 hardware can handle larger batches efficiently. Don't exceed memory constraints.

---

## Difficulty & Target Configuration

### `DEFAULT_DIFFICULTY`
- **Value**: `0.00015`
- **Location**: `src/mining.h:9`
- **Type**: Double-precision floating point
- **Description**: Default pool difficulty suggested to the mining pool during connection. Very low difficulty suitable for low-hashrate devices like ESP32.
- **Impact**: Lower difficulty = more frequent shares submitted to pool. Higher difficulty = fewer but more valuable shares.
- **Context**: NerdMiner targets solo pools that accept low-difficulty shares (e.g., public-pool.io). Standard pools may ignore this suggestion.

### `TARGET_NONCE`
- **Value**: `471136297U` (0x1C1A1249)
- **Location**: `src/mining.h:8`
- **Type**: Unsigned 32-bit integer
- **Description**: Reference target nonce value, possibly used for testing or difficulty calculations.
- **Impact**: Primarily used for validation and testing purposes.

---

## Hash Endianness

### Little-Endian Hash Representation

**Overview**: The NerdMiner v2 codebase consistently treats the 32-byte double-SHA256 hash result as little-endian byte order throughout all mining operations. This ensures compatibility with Bitcoin's internal hash representation while maintaining efficient hardware acceleration.

**Key Components**:

#### `checkValid` Function (`src/utils.cpp:171-201`)
- **Input**: 32-byte SHA256 hash (little-endian), 32-byte difficulty target (big-endian from stratum)
- **Process**: Reverses the big-endian stratum target to little-endian for comparison
- **Comparison**: Compares hashes from most-significant byte (index 31) to least-significant byte (index 0)
- **Purpose**: Validates if hash meets difficulty target in little-endian byte order

#### `le256todouble` Function (`src/utils.cpp:115-135`)
- **Input**: 256-bit hash buffer stored LSB-first (little-endian)
- **Process**: Reads most-significant limb from offset 24, most-significant from offset 0
- **Output**: Converts to double-precision floating point for difficulty calculations
- **Usage**: Used in `diff_from_target()` for both software and hardware mining loops

#### Runtime Share Validation (`src/mining.cpp:682-686`)
- **Check**: Flags shares as 32-bit only if bytes 28-29 are zero
- **Rationale**: In little-endian layout, bytes 28-29 represent the highest-order bits
- **Purpose**: Identifies exceptionally rare shares with 32 bits of leading zeros

#### Hardware/Software Digest Agreement (`src/mining.cpp:839-874, 1012-1025`)
- **Hardware**: ESP32 SHA registers output directly in little-endian byte order
- **Software**: `nerd_sha256d_baked_nonce` produces little-endian results
- **Validation**: Optional byte-for-byte comparison ensures consistency
- **Purpose**: Guarantees hardware acceleration matches software fallback

**Network Serialization**: When transmitting hashes over the network (stratum protocol), reverse byte order to big-endian format as required by Bitcoin network standards.

**Consistency**: All internal hash operations, difficulty calculations, and validations use little-endian representation for optimal performance with ESP32 hardware.

---

## Timing & Timeout Values

### `KEEPALIVE_TIME_ms`
- **Value**: `30000` (30 seconds)
- **Location**: `src/mining.h:10`
- **Type**: Integer (milliseconds)
- **Description**: Pool connection keepalive interval. If no shares are submitted within this time, a keepalive message (suggest_difficulty) is sent to prevent socket timeout.
- **Impact**: Too short = unnecessary network traffic. Too long = risk of silent disconnection.
- **Implementation**: See `checkPoolInactivity()` in `src/mining.cpp:120`

### `POOLINACTIVITY_TIME_ms`
- **Value**: `60000` (60 seconds)
- **Location**: `src/mining.h:11`
- **Type**: Integer (milliseconds)
- **Description**: Maximum time to wait without receiving data from the pool before reconnecting. If hashrate is zero for this duration, the connection is reset.
- **Impact**: Prevents stuck connections. Balance between patience and quick recovery.
- **Implementation**: See `checkPoolInactivity()` in `src/mining.cpp:120`

### Monitor Update Delay (`DELAY`)
- **Value**: `1000` (1 second)
- **Location**: `src/mining.cpp:1332`
- **Type**: Integer (milliseconds)
- **Description**: Display/monitor update interval. Reduced from 500ms to 1000ms to save CPU cycles.
- **Impact**: Affects screen refresh rate and hashrate calculation frequency.

---

## SHA256 Hardware Configuration

### `SHA_HARDWARE_TIMEOUT_CYCLES`
- **Value**: `100000` (100K cycles)
- **Location**: `src/mining.h:17`
- **Type**: Integer
- **Description**: Maximum number of CPU cycles to wait for SHA hardware to complete before timing out. Prevents infinite hangs if SHA hardware enters invalid state.
- **Impact**:
  - Too low = false timeouts, wasted nonces
  - Too high = longer hangs if hardware fails
- **Tracking**: Timeout events are counted in `sha_timeout_count` atomic variable
- **Recovery**: After timeout, hardware is reset via `nerd_sha_hw_reset()` and nonce is skipped

### `HARDWARE_SHA256`
- **Value**: Defined (enabled by default)
- **Location**: `src/mining.h:25-27`
- **Type**: Preprocessor macro
- **Description**: Enables hardware SHA256 acceleration. Currently enabled for all ESP32 variants (classic, S2, S3, C3).
- **Impact**: 4-10x speedup compared to software SHA256. Critical for competitive hashrates.

---

## Queue & Buffer Sizes

### `JOB_QUEUE_SIZE`
- **Value**: `4`
- **Location**: `src/mining.h:20`
- **Type**: Integer constant
- **Description**: Maximum number of mining jobs queued for workers. Separate queues for software (`s_job_request_list_sw`) and hardware (`s_job_request_list_hw`) workers.
- **Impact**: Larger queue = better worker utilization but more memory. Smaller queue = risk of worker starvation.
- **Memory**: Each job ~300 bytes, so 4 jobs ≈ 1.2KB per queue.

### `RESULT_LIST_SIZE`
- **Value**: `16`
- **Location**: `src/mining.h:21`
- **Type**: Integer constant
- **Description**: Maximum number of mining results (potential shares) queued for submission to pool.
- **Impact**: Prevents memory overflow if pool response is slow. Should be larger than typical submission rate.

### `SUBMISSION_MAP_MAX`
- **Value**: `16`
- **Location**: `src/mining.h:22`
- **Type**: Integer constant
- **Description**: Maximum number of pending share submissions tracked while waiting for pool confirmation.
- **Impact**: Limits memory usage for tracking submitted shares. Oldest/timed-out submissions are evicted (60s timeout).
- **Implementation**: See cleanup logic in `src/mining.cpp:714-743`

### `MAX_POOL_LINE_SIZE`
- **Value**: `4096` (4KB)
- **Location**: `src/mining.h:14`
- **Type**: Integer constant
- **Description**: Maximum size of a single stratum protocol message from the pool. Messages exceeding this are rejected and connection is reset.
- **Impact**: Prevents buffer overflow attacks or malformed pool responses from crashing the miner.

### `TARGET_BUFFER_SIZE`
- **Value**: `64`
- **Location**: `src/mining.h:30`
- **Type**: Integer constant
- **Description**: Size of target value buffers.

---

## Nonce Start Values

### `NONCE_START_RANDOM`
- **Value**: `0xDA54E700` (3,663,153,920)
- **Location**: `src/mining.h:33`
- **Type**: Unsigned 32-bit integer (hex)
- **Description**: Starting nonce for random nonce mode. Non-zero for compatibility with certain pools.
- **Usage**: Used when `RANDOM_NONCE` is not defined (sequential mining mode).

### `RANDOM_NONCE_MASK`
- **Value**: `0xFFFFC000`
- **Location**: `src/mining.cpp:37`
- **Type**: Unsigned 32-bit integer (hex)
- **Description**: Clears lower 14 bits (0x3FFF) to ensure 16KB alignment when generating random nonces. Provides better distribution across nonce space.
- **Effect**: Random nonces will be multiples of 16,384 (2^14)

---

## Global Mining Statistics

These are global variables tracked throughout the mining session and persisted to NVS (Non-Volatile Storage).

### Hash Counters

#### `hashes`
- **Type**: `uint32_t`
- **Location**: `src/mining.cpp:54`
- **Description**: Current hash count. When it exceeds 1 million, it rolls over to `Mhashes` and resets.
- **Thread Safety**: Modified by worker threads, read by monitor thread.

#### `Mhashes`
- **Type**: `uint32_t`
- **Location**: `src/mining.cpp:55`
- **Description**: Million-hash counter. Incremented each time `hashes` reaches 1,000,000.
- **Persistence**: Saved to NVS for tracking across reboots.

#### `totalKHashes`
- **Type**: `uint32_t`
- **Location**: `src/mining.cpp:56`
- **Description**: Total kilohashes computed, calculated as `(Mhashes × 1000) + (hashes / 1000)`.
- **Usage**: Used for hashrate calculations and pool inactivity detection.

#### `elapsedKHs`
- **Type**: `uint32_t`
- **Location**: `src/mining.cpp:57`
- **Description**: Kilohashes computed in the last measurement period (typically 1 second).
- **Usage**: Primary hashrate display value: `elapsedKHs` KH/s.

### Success Metrics

#### `shares`
- **Type**: `std::atomic<uint32_t>`
- **Location**: `src/mining.cpp:60`
- **Description**: Number of shares found with 32 bits of leading zeros (extremely rare, ~1 in 4 billion).
- **Thread Safety**: Atomic for thread-safe increment from worker threads.
- **Significance**: These are "32-bit shares" that indicate exceptional luck.

#### `valids`
- **Type**: `std::atomic<uint32_t>`
- **Location**: `src/mining.cpp:61`
- **Description**: Number of valid blocks found (hash below actual block target, not just pool target).
- **Thread Safety**: Atomic for thread-safe increment.
- **Significance**: Finding a valid block means successfully mining a Bitcoin block (extremely rare on ESP32).

#### `best_diff`
- **Type**: `std::atomic<float>`
- **Location**: `src/mining.cpp:66`
- **Description**: Best (highest) difficulty share ever found by this miner.
- **Thread Safety**: Lock-free atomic variable for thread-safe updates using compare-exchange.
- **Persistence**: Saved to NVS and restored on boot.

#### `templates`
- **Type**: `uint32_t`
- **Location**: `src/mining.cpp:53`
- **Description**: Number of mining job templates received from the pool.
- **Usage**: Indicates pool connectivity and job update frequency.

### Diagnostic Metrics

#### `sha_timeout_count`
- **Type**: `std::atomic<uint32_t>`
- **Location**: `src/mining.cpp:68`
- **Description**: Tracks SHA hardware timeout events for diagnostics. Incremented when SHA hardware doesn't respond within `SHA_HARDWARE_TIMEOUT_CYCLES`.
- **Debug**: Logged every 1000 timeouts to serial console.

#### `upTime`
- **Type**: `uint64_t`
- **Location**: `src/mining.cpp:58`
- **Description**: Total uptime in seconds. Updated every second by monitor task.
- **Persistence**: Saved to NVS for tracking total mining time across reboots.

---

## Critical Data Structures

### `miner_data` Structure

**Location**: `src/mining.h:50-63`

```cpp
struct miner_data {
  uint8_t bytearray_target[32];
  uint8_t bytearray_pooltarget[32];
  uint8_t merkle_result[32];
  uint8_t bytearray_blockheader[128];
};
```

**Fields**:
- `bytearray_target[32]`: The difficulty target for the current block. A valid hash must be less than or equal to this value.
- `bytearray_pooltarget[32]`: Pool-specific target based on pool difficulty. Usually higher (easier) than block target.
- `merkle_result[32]`: Result of merkle tree calculation combining coinbase transaction with merkle branches.
- `bytearray_blockheader[128]`: The 80-byte Bitcoin block header being mined, padded to 128 bytes for SHA256 processing.

**Purpose**: Contains all data needed for a miner worker to compute hashes.

**Memory**: 224 bytes total

---

### `JobRequest` Structure

**Location**: `src/mining.cpp:159-168`

```cpp
struct JobRequest {
  uint32_t id;
  uint32_t nonce_start;
  uint32_t nonce_count;
  double difficulty;
  uint8_t sha_buffer[128];
  uint32_t midstate[8];
  uint32_t bake[16];
};
```

**Fields**:
- `id`: Unique job identifier (incrementing counter). Used to cancel obsolete jobs when new work arrives.
- `nonce_start`: Starting nonce value for this job batch.
- `nonce_count`: Number of nonces to process (typically `NONCE_PER_JOB_SW` or `NONCE_PER_JOB_HW`).
- `difficulty`: Current pool difficulty for this job.
- `sha_buffer[128]`: Block header data prepared for SHA256 hashing.
- `midstate[8]`: Pre-computed SHA256 midstate (first 64 bytes of header). Optimization to avoid re-hashing constant data.
- `bake[16]`: Pre-computed values for faster second SHA256 round.

**Purpose**: Packages all data needed for a mining worker to process a batch of nonces.

**Memory**: ~300 bytes per job

**Lifecycle**: Created by stratum task, consumed by worker threads, results returned via `JobResult`.

---

### `JobResult` Structure

**Location**: `src/mining.cpp:170-178`

```cpp
struct JobResult {
  uint32_t id;
  uint32_t nonce;
  uint32_t nonce_count;
  uint32_t nonces_skipped;
  double difficulty;
  uint8_t hash[32];
};
```

**Fields**:
- `id`: Job ID this result belongs to (matches `JobRequest.id`).
- `nonce`: Best nonce found (0xFFFFFFFF if no share found above pool difficulty).
- `nonce_count`: Number of nonces actually processed (may be less than requested if job was cancelled).
- `nonces_skipped`: Number of nonces skipped due to SHA hardware timeouts.
- `difficulty`: Difficulty of the best share found.
- `hash[32]`: The actual hash result for the best nonce.

**Purpose**: Returns mining results from worker threads back to stratum task for pool submission.

**Memory**: ~80 bytes per result

---

### `mining_subscribe` Structure

**Location**: `src/stratum.h:26-33`

```cpp
typedef struct {
    String sub_details;
    String extranonce1;
    String extranonce2;
    int extranonce2_size;
    char wName[80];
    char wPass[20];
} mining_subscribe;
```

**Fields**:
- `sub_details`: Subscription details string from pool.
- `extranonce1`: First part of extranonce from pool (unique per connection).
- `extranonce2`: Second part of extranonce (miner-controlled).
- `extranonce2_size`: Size in bytes of extranonce2 field.
- `wName[80]`: Worker name (typically Bitcoin wallet address).
- `wPass[20]`: Worker password (usually "x" or empty for solo pools).

**Purpose**: Stores stratum protocol subscription state and worker credentials.

**Protocol**: Used in mining.subscribe and mining.authorize handshake with pool.

---

### `mining_job` Structure

**Location**: `src/stratum.h:35-47`

```cpp
typedef struct {
    String job_id;
    String prev_block_hash;
    String coinb1;
    String coinb2;
    String nbits;
    size_t merkle_branch_len = 0;
    String merkle_branch[MAX_MERKLE_BRANCHES];
    String version;
    uint32_t target;
    String ntime;
    bool clean_jobs;
} mining_job;
```

**Fields**:
- `job_id`: Unique job identifier from pool (changes with each new job).
- `prev_block_hash`: Previous block hash (32 bytes hex). This is what we're building upon.
- `coinb1`: First part of coinbase transaction (before extranonce).
- `coinb2`: Second part of coinbase transaction (after extranonce).
- `nbits`: Compact difficulty target representation (4 bytes hex).
- `merkle_branch_len`: Number of merkle branches (typically 10-20 for Bitcoin).
- `merkle_branch[32]`: Array of merkle tree hashes to combine with coinbase transaction.
- `version`: Block version (4 bytes hex).
- `target`: Numeric difficulty target (derived from nbits).
- `ntime`: Network time timestamp for block (4 bytes hex).
- `clean_jobs`: If true, discard all previous jobs (new block found on network).

**Purpose**: Contains all data from pool's mining.notify message needed to construct a valid block header.

**Protocol**: Received via stratum mining.notify RPC calls.

**Memory**: ~1-2KB depending on merkle branch count and string lengths.

---

### `Submission` Structure

**Location**: `src/mining.cpp:205-211`

```cpp
struct Submission {
  double diff;
  bool is32bit;
  bool isValid;
  uint32_t timestamp_ms;
};
```

**Fields**:
- `diff`: Difficulty of the submitted share.
- `is32bit`: True if hash has 32 bits of leading zeros (extremely rare).
- `isValid`: True if hash is below actual block target (valid block found).
- `timestamp_ms`: Timestamp when submission was created (for timeout cleanup).

**Purpose**: Tracks pending share submissions waiting for pool confirmation.

**Lifecycle**: Created when share submitted, held in `s_submission_map`, removed when pool confirms or after 60s timeout.

**Memory**: ~16 bytes per submission

---

## Stratum Protocol Constants

### String Length Limits

**Location**: `src/stratum.h:18-24`

These limits prevent buffer overflows and validate incoming pool data:

- `MAX_JOB_ID_LEN`: 256 - Maximum job ID string length
- `MAX_HASH_LEN`: 256 - Maximum hash string length (hex encoded)
- `MAX_COINBASE_LEN`: 512 - Maximum coinbase transaction length
- `MAX_VERSION_LEN`: 64 - Maximum version string length
- `MAX_NBITS_LEN`: 64 - Maximum nbits string length
- `MAX_NTIME_LEN`: 64 - Maximum ntime string length

### Buffer Sizes

- `BUFFER_JSON_DOC`: 4096 - JSON document buffer size for ArduinoJson
- `BUFFER`: 1024 - General purpose buffer size
- `MAX_MERKLE_BRANCHES`: 32 - Maximum number of merkle branches (Bitcoin typically uses 10-20)
- `HASH_SIZE`: 32 - SHA256 hash size in bytes
- `COINBASE_SIZE`: 100 - Coinbase buffer size
- `COINBASE2_SIZE`: 128 - Secondary coinbase buffer size

---

## Task Configuration

### Task Stack Sizes

Defined in platform-specific configurations (see `platformio.ini`):

- **ESP32 Classic**: 5000 bytes (reduced for memory constraints)
- **ESP32-S2/S3/C3**: 6000 bytes (more memory available)

### Task Priorities

**Location**: `src/NerdMinerV2.ino`

- Monitor task: Priority 5 (highest - display updates critical)
- Stratum task: Priority 4 (network I/O important)
- Miner tasks: Priority 1-3 (CPU-intensive but can be interrupted)

---

## Save Intervals

**Location**: `src/mining.cpp:85`

```cpp
int saveIntervals[7] = {5 * 60, 15 * 60, 30 * 60, 1 * 3600, 3 * 3600, 6 * 3600, 12 * 3600};
```

NVS statistics are saved at increasing intervals to reduce flash wear:
- First save: 5 minutes
- Second save: 15 minutes
- Third save: 30 minutes
- Fourth save: 1 hour
- Fifth save: 3 hours
- Sixth save: 6 hours
- Subsequent saves: 12 hours

**Purpose**: Balance between preserving statistics across reboots and minimizing NVS flash wear.

---

## Random Nonce Configuration

### `RANDOM_NONCE` Mode

**Location**: `src/mining.cpp:34` (commented out by default)

When enabled:
- Uses hardware RNG for nonce selection instead of sequential scanning
- Initial seed: `((uint64_t)esp_random() << 32) | esp_random()`
- Random nonces are masked with `RANDOM_NONCE_MASK` (0xFFFFC000) for 16KB alignment

**Advantages**:
- Better nonce space distribution for multiple miners
- Reduces chance of nonce collision with other miners on same pool

**Disadvantages**:
- Slightly more CPU overhead for RNG
- Less predictable testing/debugging

---

## Memory Management Notes

### PSRAM vs. Non-PSRAM Boards

- **PSRAM boards**: Can use larger buffers and queue sizes
- **Non-PSRAM boards** (ESP32 Classic): Reduced stack sizes (5000 vs 6000), smaller allocations

### Watchdog Timer (WDT)

- **Timeout**: 15 minutes for miner tasks
- **Reset calls**: Every 8 job iterations in worker threads
- **Purpose**: Prevent system hang if mining task freezes

---

## Performance Tuning Recommendations

### For Higher Hashrate
1. Increase `NONCE_PER_JOB_HW` to 65536 (if memory allows)
2. Increase `JOB_QUEUE_SIZE` to 8 (reduces worker idle time)
3. Enable `RANDOM_NONCE` for multi-miner setups

### For Better Responsiveness
1. Decrease `NONCE_PER_JOB_HW` to 16384
2. Decrease `DELAY` for faster screen updates (costs CPU)

### For Lower Memory Usage
1. Decrease `JOB_QUEUE_SIZE` to 2
2. Decrease `RESULT_LIST_SIZE` to 8
3. Decrease `SUBMISSION_MAP_MAX` to 8

### For Stability
1. Keep default `SHA_HARDWARE_TIMEOUT_CYCLES` (100K)
2. Maintain `KEEPALIVE_TIME_ms` at 30s
3. Don't exceed `MAX_POOL_LINE_SIZE` (4KB is safe for all pools)

---

## Debugging Values

Enable these in your build for additional diagnostics:

- `DEBUG_MEMORY`: Shows heap usage and stack high-water marks
- `VALIDATION`: Validates hardware SHA256 results against software implementation
- `SHA256_VALIDATE`: Additional SHA256 validation

---

## References

- Bitcoin Stratum Protocol: https://braiins.com/stratum-v1/docs
- ESP32 SHA Hardware: ESP-IDF documentation
- Bitcoin Mining Difficulty: https://en.bitcoin.it/wiki/Difficulty

---

**Document Version**: 1.1
**Last Updated**: 2025-10-08
**Codebase Version**: Based on commit 8adfe5b
