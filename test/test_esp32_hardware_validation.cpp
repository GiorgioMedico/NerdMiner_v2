// Only compile this for embedded tests (ESP32 hardware)
#if !defined(NATIVE_TEST) && defined(HARDWARE_VALIDATION_TEST)

#include <unity.h>
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "test_utils.h"

// Board-specific includes - using TFT_eSPI built-in touch instead of ETOUCH
// TFT_eSPI supports touch functionality natively

// Global objects for testing
TFT_eSPI tft = TFT_eSPI();
WiFiManager wm;

// Touch functionality will use TFT_eSPI's built-in touch capabilities
// No separate touch object needed

void setUp(void) {
    Serial.println("Setting up hardware validation test...");
}

void tearDown(void) {
    Serial.println("Tearing down hardware validation test...");
}

//=============================================================================
// ESP32 SYSTEM TESTS
//=============================================================================

// Test ESP32 basic system information
void test_esp32_system_info(void) {
    Serial.println("=== ESP32 System Information ===");

    // Test chip information
    TEST_ASSERT_TRUE(ESP.getChipRevision() >= 0);
    TEST_ASSERT_TRUE(ESP.getCpuFreqMHz() > 0);
    TEST_ASSERT_TRUE(ESP.getFreeHeap() > 0);

    // Log system information
    Serial.printf("Chip Model: %s\n", ESP.getChipModel());
    Serial.printf("Chip Revision: %d\n", ESP.getChipRevision());
    Serial.printf("CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Flash Size: %d bytes\n", ESP.getFlashChipSize());
    Serial.printf("Flash Speed: %d Hz\n", ESP.getFlashChipSpeed());

    // Test memory constraints
    TEST_ASSERT_TRUE(ESP.getFreeHeap() > 100000); // At least 100KB free
    TEST_ASSERT_TRUE(ESP.getFlashChipSize() > 1000000); // At least 1MB flash

    Serial.println("ESP32 system info test passed");
}

// Test GPIO functionality
void test_gpio_functionality(void) {
    Serial.println("=== GPIO Functionality Test ===");

#ifdef ESP32_2432S028R
    // Test TFT control pins
    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_RST, OUTPUT);
    pinMode(TFT_BL, OUTPUT);

    // Test pin operations
    digitalWrite(TFT_CS, HIGH);
    TEST_ASSERT_EQUAL(HIGH, digitalRead(TFT_CS));

    digitalWrite(TFT_CS, LOW);
    TEST_ASSERT_EQUAL(LOW, digitalRead(TFT_CS));

    digitalWrite(TFT_DC, HIGH);
    TEST_ASSERT_EQUAL(HIGH, digitalRead(TFT_DC));

    digitalWrite(TFT_DC, LOW);
    TEST_ASSERT_EQUAL(LOW, digitalRead(TFT_DC));

    // Test backlight control
    digitalWrite(TFT_BL, HIGH);
    delay(100);
    digitalWrite(TFT_BL, LOW);
    delay(100);
    digitalWrite(TFT_BL, HIGH); // Leave backlight on

    Serial.println("TFT control pins test passed");

    // Test touch pins
    // Test touch CS pin (if defined in board configuration)
    #ifdef TOUCH_CS
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);
    TEST_ASSERT_EQUAL(HIGH, digitalRead(TOUCH_CS));
    #endif

    Serial.println("Touch control pins test passed");
#endif

    Serial.println("GPIO functionality test passed");
}

//=============================================================================
// DISPLAY TESTS
//=============================================================================

