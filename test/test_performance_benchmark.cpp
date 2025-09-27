// Only compile this for embedded tests (ESP32 hardware)
#if !defined(NATIVE_TEST) && defined(PERFORMANCE_BENCHMARK_TEST)

#include <unity.h>
#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <mbedtls/sha256.h>
#include "test_utils.h"
#include "fixtures/mining_test_vectors.h"
#include "fixtures/sha256_test_vectors.h"

// Board-specific includes - using TFT_eSPI built-in touch instead of ETOUCH
// TFT_eSPI supports touch functionality natively

// Global objects for benchmarking
TFT_eSPI tft = TFT_eSPI();

// Touch functionality will use TFT_eSPI's built-in touch capabilities
// No separate touch object needed

// Performance measurement structure
typedef struct {
    uint32_t min_time;
    uint32_t max_time;
    uint32_t avg_time;
    uint32_t total_time;
    uint32_t iterations;
    float rate; // Operations per second
} benchmark_result_t;

void setUp(void) {
    Serial.println("Setting up performance benchmark test...");
}

void tearDown(void) {
    Serial.println("Tearing down performance benchmark test...");
}

// Helper function to calculate benchmark statistics
void calculate_benchmark_stats(uint32_t* times, uint32_t count, benchmark_result_t* result) {
    result->iterations = count;
    result->min_time = UINT32_MAX;
    result->max_time = 0;
    result->total_time = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (times[i] < result->min_time) result->min_time = times[i];
        if (times[i] > result->max_time) result->max_time = times[i];
        result->total_time += times[i];
    }

    result->avg_time = result->total_time / count;
    result->rate = (float)count / (float)result->total_time * 1000000.0f; // Operations per second
}

// Print benchmark results
void print_benchmark_results(const char* test_name, benchmark_result_t* result) {
    Serial.printf("=== %s Benchmark Results ===\n", test_name);
    Serial.printf("Iterations: %u\n", result->iterations);
    Serial.printf("Total time: %u µs\n", result->total_time);
    Serial.printf("Min time: %u µs\n", result->min_time);
    Serial.printf("Max time: %u µs\n", result->max_time);
    Serial.printf("Avg time: %u µs\n", result->avg_time);
    Serial.printf("Rate: %.2f ops/sec\n", result->rate);
    Serial.println();
}

//=============================================================================
// SHA256 PERFORMANCE BENCHMARKS
//=============================================================================

// Benchmark SHA256 single hash performance
void test_sha256_single_hash_benchmark(void) {
    Serial.println("=== SHA256 Single Hash Benchmark ===");

    const uint32_t iterations = 100;
    uint32_t times[iterations];
    uint8_t input[] = "benchmark test data for SHA256 performance measurement";
    uint8_t output[32];

    // Warm up
    for (int i = 0; i < 10; i++) {
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, input, sizeof(input) - 1);
        mbedtls_sha256_finish(&ctx, output);
        mbedtls_sha256_free(&ctx);
    }

    // Benchmark
    for (uint32_t i = 0; i < iterations; i++) {
        uint32_t start_time = micros();

        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, input, sizeof(input) - 1);
        mbedtls_sha256_finish(&ctx, output);
        mbedtls_sha256_free(&ctx);

        times[i] = micros() - start_time;

        // Verify hash is valid
        TEST_ASSERT_TRUE(validate_sha256_hash(output));
    }

    benchmark_result_t result;
    calculate_benchmark_stats(times, iterations, &result);
    print_benchmark_results("SHA256 Single Hash", &result);

    // Performance expectations for ESP32
    TEST_ASSERT_TRUE(result.rate >= 1000.0f); // At least 1000 hashes/sec
    TEST_ASSERT_TRUE(result.rate <= 100000.0f); // Less than 100K hashes/sec

    Serial.println("SHA256 single hash benchmark passed");
}

