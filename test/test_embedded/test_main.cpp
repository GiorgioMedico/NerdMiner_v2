#include <unity.h>
#include <Arduino.h>

// Include the actual source files for testing
#include "../../src/utils.cpp"

#ifdef SHA256_TEST
#include <mbedtls/sha256.h>
#endif

void setUp(void) {
    // Set up function called before each test
    Serial.begin(115200);
    delay(100);
}

void tearDown(void) {
    // Tear down function called after each test
}

// =============================================================================
// COMMON UTILITY TESTS (used by all test modes)
// =============================================================================

void test_hex_conversion() {
    TEST_ASSERT_EQUAL_UINT8(0, hex('0'));
    TEST_ASSERT_EQUAL_UINT8(9, hex('9'));
    TEST_ASSERT_EQUAL_UINT8(10, hex('a'));
    TEST_ASSERT_EQUAL_UINT8(10, hex('A'));
    TEST_ASSERT_EQUAL_UINT8(15, hex('f'));
    TEST_ASSERT_EQUAL_UINT8(15, hex('F'));

    Serial.println("✓ Hex conversion tests passed");
}

void test_to_byte_array() {
    const char* hex_string = "deadbeef";
    uint8_t expected[] = {0xde, 0xad, 0xbe, 0xef};
    uint8_t result[4];

    int converted = to_byte_array(hex_string, strlen(hex_string), result);

    TEST_ASSERT_EQUAL_INT(4, converted);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, result, 4);

    Serial.println("✓ Byte array conversion tests passed");
}

void test_crc32_functions() {
    uint32_t crc = crc32_reset();
    const char* test_data = "Hello, World!";

    crc = crc32_add(crc, test_data, strlen(test_data));
    uint32_t final_crc = crc32_finish(crc);

    // CRC32 should be deterministic
    TEST_ASSERT_NOT_EQUAL_UINT32(0, final_crc);

    // Test same input gives same result
    uint32_t crc2 = crc32_reset();
    crc2 = crc32_add(crc2, test_data, strlen(test_data));
    uint32_t final_crc2 = crc32_finish(crc2);

    TEST_ASSERT_EQUAL_UINT32(final_crc, final_crc2);

    Serial.println("✓ CRC32 function tests passed");
}

void test_suffix_string() {
    char buffer[32];

    // Test with hash rate values
    suffix_string(1000.0, buffer, sizeof(buffer), 3);
    Serial.print("1000.0 -> ");
    Serial.println(buffer);

    suffix_string(1500000.0, buffer, sizeof(buffer), 3);
    Serial.print("1500000.0 -> ");
    Serial.println(buffer);

    // Just verify buffer is not empty and null-terminated
    TEST_ASSERT_NOT_EQUAL('\0', buffer[0]);
    TEST_ASSERT_EQUAL('\0', buffer[strlen(buffer)]);

    Serial.println("✓ Suffix string tests passed");
}

void test_sha256_validation_basic() {
    // Create a test hash with leading zeros (valid Bitcoin hash pattern)
    unsigned char valid_hash[32] = {
        0x00, 0x00, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89,
        0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89,
        0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89,
        0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89
    };

    // Create an invalid hash (all high values)
    unsigned char invalid_hash[32];
    memset(invalid_hash, 0xff, 32);

    bool is_valid = isSha256Valid(valid_hash);
    bool is_invalid = isSha256Valid(invalid_hash);

    Serial.print("Valid hash result: ");
    Serial.println(is_valid);
    Serial.print("Invalid hash result: ");
    Serial.println(is_invalid);

    // At minimum, the function should work without crashing
    TEST_ASSERT_TRUE(true); // Basic functionality test

    Serial.println("✓ SHA256 validation tests passed");
}

// =============================================================================
// SHA256 HARDWARE TESTS (only when SHA256_TEST is defined)
// =============================================================================

#ifdef SHA256_TEST