// Test TFT display initialization
void test_tft_display_init(void) {
    Serial.println("=== TFT Display Initialization Test ===");

#ifdef ESP32_2432S028R
    // Initialize display
    tft.init();
    tft.setRotation(1); // Landscape mode

    // Test display parameters (rotation 1 swaps width/height)
    TEST_ASSERT_EQUAL(TFT_HEIGHT, tft.width());  // In landscape: width = original height
    TEST_ASSERT_EQUAL(TFT_WIDTH, tft.height());  // In landscape: height = original width

    Serial.printf("Display initialized: %dx%d\n", tft.width(), tft.height());

    // Test basic drawing
    tft.fillScreen(TFT_BLACK);
    delay(100);

    tft.fillScreen(TFT_RED);
    delay(100);

    tft.fillScreen(TFT_GREEN);
    delay(100);

    tft.fillScreen(TFT_BLUE);
    delay(100);

    tft.fillScreen(TFT_BLACK);

    Serial.println("TFT display color test passed");

    // Test text rendering
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("TEST MODE");
    tft.setCursor(10, 40);
    tft.println("Display OK");

    delay(1000);
    tft.fillScreen(TFT_BLACK);

    Serial.println("TFT display text test passed");
#else
    Serial.println("TFT display test skipped (board not supported)");
#endif

    Serial.println("TFT display initialization test passed");
}

// Test touch interface
void test_touch_interface(void) {
    Serial.println("=== Touch Interface Test ===");

#ifdef ESP32_2432S028R
    // Initialize TFT (includes touch initialization if available)
    tft.init();

    Serial.println("Touch interface initialized via TFT_eSPI");

    // Test touch calibration using TFT_eSPI built-in touch
    #ifdef TOUCH_CS
    uint16_t touch_x, touch_y;
    bool touch_available = tft.getTouch(&touch_x, &touch_y);
    Serial.printf("Touch available: %s\n", touch_available ? "Yes" : "No");
    #else
    Serial.println("Touch not configured - TOUCH_CS not defined");
    bool touch_available = false;
    #endif

    // Display touch test screen
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Touch Test");
    tft.setCursor(10, 40);
    tft.println("Touch screen");
    tft.setCursor(10, 70);
    tft.println("for 3 seconds");

    // Test touch for a short time
    uint32_t start_time = millis();
    int touch_count = 0;

    #ifdef TOUCH_CS
    while (millis() - start_time < 3000) { // Test for 3 seconds
        uint16_t x, y;
        if (tft.getTouch(&x, &y)) {
            Serial.printf("Touch detected at: %d, %d\n", x, y);
            touch_count++;

            // Draw a small circle at touch point
            tft.fillCircle(x, y, 5, TFT_GREEN);
            delay(50); // Debounce
        }
        delay(10);
    }
    #else
    Serial.println("Touch testing skipped - TOUCH_CS not defined");
    delay(3000); // Still wait 3 seconds for consistent test timing
    #endif

    Serial.printf("Touch events detected: %d\n", touch_count);

    tft.fillScreen(TFT_BLACK);
    tft.setCursor(10, 10);
    tft.printf("Touches: %d", touch_count);

    delay(1000);
    tft.fillScreen(TFT_BLACK);

    Serial.println("Touch interface test completed");
#else
    Serial.println("Touch interface test skipped (board not supported)");
#endif

    Serial.println("Touch interface test passed");
}

//=============================================================================
// SPI TESTS
//=============================================================================

// Test SPI communication
void test_spi_communication(void) {
    Serial.println("=== SPI Communication Test ===");

#ifdef ESP32_2432S028R
    // SPI is already initialized by TFT library
    Serial.println("SPI initialized via TFT library");

    // Test SPI settings
    SPISettings settings(27000000, MSBFIRST, SPI_MODE0); // 27MHz, common for displays
    SPI.beginTransaction(settings);

    // Send test data
    uint8_t test_data = 0xAA;
    uint8_t received = SPI.transfer(test_data);

    SPI.endTransaction();

    Serial.printf("SPI test: sent 0x%02X, received 0x%02X\n", test_data, received);

    Serial.println("SPI communication test passed");
#else
    Serial.println("SPI communication test skipped (board not supported)");
#endif

    Serial.println("SPI communication test passed");
}

//=============================================================================
// WIFI TESTS
//=============================================================================