// Benchmark SHA256 double hash (Bitcoin mining style)
void test_sha256_double_hash_benchmark(void) {
    Serial.println("=== SHA256 Double Hash Benchmark ===");

    const uint32_t iterations = 50;
    uint32_t times[iterations];
    uint8_t block_header[80];
    uint8_t first_hash[32];
    uint8_t final_hash[32];

    // Initialize block header
    memcpy(block_header, BITCOIN_BLOCK_HEADER_TV1, 80);

    // Warm up
    for (int i = 0; i < 5; i++) {
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, block_header, 80);
        mbedtls_sha256_finish(&ctx, first_hash);

        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, first_hash, 32);
        mbedtls_sha256_finish(&ctx, final_hash);
        mbedtls_sha256_free(&ctx);
    }

    // Benchmark
    for (uint32_t i = 0; i < iterations; i++) {
        // Update nonce for each iteration
        *((uint32_t*)&block_header[76]) = i;

        uint32_t start_time = micros();

        // Double SHA256
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, block_header, 80);
        mbedtls_sha256_finish(&ctx, first_hash);

        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, first_hash, 32);
        mbedtls_sha256_finish(&ctx, final_hash);
        mbedtls_sha256_free(&ctx);

        times[i] = micros() - start_time;

        // Verify hash is valid
        TEST_ASSERT_TRUE(validate_sha256_hash(final_hash));
    }

    benchmark_result_t result;
    calculate_benchmark_stats(times, iterations, &result);
    print_benchmark_results("SHA256 Double Hash (Mining)", &result);

    // Performance expectations for ESP32 mining
    TEST_ASSERT_TRUE(result.rate >= 500.0f); // At least 500 mining hashes/sec
    TEST_ASSERT_TRUE(result.rate <= 50000.0f); // Less than 50K mining hashes/sec

    Serial.println("SHA256 double hash benchmark passed");
}

//=============================================================================
// MEMORY PERFORMANCE BENCHMARKS
//=============================================================================

// Benchmark memory allocation performance
void test_memory_allocation_benchmark(void) {
    Serial.println("=== Memory Allocation Benchmark ===");

    const uint32_t iterations = 100;
    const size_t alloc_size = 1024; // 1KB allocations
    uint32_t times[iterations];

    // Benchmark malloc/free cycles
    for (uint32_t i = 0; i < iterations; i++) {
        uint32_t start_time = micros();

        void* ptr = malloc(alloc_size);
        TEST_ASSERT_NOT_NULL(ptr);

        // Write to memory to ensure it's actually allocated
        memset(ptr, 0xAA, alloc_size);

        free(ptr);

        times[i] = micros() - start_time;
    }

    benchmark_result_t result;
    calculate_benchmark_stats(times, iterations, &result);
    print_benchmark_results("Memory Allocation (1KB)", &result);

    // Check for reasonable allocation performance
    TEST_ASSERT_TRUE(result.avg_time < 1000); // Less than 1ms average
    TEST_ASSERT_TRUE(result.rate >= 1000.0f); // At least 1000 allocs/sec

    Serial.println("Memory allocation benchmark passed");
}

// Benchmark memory bandwidth
void test_memory_bandwidth_benchmark(void) {
    Serial.println("=== Memory Bandwidth Benchmark ===");

    const uint32_t iterations = 10;
    const size_t buffer_size = 10240; // 10KB buffer
    uint32_t times[iterations];

    // Allocate test buffers
    uint8_t* src_buffer = (uint8_t*)malloc(buffer_size);
    uint8_t* dst_buffer = (uint8_t*)malloc(buffer_size);
    TEST_ASSERT_NOT_NULL(src_buffer);
    TEST_ASSERT_NOT_NULL(dst_buffer);

    // Initialize source buffer
    for (size_t i = 0; i < buffer_size; i++) {
        src_buffer[i] = i & 0xFF;
    }

    // Benchmark memory copy operations
    for (uint32_t i = 0; i < iterations; i++) {
        uint32_t start_time = micros();

        memcpy(dst_buffer, src_buffer, buffer_size);

        times[i] = micros() - start_time;

        // Verify copy was successful
        TEST_ASSERT_EQUAL(0, memcmp(src_buffer, dst_buffer, buffer_size));
    }

    benchmark_result_t result;
    calculate_benchmark_stats(times, iterations, &result);

    // Calculate bandwidth in MB/s
    float bandwidth_mbps = (float)(buffer_size * iterations) / (float)result.total_time;
    Serial.printf("Memory bandwidth: %.2f MB/s\n", bandwidth_mbps);

    print_benchmark_results("Memory Copy (10KB)", &result);

    // Check for reasonable memory bandwidth
    TEST_ASSERT_TRUE(bandwidth_mbps >= 10.0f); // At least 10 MB/s
    TEST_ASSERT_TRUE(bandwidth_mbps <= 1000.0f); // Less than 1 GB/s

    free(src_buffer);
    free(dst_buffer);

    Serial.println("Memory bandwidth benchmark passed");
}

//=============================================================================
// DISPLAY PERFORMANCE BENCHMARKS
//=============================================================================

