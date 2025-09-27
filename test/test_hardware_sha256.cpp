// Only compile this for embedded tests (ESP32 hardware)
#if !defined(NATIVE_TEST) && defined(HARDWARE_SHA256_TEST)

#include <unity.h>
#include <Arduino.h>
#include <mbedtls/sha256.h>
#include "test_utils.h"
#include "fixtures/sha256_test_vectors.h"

// ESP32 hardware SHA256 acceleration (if available)
#ifdef ESP32
#include "esp_system.h"
#include "esp_log.h"
// Use mbedtls for hardware accelerated SHA256 instead of low-level HAL
#endif

void setUp(void) {
    Serial.println("Setting up hardware SHA256 test...");
}

void tearDown(void) {
    Serial.println("Tearing down hardware SHA256 test...");
}

//=============================================================================
// HARDWARE SHA256 TESTS
//=============================================================================

// Software SHA256 implementation for comparison
void software_sha256(const uint8_t* input, size_t input_len, uint8_t* output) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA256 (not SHA224)
    mbedtls_sha256_update(&ctx, input, input_len);
    mbedtls_sha256_finish(&ctx, output);
    mbedtls_sha256_free(&ctx);
}

// Hardware SHA256 implementation (ESP32 specific)
void hardware_sha256(const uint8_t* input, size_t input_len, uint8_t* output) {
#ifdef ESP32
    // Use ESP32 hardware acceleration if available
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA256
    mbedtls_sha256_update(&ctx, input, input_len);
    mbedtls_sha256_finish(&ctx, output);
    mbedtls_sha256_free(&ctx);
#else
    // Fallback to software implementation
    software_sha256(input, input_len, output);
#endif
}

// Test basic hardware SHA256 functionality
void test_hardware_sha256_basic(void) {
    uint8_t input[] = "abc";
    uint8_t hardware_result[32];
    uint8_t software_result[32];
    uint8_t expected[32];

    // Get expected result
    hex_string_to_bytes(SHA256_TV2_EXPECTED, expected, 32);

    // Test hardware implementation
    hardware_sha256(input, 3, hardware_result);

    // Test software implementation
    software_sha256(input, 3, software_result);

    // Compare results
    assert_bytes_equal(expected, hardware_result, 32, "Hardware SHA256 test failed");
    assert_bytes_equal(expected, software_result, 32, "Software SHA256 test failed");
    assert_bytes_equal(hardware_result, software_result, 32, "Hardware vs Software SHA256 mismatch");

    Serial.println("Hardware SHA256 basic test passed");
}

// Test SHA256 with empty string
void test_hardware_sha256_empty(void) {
    uint8_t input[] = "";
    uint8_t hardware_result[32];
    uint8_t expected[32];

    hex_string_to_bytes(SHA256_TV1_EXPECTED, expected, 32);
    hardware_sha256(input, 0, hardware_result);

    assert_bytes_equal(expected, hardware_result, 32, "Hardware SHA256 empty string test failed");

    Serial.println("Hardware SHA256 empty string test passed");
}

// Test SHA256 with longer message
void test_hardware_sha256_message_digest(void) {
    uint8_t input[] = "message digest";
    uint8_t hardware_result[32];
    uint8_t expected[32];

    hex_string_to_bytes(SHA256_TV3_EXPECTED, expected, 32);
    hardware_sha256(input, 14, hardware_result);

    assert_bytes_equal(expected, hardware_result, 32, "Hardware SHA256 message digest test failed");

    Serial.println("Hardware SHA256 message digest test passed");
}

// Test double SHA256 (Bitcoin mining style)
void test_hardware_double_sha256(void) {
    uint8_t input[] = "hello";
    uint8_t first_hash[32];
    uint8_t double_hash[32];
    uint8_t expected[32];

    // First SHA256
    hardware_sha256(input, 5, first_hash);

    // Second SHA256
    hardware_sha256(first_hash, 32, double_hash);

    // Compare with expected
    hex_string_to_bytes(SHA256_DOUBLE_TV1_EXPECTED, expected, 32);
    assert_bytes_equal(expected, double_hash, 32, "Hardware double SHA256 test failed");

    Serial.println("Hardware double SHA256 test passed");
}

// Test Bitcoin block header hashing
void test_bitcoin_block_header_hashing(void) {
    uint8_t block_header[80];
    uint8_t hash_result[32];

    // Copy test block header
    memcpy(block_header, BITCOIN_BLOCK_HEADER_TV1, 80);

    // Compute double SHA256 (Bitcoin style)
    uint8_t first_hash[32];
    hardware_sha256(block_header, 80, first_hash);
    hardware_sha256(first_hash, 32, hash_result);

    // Verify hash is valid (not all zeros)
    TEST_ASSERT_TRUE(validate_sha256_hash(hash_result));

    print_bytes_hex(hash_result, 32, "Block Header Hash");

    Serial.println("Bitcoin block header hashing test passed");
}