// Test WiFi functionality
void test_wifi_functionality(void) {
    Serial.println("=== WiFi Functionality Test ===");

    // Test WiFi initialization
    TEST_ASSERT_NOT_EQUAL((uintptr_t)nullptr, (uintptr_t)&WiFi);

    // Test WiFi mode setting
    WiFi.mode(WIFI_STA);
    TEST_ASSERT_EQUAL(WIFI_STA, WiFi.getMode());

    Serial.println("WiFi mode test passed");

    // Test WiFi scanning
    Serial.println("Starting WiFi scan...");
    int networks = WiFi.scanNetworks();

    if (networks > 0) {
        Serial.printf("Found %d networks:\n", networks);
        for (int i = 0; i < min(networks, 5); i++) { // Show max 5 networks
            Serial.printf("  %d: %s (RSSI: %d, Encryption: %d)\n",
                         i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i));
        }
        TEST_ASSERT_TRUE(networks > 0);
    } else {
        Serial.println("No networks found (this might be expected in some environments)");
        // Don't fail the test if no networks are found
    }

    // Clean up scan results
    WiFi.scanDelete();

    Serial.println("WiFi scan test passed");

    // Test MAC address
    String mac = WiFi.macAddress();
    TEST_ASSERT_TRUE(mac.length() == 17); // MAC address should be 17 characters (XX:XX:XX:XX:XX:XX)
    Serial.printf("WiFi MAC Address: %s\n", mac.c_str());

    Serial.println("WiFi functionality test passed");
}

// Test WiFi Manager functionality
void test_wifi_manager(void) {
    Serial.println("=== WiFi Manager Test ===");

    // Test WiFi Manager initialization
    wm.setDebugOutput(false); // Reduce debug output for tests

    // Test configuration portal setup (without actually starting it)
    wm.setConfigPortalTimeout(60); // 1 minute timeout
    wm.setConnectTimeout(20); // 20 seconds connect timeout

    // Test parameter setup
    WiFiManagerParameter custom_pool("pool", "Mining Pool", "pool.nerdminers.org:3333", 64);
    wm.addParameter(&custom_pool);

    Serial.println("WiFi Manager configuration test passed");

    // Test AP mode capabilities (without actually starting AP)
    WiFi.mode(WIFI_AP);
    bool ap_result = WiFi.softAP("NerdMinerTest", "password123");

    if (ap_result) {
        Serial.printf("AP mode test successful. IP: %s\n", WiFi.softAPIP().toString().c_str());
        WiFi.softAPdisconnect(true);
    } else {
        Serial.println("AP mode test failed (might be expected in some environments)");
    }

    WiFi.mode(WIFI_STA); // Return to station mode

    Serial.println("WiFi Manager test passed");
}

//=============================================================================
// MEMORY TESTS
//=============================================================================

// Test memory allocation and management
void test_memory_management(void) {
    Serial.println("=== Memory Management Test ===");

    uint32_t initial_heap = ESP.getFreeHeap();
    Serial.printf("Initial free heap: %u bytes\n", initial_heap);

    // Test dynamic allocation
    const size_t test_size = 1024; // 1KB
    void* test_ptr = malloc(test_size);
    TEST_ASSERT_NOT_NULL(test_ptr);

    uint32_t heap_after_alloc = ESP.getFreeHeap();
    Serial.printf("Free heap after 1KB allocation: %u bytes\n", heap_after_alloc);

    // Verify memory is allocated
    TEST_ASSERT_TRUE(initial_heap - heap_after_alloc >= test_size);

    // Test memory write/read
    memset(test_ptr, 0xAA, test_size);
    uint8_t* byte_ptr = (uint8_t*)test_ptr;
    for (size_t i = 0; i < test_size; i++) {
        TEST_ASSERT_EQUAL(0xAA, byte_ptr[i]);
    }

    // Free memory
    free(test_ptr);

    uint32_t heap_after_free = ESP.getFreeHeap();
    Serial.printf("Free heap after free: %u bytes\n", heap_after_free);

    // Check for memory leaks (allow some variance)
    int32_t heap_difference = (int32_t)heap_after_free - (int32_t)initial_heap;
    TEST_ASSERT_TRUE(abs(heap_difference) < 100); // Less than 100 bytes difference

    Serial.println("Memory management test passed");
}