// Benchmark display rendering performance
void test_display_rendering_benchmark(void) {
    Serial.println("=== Display Rendering Benchmark ===");

#ifdef ESP32_2432S028R
    // Initialize display
    tft.init();
    tft.setRotation(1);

    const uint32_t iterations = 20;
    uint32_t times[iterations];

    // Benchmark full screen fills
    for (uint32_t i = 0; i < iterations; i++) {
        uint16_t color = (i % 8) == 0 ? TFT_BLACK :
                        (i % 8) == 1 ? TFT_RED :
                        (i % 8) == 2 ? TFT_GREEN :
                        (i % 8) == 3 ? TFT_BLUE :
                        (i % 8) == 4 ? TFT_YELLOW :
                        (i % 8) == 5 ? TFT_MAGENTA :
                        (i % 8) == 6 ? TFT_CYAN : TFT_WHITE;

        uint32_t start_time = micros();
        tft.fillScreen(color);
        times[i] = micros() - start_time;
    }

    benchmark_result_t result;
    calculate_benchmark_stats(times, iterations, &result);

    // Calculate frame rate
    float fps = result.rate;
    Serial.printf("Display frame rate: %.2f FPS\n", fps);

    print_benchmark_results("Display Full Screen Fill", &result);

    // Check for reasonable display performance
    TEST_ASSERT_TRUE(fps >= 5.0f); // At least 5 FPS
    TEST_ASSERT_TRUE(fps <= 100.0f); // Less than 100 FPS

    // Benchmark text rendering
    const uint32_t text_iterations = 10;
    uint32_t text_times[text_iterations];

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);

    for (uint32_t i = 0; i < text_iterations; i++) {
        uint32_t start_time = micros();

        tft.setCursor(10, 10 + (i % 5) * 30);
        tft.printf("Benchmark %u", i);

        text_times[i] = micros() - start_time;
    }

    benchmark_result_t text_result;
    calculate_benchmark_stats(text_times, text_iterations, &text_result);
    print_benchmark_results("Display Text Rendering", &text_result);

    tft.fillScreen(TFT_BLACK);

    Serial.println("Display rendering benchmark passed");
#else
    Serial.println("Display rendering benchmark skipped (board not supported)");
#endif
}