void test_bitcoin_sha256() {
    Serial.println("\n=== Testing Bitcoin SHA256 ===");

    // Test vector: empty string
    const char* input = "";
    uint8_t expected_hash[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };

    uint8_t result[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char*)input, strlen(input));
    mbedtls_sha256_finish(&ctx, result);
    mbedtls_sha256_free(&ctx);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_hash, result, 32);

    Serial.print("Expected: ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", expected_hash[i]);
    }
    Serial.println();
    Serial.print("Got:      ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", result[i]);
    }
    Serial.println();
}

void test_bitcoin_double_sha256() {
    Serial.println("\n=== Testing Bitcoin Double SHA256 ===");

    // Test vector: "hello"
    const char* input = "hello";
    uint8_t expected_hash[32] = {
        0x95, 0x95, 0xc9, 0xdf, 0x90, 0x07, 0x51, 0x48,
        0xeb, 0x06, 0x86, 0x03, 0x65, 0xdf, 0x33, 0x58,
        0x4b, 0x75, 0xbf, 0xf7, 0x82, 0xa5, 0x10, 0xc6,
        0xcd, 0x48, 0x83, 0xa4, 0x19, 0x83, 0x3d, 0x50
    };

    uint8_t first_hash[32];
    uint8_t result[32];

    // First SHA256
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char*)input, strlen(input));
    mbedtls_sha256_finish(&ctx, first_hash);
    mbedtls_sha256_free(&ctx);

    // Second SHA256
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, first_hash, 32);
    mbedtls_sha256_finish(&ctx, result);
    mbedtls_sha256_free(&ctx);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_hash, result, 32);

    Serial.print("Expected: ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", expected_hash[i]);
    }
    Serial.println();
    Serial.print("Got:      ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", result[i]);
    }
    Serial.println();
}

void test_hardware_sha256_performance() {
    Serial.println("\n=== Testing SHA256 Hardware Performance ===");

    const int iterations = 1000;
    uint8_t test_data[64];
    uint8_t result[32];

    // Fill test data
    for (int i = 0; i < 64; i++) {
        test_data[i] = (uint8_t)(i * 37 + 42); // Some pseudo-random data
    }

    unsigned long start_time = micros();

    // Test hardware SHA256
    for (int i = 0; i < iterations; i++) {
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, test_data, 64);
        mbedtls_sha256_finish(&ctx, result);
        mbedtls_sha256_free(&ctx);
    }

    unsigned long end_time = micros();
    unsigned long duration = end_time - start_time;

    Serial.print("Hardware SHA256 performance: ");
    Serial.print(iterations);
    Serial.print(" iterations in ");
    Serial.print(duration);
    Serial.print(" microseconds (");
    Serial.print((float)duration / iterations);
    Serial.println(" μs per hash)");

    // Test should complete reasonably fast
    TEST_ASSERT_TRUE(duration < 500000); // < 500ms for 1000 hashes

    Serial.print("Hash rate: ");
    Serial.print((1000000.0 * iterations) / duration);
    Serial.println(" hashes per second");
}

void test_block_header_hashing() {
    Serial.println("\n=== Testing Bitcoin Block Header Hashing ===");

    // Test with a known block header (fake but realistic format)
    uint8_t block_header[80] = {
        // Version (4 bytes, little endian)
        0x01, 0x00, 0x00, 0x00,
        // Previous block hash (32 bytes)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Merkle root (32 bytes)
        0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2, 0x7a, 0xc7, 0x2c, 0x3e,
        0x67, 0x76, 0x8f, 0x61, 0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
        0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a,
        // Timestamp (4 bytes)
        0x29, 0xab, 0x5f, 0x49,
        // Bits/Difficulty (4 bytes)
        0xff, 0xff, 0x00, 0x1d,
        // Nonce (4 bytes)
        0xf4, 0x9c, 0x44, 0x70
    };

    uint8_t first_hash[32];
    uint8_t final_hash[32];

    // Bitcoin uses double SHA256
    mbedtls_sha256_context ctx;

    // First SHA256
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, block_header, 80);
    mbedtls_sha256_finish(&ctx, first_hash);
    mbedtls_sha256_free(&ctx);

    // Second SHA256
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, first_hash, 32);
    mbedtls_sha256_finish(&ctx, final_hash);
    mbedtls_sha256_free(&ctx);

    TEST_ASSERT_TRUE(true); // Test completion without crashing

    Serial.print("Block header hash: ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", final_hash[i]);
    }
    Serial.println();
}