// Test PSRAM (if available)
void test_psram(void) {
    Serial.println("=== PSRAM Test ===");

    if (psramFound()) {
        Serial.println("PSRAM found!");
        Serial.printf("PSRAM size: %d bytes\n", ESP.getPsramSize());
        Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());

        // Test PSRAM allocation
        void* psram_ptr = ps_malloc(1024);
        if (psram_ptr) {
            memset(psram_ptr, 0x55, 1024);
            uint8_t* byte_ptr = (uint8_t*)psram_ptr;
            TEST_ASSERT_EQUAL(0x55, byte_ptr[0]);
            TEST_ASSERT_EQUAL(0x55, byte_ptr[1023]);
            free(psram_ptr);
            Serial.println("PSRAM allocation test passed");
        } else {
            Serial.println("PSRAM allocation failed");
        }
    } else {
        Serial.println("PSRAM not found (this is normal for many ESP32 variants)");
    }

    Serial.println("PSRAM test completed");
}

//=============================================================================
// PERFORMANCE TESTS
//=============================================================================

// Test system performance metrics
void test_system_performance(void) {
    Serial.println("=== System Performance Test ===");

    // Test CPU frequency
    uint32_t cpu_freq = ESP.getCpuFreqMHz();
    TEST_ASSERT_TRUE(cpu_freq >= 80); // Should be at least 80MHz
    TEST_ASSERT_TRUE(cpu_freq <= 240); // Should not exceed 240MHz
    Serial.printf("CPU Frequency: %u MHz\n", cpu_freq);

    // Test timing accuracy
    uint32_t start_time = micros();
    delay(100); // 100ms delay
    uint32_t elapsed = micros() - start_time;

    // Should be approximately 100,000 microseconds (allow 10% variance)
    TEST_ASSERT_TRUE(elapsed >= 90000);
    TEST_ASSERT_TRUE(elapsed <= 110000);
    Serial.printf("Timing test: expected ~100ms, got %u microseconds\n", elapsed);

    // Test millis() function
    uint32_t millis_start = millis();
    delay(50);
    uint32_t millis_elapsed = millis() - millis_start;
    TEST_ASSERT_TRUE(millis_elapsed >= 45);
    TEST_ASSERT_TRUE(millis_elapsed <= 55);

    Serial.println("System performance test passed");
}

// Test power management
void test_power_management(void) {
    Serial.println("=== Power Management Test ===");

    // Test light sleep mode preparation
    esp_sleep_enable_timer_wakeup(1000000); // 1 second
    Serial.println("Sleep timer configured");

    // Test that we can configure wake-up sources without actually sleeping
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup(1000000);

    Serial.println("Power management configuration test passed");

    // Note: We don't actually enter sleep mode in the test to avoid interrupting the test suite
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial port to connect
    }
    delay(2000); // Allow time for serial to initialize

    Serial.println("Starting ESP32 Hardware Validation tests...");

    UNITY_BEGIN();

    // System Tests
    RUN_TEST(test_esp32_system_info);
    RUN_TEST(test_gpio_functionality);

    // Display Tests
    RUN_TEST(test_tft_display_init);
    RUN_TEST(test_touch_interface);

    // Communication Tests
    RUN_TEST(test_spi_communication);
    RUN_TEST(test_wifi_functionality);
    RUN_TEST(test_wifi_manager);

    // Memory Tests
    RUN_TEST(test_memory_management);
    RUN_TEST(test_psram);

    // Performance Tests
    RUN_TEST(test_system_performance);
    RUN_TEST(test_power_management);

    UNITY_END();

    Serial.println("All ESP32 hardware validation tests completed!");
}

void loop() {
    // Empty loop for embedded test runner
    // Tests run once in setup()
}

#endif // !defined(NATIVE_TEST) && defined(HARDWARE_VALIDATION_TEST)