// Performance test: Hardware vs Software SHA256
void test_sha256_performance_comparison(void) {
    uint8_t test_data[64];
    uint8_t hardware_result[32];
    uint8_t software_result[32];

    // Fill test data
    for (int i = 0; i < 64; i++) {
        test_data[i] = i & 0xFF;
    }

    uint32_t iterations = 100;

    // Test hardware performance
    uint32_t start_time = millis();
    for (uint32_t i = 0; i < iterations; i++) {
        hardware_sha256(test_data, 64, hardware_result);
    }
    uint32_t hardware_time = millis() - start_time;

    // Test software performance
    start_time = millis();
    for (uint32_t i = 0; i < iterations; i++) {
        software_sha256(test_data, 64, software_result);
    }
    uint32_t software_time = millis() - start_time;

    // Results should be identical
    assert_bytes_equal(hardware_result, software_result, 32, "Performance test: Hardware vs Software mismatch");

    // Log performance results
    Serial.printf("Hardware SHA256 time: %u ms for %u iterations\n", hardware_time, iterations);
    Serial.printf("Software SHA256 time: %u ms for %u iterations\n", software_time, iterations);

    if (hardware_time > 0) {
        float hardware_rate = (float)iterations / hardware_time * 1000.0f; // Hashes per second
        Serial.printf("Hardware rate: %.2f H/s\n", hardware_rate);
    }

    if (software_time > 0) {
        float software_rate = (float)iterations / software_time * 1000.0f; // Hashes per second
        Serial.printf("Software rate: %.2f H/s\n", software_rate);
    }

    // Hardware should generally be faster or similar to software
    // (Note: mbedtls may already use hardware acceleration on ESP32)
    TEST_ASSERT_TRUE(hardware_time <= software_time * 2); // Allow some variance

    Serial.println("SHA256 performance comparison test passed");
}

// Test hash rate measurement for mining
void test_mining_hashrate_measurement(void) {
    uint8_t block_header[80];
    uint8_t hash_result[32];

    // Initialize block header
    memcpy(block_header, BITCOIN_BLOCK_HEADER_TV1, 80);

    uint32_t iterations = 50; // Reasonable number for test
    uint32_t start_time = millis();

    // Simulate mining loop with nonce iteration
    for (uint32_t nonce = 0; nonce < iterations; nonce++) {
        // Set nonce in block header (bytes 76-79, little endian)
        *((uint32_t*)&block_header[76]) = nonce;

        // Compute double SHA256
        uint8_t first_hash[32];
        hardware_sha256(block_header, 80, first_hash);
        hardware_sha256(first_hash, 32, hash_result);

        // Verify hash is valid
        TEST_ASSERT_TRUE(validate_sha256_hash(hash_result));
    }

    uint32_t total_time = millis() - start_time;

    if (total_time > 0) {
        float hashrate = (float)iterations / total_time * 1000.0f; // Hashes per second
        Serial.printf("Mining hashrate: %.2f H/s (%u hashes in %u ms)\n", hashrate, iterations, total_time);

        // Verify reasonable hashrate for ESP32
        TEST_ASSERT_TRUE(hashrate >= 1.0f);    // At least 1 H/s
        TEST_ASSERT_TRUE(hashrate <= 50000.0f); // Less than 50 KH/s (reasonable upper bound for ESP32)
    }

    Serial.println("Mining hashrate measurement test passed");
}

// Test memory usage during SHA256 operations
void test_sha256_memory_usage(void) {
    uint32_t free_heap_before = ESP.getFreeHeap();
    Serial.printf("Free heap before SHA256 tests: %u bytes\n", free_heap_before);

    uint8_t test_data[128];
    uint8_t hash_result[32];

    // Fill test data
    for (int i = 0; i < 128; i++) {
        test_data[i] = i & 0xFF;
    }

    // Perform multiple SHA256 operations
    for (int i = 0; i < 10; i++) {
        hardware_sha256(test_data, 128, hash_result);
        TEST_ASSERT_TRUE(validate_sha256_hash(hash_result));
    }

    uint32_t free_heap_after = ESP.getFreeHeap();
    Serial.printf("Free heap after SHA256 tests: %u bytes\n", free_heap_after);

    // Check for memory leaks
    int32_t heap_difference = (int32_t)free_heap_after - (int32_t)free_heap_before;
    Serial.printf("Heap difference: %d bytes\n", heap_difference);

    // Allow small variations in heap usage
    TEST_ASSERT_TRUE(abs(heap_difference) < 1000); // Less than 1KB difference

    Serial.println("SHA256 memory usage test passed");
}

// Test concurrent SHA256 operations (if applicable)
void test_concurrent_sha256(void) {
    uint8_t input1[] = "test input 1";
    uint8_t input2[] = "test input 2 with different length";
    uint8_t result1[32];
    uint8_t result2[32];

    // Compute hashes
    hardware_sha256(input1, strlen((char*)input1), result1);
    hardware_sha256(input2, strlen((char*)input2), result2);

    // Verify both results are valid and different
    TEST_ASSERT_TRUE(validate_sha256_hash(result1));
    TEST_ASSERT_TRUE(validate_sha256_hash(result2));
    TEST_ASSERT_FALSE(memcmp(result1, result2, 32) == 0); // Should be different

    print_bytes_hex(result1, 32, "Hash 1");
    print_bytes_hex(result2, 32, "Hash 2");

    Serial.println("Concurrent SHA256 test passed");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial port to connect
    }
    delay(2000); // Allow time for serial to initialize

    Serial.println("Starting ESP32 Hardware SHA256 tests...");

    UNITY_BEGIN();

    // Hardware SHA256 Tests
    RUN_TEST(test_hardware_sha256_basic);
    RUN_TEST(test_hardware_sha256_empty);
    RUN_TEST(test_hardware_sha256_message_digest);
    RUN_TEST(test_hardware_double_sha256);
    RUN_TEST(test_bitcoin_block_header_hashing);
    RUN_TEST(test_sha256_performance_comparison);
    RUN_TEST(test_mining_hashrate_measurement);
    RUN_TEST(test_sha256_memory_usage);
    RUN_TEST(test_concurrent_sha256);

    UNITY_END();

    Serial.println("All hardware SHA256 tests completed!");
}

void loop() {
    // Empty loop for embedded test runner
    // Tests run once in setup()
}

#endif // !defined(NATIVE_TEST) && defined(HARDWARE_SHA256_TEST)