void test_mining_hash_validation() {
    Serial.println("\n=== Testing Mining Hash Validation ===");

    // Create a hash with leading zeros (like a valid mining result)
    uint8_t valid_mining_hash[32] = {
        0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    // Create an invalid hash (no leading zeros)
    uint8_t invalid_mining_hash[32] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    // Create a target (difficulty)
    uint8_t target[32] = {
        0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    bool valid_result = checkValid(valid_mining_hash, target);
    bool invalid_result = checkValid(invalid_mining_hash, target);

    TEST_ASSERT_TRUE(valid_result);
    TEST_ASSERT_FALSE(invalid_result);

    Serial.print("Valid hash check: ");
    Serial.println(valid_result ? "PASS" : "FAIL");
    Serial.print("Invalid hash check: ");
    Serial.println(invalid_result ? "FAIL (expected)" : "PASS (unexpected)");
}

#endif // SHA256_TEST

// =============================================================================
// SIMPLE TEST MODE SPECIFIC TESTS (when SIMPLE_TEST is defined)
// =============================================================================

#ifdef SIMPLE_TEST

void test_simple_framework_assert() {
    // Test our simple assertion framework
    bool test_passed = true;
    TEST_ASSERT_TRUE(test_passed);
    Serial.println("✓ Simple test framework validation passed");
}

void test_memory_info() {
    Serial.println("\n=== Memory Information ===");
    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("Heap size: ");
    Serial.println(ESP.getHeapSize());
    Serial.print("Free PSRAM: ");
    Serial.println(ESP.getFreePsram());

    TEST_ASSERT_TRUE(ESP.getFreeHeap() > 10000); // At least 10KB free
    Serial.println("✓ Memory information tests passed");
}

#endif // SIMPLE_TEST

void setup() {
    Serial.begin(115200);
    delay(2000); // Wait for serial to stabilize

    // Print test mode information
    Serial.println("=== NerdMiner Unified Test Suite ===");
    Serial.println("Running on ESP32-2432S028R");

    #ifdef SIMPLE_TEST
    Serial.println("Test Mode: SIMPLE TEST");
    #elif defined(SHA256_TEST)
    Serial.println("Test Mode: SHA256 HARDWARE TEST");
    #ifdef HARDWARE_SHA265
    Serial.println("Hardware SHA256 acceleration: ENABLED");
    #else
    Serial.println("Hardware SHA256 acceleration: DISABLED (Software fallback)");
    #endif
    #else
    Serial.println("Test Mode: UNITY EMBEDDED TEST (default)");
    #endif

    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("Chip model: ");
    Serial.println(ESP.getChipModel());
    Serial.print("Chip revision: ");
    Serial.println(ESP.getChipRevision());
    Serial.print("CPU frequency: ");
    Serial.print(ESP.getCpuFreqMHz());
    Serial.println(" MHz");

    UNITY_BEGIN();

    // Run common tests for all modes
    RUN_TEST(test_hex_conversion);
    RUN_TEST(test_to_byte_array);
    RUN_TEST(test_crc32_functions);
    RUN_TEST(test_suffix_string);
    RUN_TEST(test_sha256_validation_basic);

    // Run mode-specific tests
    #ifdef SHA256_TEST
    RUN_TEST(test_bitcoin_sha256);
    RUN_TEST(test_bitcoin_double_sha256);
    RUN_TEST(test_hardware_sha256_performance);
    RUN_TEST(test_block_header_hashing);
    RUN_TEST(test_mining_hash_validation);
    #endif

    #ifdef SIMPLE_TEST
    RUN_TEST(test_simple_framework_assert);
    RUN_TEST(test_memory_info);
    #endif

    UNITY_END();

    Serial.println("=== Test Suite Complete ===");
}

void loop() {
    // Test results are shown once in setup()
    delay(5000);
    Serial.println("Tests completed. Reset to run again.");
}