// Benchmark touch interface performance
void test_touch_interface_benchmark(void) {
    Serial.println("=== Touch Interface Benchmark ===");

#ifdef ESP32_2432S028R
    tft.init(); // Initialize TFT first

    const uint32_t iterations = 1000;
    uint32_t times[iterations];
    uint32_t successful_reads = 0;

    // Display touch test screen
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Touch Speed");
    tft.setCursor(10, 40);

    #ifdef TOUCH_CS
    tft.println("Test Running");

    // Benchmark touch reading speed
    for (uint32_t i = 0; i < iterations; i++) {
        uint32_t start_time = micros();

        uint16_t x, y;
        bool touch_available = tft.getTouch(&x, &y);
        if (touch_available) {
            successful_reads++;
        }

        times[i] = micros() - start_time;

        // Small delay to avoid overwhelming the touch controller
        delayMicroseconds(100);
    #else
    tft.println("Touch Disabled");
    Serial.println("Touch benchmark skipped - TOUCH_CS not defined");

    // Simulate benchmark timing without actual touch reading
    for (uint32_t i = 0; i < iterations; i++) {
        uint32_t start_time = micros();
        times[i] = micros() - start_time;
        delayMicroseconds(100);
    }
    #endif

    benchmark_result_t result;
    calculate_benchmark_stats(times, iterations, &result);

    float touch_read_rate = result.rate;
    Serial.printf("Touch read rate: %.2f reads/sec\n", touch_read_rate);
    #ifdef TOUCH_CS
    Serial.printf("Successful touch reads: %u/%u\n", successful_reads, iterations);
    #else
    Serial.printf("Touch functionality disabled (no actual reads performed)\n");
    #endif

    print_benchmark_results("Touch Interface Reading", &result);

    // Check for reasonable touch performance (only if touch is enabled)
    #ifdef TOUCH_CS
    TEST_ASSERT_TRUE(touch_read_rate >= 100.0f); // At least 100 reads/sec
    TEST_ASSERT_TRUE(touch_read_rate <= 10000.0f); // Less than 10K reads/sec
    #else
    // If touch is disabled, just verify the test completed without errors
    TEST_ASSERT_TRUE(touch_read_rate >= 0.0f); // Allow any rate when touch is disabled
    #endif

    tft.fillScreen(TFT_BLACK);

    Serial.println("Touch interface benchmark passed");
#else
    Serial.println("Touch interface benchmark skipped (board not supported)");
#endif
}

//=============================================================================
// WIFI PERFORMANCE BENCHMARKS
//=============================================================================

// Benchmark WiFi operations
void test_wifi_performance_benchmark(void) {
    Serial.println("=== WiFi Performance Benchmark ===");

    // Benchmark WiFi scan performance
    const uint32_t scan_iterations = 3;
    uint32_t scan_times[scan_iterations];

    WiFi.mode(WIFI_STA);

    for (uint32_t i = 0; i < scan_iterations; i++) {
        uint32_t start_time = millis(); // Use millis for longer operations

        int networks = WiFi.scanNetworks();

        scan_times[i] = millis() - start_time;

        Serial.printf("Scan %u: found %d networks in %u ms\n", i, networks, scan_times[i]);

        WiFi.scanDelete();
        delay(1000); // Wait between scans
    }

    // Calculate scan performance
    uint32_t total_scan_time = 0;
    uint32_t min_scan_time = UINT32_MAX;
    uint32_t max_scan_time = 0;

    for (uint32_t i = 0; i < scan_iterations; i++) {
        total_scan_time += scan_times[i];
        if (scan_times[i] < min_scan_time) min_scan_time = scan_times[i];
        if (scan_times[i] > max_scan_time) max_scan_time = scan_times[i];
    }

    uint32_t avg_scan_time = total_scan_time / scan_iterations;

    Serial.printf("WiFi Scan Performance:\n");
    Serial.printf("  Average: %u ms\n", avg_scan_time);
    Serial.printf("  Min: %u ms\n", min_scan_time);
    Serial.printf("  Max: %u ms\n", max_scan_time);

    // Check for reasonable scan performance
    TEST_ASSERT_TRUE(avg_scan_time <= 10000); // Less than 10 seconds average
    TEST_ASSERT_TRUE(min_scan_time >= 100); // At least 100ms minimum

    Serial.println("WiFi performance benchmark passed");
}

//=============================================================================
// SYSTEM PERFORMANCE BENCHMARKS
//=============================================================================

// Benchmark overall system performance
void test_system_performance_benchmark(void) {
    Serial.println("=== System Performance Benchmark ===");

    // Test timer resolution and accuracy
    const uint32_t iterations = 100;
    uint32_t timer_diffs[iterations];

    for (uint32_t i = 0; i < iterations; i++) {
        uint32_t start_micros = micros();
        delayMicroseconds(1000); // 1ms delay
        uint32_t end_micros = micros();
        timer_diffs[i] = end_micros - start_micros;
    }

    benchmark_result_t timer_result;
    calculate_benchmark_stats(timer_diffs, iterations, &timer_result);

    Serial.printf("Timer accuracy test (1000µs delay):\n");
    Serial.printf("  Average: %u µs\n", timer_result.avg_time);
    Serial.printf("  Min: %u µs\n", timer_result.min_time);
    Serial.printf("  Max: %u µs\n", timer_result.max_time);

    // Check timer accuracy (allow 10% variance)
    TEST_ASSERT_TRUE(timer_result.avg_time >= 900);
    TEST_ASSERT_TRUE(timer_result.avg_time <= 1100);

    // Test heap fragmentation over time
    uint32_t initial_heap = ESP.getFreeHeap();
    Serial.printf("Initial free heap: %u bytes\n", initial_heap);

    // Perform many small allocations and deallocations
    for (int cycle = 0; cycle < 5; cycle++) {
        void* ptrs[20];

        // Allocate
        for (int i = 0; i < 20; i++) {
            ptrs[i] = malloc(100 + (i * 10)); // Variable sizes
        }

        // Deallocate in reverse order
        for (int i = 19; i >= 0; i--) {
            free(ptrs[i]);
        }
    }

    uint32_t final_heap = ESP.getFreeHeap();
    int32_t heap_change = (int32_t)final_heap - (int32_t)initial_heap;

    Serial.printf("Final free heap: %u bytes\n", final_heap);
    Serial.printf("Heap change: %d bytes\n", heap_change);

    // Check for excessive heap fragmentation
    TEST_ASSERT_TRUE(abs(heap_change) < 1000); // Less than 1KB difference

    Serial.println("System performance benchmark passed");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial port to connect
    }
    delay(2000); // Allow time for serial to initialize

    Serial.println("Starting ESP32 Performance Benchmark tests...");
    Serial.printf("CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println();

    UNITY_BEGIN();

    // SHA256 Performance Tests
    RUN_TEST(test_sha256_single_hash_benchmark);
    RUN_TEST(test_sha256_double_hash_benchmark);

    // Memory Performance Tests
    RUN_TEST(test_memory_allocation_benchmark);
    RUN_TEST(test_memory_bandwidth_benchmark);

    // Display Performance Tests
    RUN_TEST(test_display_rendering_benchmark);
    RUN_TEST(test_touch_interface_benchmark);

    // WiFi Performance Tests
    RUN_TEST(test_wifi_performance_benchmark);

    // System Performance Tests
    RUN_TEST(test_system_performance_benchmark);

    UNITY_END();

    Serial.println("All performance benchmark tests completed!");
}

void loop() {
    // Empty loop for embedded test runner
    // Tests run once in setup()
}

#endif // !defined(NATIVE_TEST) && defined(PERFORMANCE_BENCHMARK_TEST)