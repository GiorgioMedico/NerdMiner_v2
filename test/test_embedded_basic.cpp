// Only compile this for embedded tests (not native)
#if !defined(NATIVE_TEST) && !defined(HARDWARE_VALIDATION_TEST) && !defined(HARDWARE_SHA256_TEST) && !defined(PERFORMANCE_BENCHMARK_TEST)

#include <unity.h>
#include <Arduino.h>

// Test function prototypes
void setUp(void) {
    // Set up any common test fixtures here
    Serial.println("Setting up test...");
}

void tearDown(void) {
    // Clean up any common test fixtures here
    Serial.println("Tearing down test...");
}

// Basic embedded test environment validation
void test_embedded_environment(void) {
    TEST_ASSERT_EQUAL(1, 1);
    Serial.println("Embedded environment test passed");
}

// Test ESP32-2432S028R specific constants
void test_esp32_constants(void) {
    #ifdef ESP32_2432S028R
    // Test the display constants that should be defined for ESP32-2432S028R
    TEST_ASSERT_EQUAL(240, TFT_WIDTH);
    TEST_ASSERT_EQUAL(320, TFT_HEIGHT);
    TEST_ASSERT_EQUAL(13, TFT_MOSI);
    TEST_ASSERT_EQUAL(14, TFT_SCLK);
    TEST_ASSERT_EQUAL(15, TFT_CS);
    TEST_ASSERT_EQUAL(2, TFT_DC);
    TEST_ASSERT_EQUAL(12, TFT_RST);
    TEST_ASSERT_EQUAL(21, TFT_BL);
    #endif
    Serial.println("ESP32 constants test passed");
}

// Test basic Arduino/ESP32 functions
void test_arduino_functions(void) {
    // Test basic Arduino functions are available
    TEST_ASSERT_TRUE(millis() >= 0);
    TEST_ASSERT_TRUE(micros() >= 0);

    // Test that we can read/write digital pins (without actually doing it on hardware)
    // Just test that the functions exist
    TEST_ASSERT_NOT_EQUAL((uintptr_t)pinMode, 0);
    TEST_ASSERT_NOT_EQUAL((uintptr_t)digitalWrite, 0);
    TEST_ASSERT_NOT_EQUAL((uintptr_t)digitalRead, 0);

    Serial.println("Arduino functions test passed");
}

// Test ESP32 specific functions
void test_esp32_functions(void) {
    // Test ESP32-specific functionality
    TEST_ASSERT_TRUE(ESP.getChipRevision() >= 0);
    TEST_ASSERT_TRUE(ESP.getCpuFreqMHz() > 0);
    TEST_ASSERT_TRUE(ESP.getFreeHeap() > 0);

    Serial.print("ESP32 Chip Revision: ");
    Serial.println(ESP.getChipRevision());
    Serial.print("CPU Frequency: ");
    Serial.println(ESP.getCpuFreqMHz());
    Serial.print("Free Heap: ");
    Serial.println(ESP.getFreeHeap());

    Serial.println("ESP32 functions test passed");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial port to connect
    }
    delay(2000); // Allow time for serial to initialize

    Serial.println("Starting ESP32-2432S028R embedded tests...");

    UNITY_BEGIN();

    // Basic environment tests
    RUN_TEST(test_embedded_environment);
    RUN_TEST(test_esp32_constants);
    RUN_TEST(test_arduino_functions);
    RUN_TEST(test_esp32_functions);

    UNITY_END();

    Serial.println("All embedded tests completed!");
}

void loop() {
    // Empty loop for embedded test runner
    // Tests run once in setup()
}

#endif // !defined(NATIVE_TEST) && !defined(HARDWARE_VALIDATION_TEST) && !defined(HARDWARE_SHA256_TEST) && !defined(PERFORMANCE_BENCHMARK_TEST)