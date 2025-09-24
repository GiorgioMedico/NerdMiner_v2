# NerdMiner_v2 Testing Framework

This directory contains the testing infrastructure for the NerdMiner_v2 Bitcoin solo mining device. The testing framework is designed to validate both algorithmic logic and hardware functionality across 30+ supported ESP32 board variants.

## Test Architecture

The testing system uses a **dual-environment approach**:

- **Native Tests** (`native_test` environment) - Run on development computer for rapid algorithm validation
- **Embedded Tests** (device-specific environments like `ESP32-2432S028R-test`) - Run on actual ESP32 hardware

This approach enables Test-Driven Development (TDD) workflows while ensuring real-world hardware compatibility.

## Test Files Overview

### Core Test Files

#### `test_utils.h` / `test_utils.cpp`
Common testing utilities and helper functions shared across all tests:

- **Hex Conversion**: `hex_string_to_bytes()`, `bytes_to_hex_string()`
- **Memory Comparison**: `assert_bytes_equal()`, `print_bytes_hex()`
- **Bitcoin Validation**: `validate_sha256_hash()`, `validate_bitcoin_difficulty_target()`

**Usage Example:**
```cpp
uint8_t hash[32];
hex_string_to_bytes("a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3", hash, 32);
TEST_ASSERT_TRUE(validate_sha256_hash(hash));
```

#### `test_native_all.cpp`
Comprehensive native tests for core algorithms and protocol logic:

- **SHA256 Algorithm Tests**: Software implementation validation with known test vectors
- **Bitcoin Protocol Tests**: Block header structure, difficulty calculations
- **Mining Logic Tests**: Nonce validation, difficulty targets, mining constants
- **Endian Conversion Tests**: Byte order utilities for Bitcoin protocol compliance

**Test Coverage:**
- 16 test cases covering SHA256, mining data structures, and protocol validation
- Uses mock implementations for hardware-dependent functions
- Validates against standard Bitcoin test vectors

#### `test_embedded_basic.cpp`
Basic embedded environment validation for ESP32 hardware:

- **Environment Validation**: Arduino/ESP32 function availability
- **Hardware Constants**: Board-specific pin mappings and display parameters
- **System Functions**: Memory, CPU frequency, chip revision validation
- **ESP32 Features**: Hardware-specific functionality verification

**Conditional Compilation:**
```cpp
#ifdef ESP32_2432S028R
TEST_ASSERT_EQUAL(240, TFT_WIDTH);   // 2.8" display width
TEST_ASSERT_EQUAL(320, TFT_HEIGHT);  // 2.8" display height
#endif
```

#### `test_validation_functions.cpp`
Specialized validation functions for mining and protocol data:

- **Mining Job Validation**: Stratum protocol job structure verification
- **Block Header Validation**: Bitcoin block header format checking
- **Nonce Range Validation**: Mining nonce boundary validation
- **Protocol Data Validation**: Comprehensive Bitcoin protocol compliance

## Test Environments

### Native Testing Environment

**Configuration:** `[env:native_test]` in `platformio.ini`

```ini
platform = native
test_framework = unity
test_ignore = test/embedded
build_flags =
    -D NATIVE_TEST=1
    -D MAX_NONCE_STEP=5000000U
    -D MAX_NONCE=25000000U
    -D DEFAULT_DIFFICULTY=0.00015
```

**Purpose:**
- Fast execution on development computer
- Algorithm and logic validation
- No hardware dependencies
- Ideal for TDD workflows

### Embedded Testing Environment

**Example Configuration:** `[env:ESP32-2432S028R-test]` in `platformio.ini`

```ini
platform = espressif32@6.6.0
board = esp32dev
framework = arduino
test_framework = unity
test_ignore = test/native
build_flags =
    -D ESP32_2432S028R=1
    -D UNIT_TEST=1
    -D TFT_WIDTH=240
    -D TFT_HEIGHT=320
```

**Purpose:**
- Real hardware validation
- Hardware-specific functionality testing
- Performance and integration verification
- Board-specific feature validation

## Usage Instructions

### Running Tests

#### Native Tests (Computer-based)
```bash
# Run all native tests
pio test -e native_test

# Run specific native test
pio test -e native_test -f "test_sha256"

# Verbose output
pio test -e native_test -v
```

#### Embedded Tests (Hardware-based)
```bash
# Run all ESP32 tests
pio test -e ESP32-2432S028R-test

# Upload and run on specific device
pio test -e ESP32-2432S028R-test --upload-port /dev/ttyUSB0

# Run specific embedded test
pio test -e ESP32-2432S028R-test -f "test_display"
```

#### Combined Testing
```bash
# Run all tests (native + embedded)
pio test

# Generate JSON test report
pio test --json-output-path test_results.json
```

### Test Results Interpretation

**Test Status Indicators:**
- ✅ `PASSED` - Test completed successfully
- ❌ `FAILED` - Test assertion failed
- ⚠️ `SKIPPED` - Test environment not available/applicable
- 🔄 `ERROR` - Test execution error

**Example Results:**
```
Test Results Summary:
- Native Tests: 16/16 passed
- Embedded Tests: Skipped (no hardware)
- Total Duration: 0.52s
```

## Test Dependencies

### Unity Testing Framework
The project uses Unity C testing framework for all test environments:

```cpp
#include <unity.h>

void setUp(void) {
    // Common setup for each test
}

void tearDown(void) {
    // Common cleanup after each test
}
```

### Native Dependencies
- **ArduinoJson**: JSON parsing for Stratum protocol tests
- **Standard Libraries**: String manipulation, memory operations

### Embedded Dependencies
- **Arduino Framework**: ESP32 Arduino core functionality
- **ESP32 Libraries**: Hardware-specific drivers and utilities
- **Board Libraries**: Display, touch, and peripheral drivers

## Adding New Tests

### Creating Native Tests

1. Add test functions to `test_native_all.cpp`:
```cpp
void test_new_algorithm(void) {
    // Test implementation
    uint8_t expected[32] = {0x01, 0x02, ...};
    uint8_t actual[32];

    my_new_algorithm(input, actual);
    assert_bytes_equal(expected, actual, 32, "Algorithm output mismatch");
}
```

2. Register in main function:
```cpp
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_new_algorithm);
    return UNITY_END();
}
```

### Creating Embedded Tests

1. Add test functions to `test_embedded_basic.cpp`:
```cpp
void test_hardware_feature(void) {
    // Hardware-specific test
    TEST_ASSERT_TRUE(digitalRead(LED_PIN) == HIGH);
}
```

2. Add board-specific conditionals:
```cpp
#ifdef ESP32_2432S028R
    TEST_ASSERT_EQUAL(21, TFT_BL);  // Backlight pin
#endif
```

### Test Utilities

Use common utilities from `test_utils.h`:
```cpp
// Hex string conversion
hex_string_to_bytes("deadbeef", bytes, 4);

// Memory comparison with helpful errors
assert_bytes_equal(expected, actual, length, "Context message");

// Debug output
print_bytes_hex(data, length, "Debug Label");
```

## Test Coverage Areas

### Current Coverage
- ✅ **SHA256 Algorithm**: Software implementation with test vectors
- ✅ **Bitcoin Protocol**: Block headers, difficulty calculation
- ✅ **Mining Logic**: Nonce validation, mining constants
- ✅ **ESP32 Environment**: Basic hardware functionality
- ✅ **Utility Functions**: Hex conversion, byte operations

### Planned Coverage (see `../test_plan.txt`)
- 🔄 **Stratum Protocol**: Complete mining protocol testing
- 🔄 **Display System**: TFT display and touch functionality
- 🔄 **WiFi Management**: Connection and configuration testing
- 🔄 **Storage System**: NVS and SD card operations
- 🔄 **Performance**: Benchmarking and optimization validation

## Troubleshooting

### Common Issues

#### Native Tests Failing
```
Error: undefined reference to 'Arduino'
```
**Solution:** Ensure test files use `#ifdef NATIVE_TEST` guards around Arduino-specific code.

#### Embedded Tests Not Running
```
Error: Could not find a board ID
```
**Solution:** Specify correct upload port: `pio test -e ESP32-2432S028R-test --upload-port /dev/ttyUSB0`

#### Missing Test Fixtures
```
Error: 'fixtures/sha256_test_vectors.h' file not found
```
**Solution:** Test fixtures are referenced but not yet implemented. Tests use inline test vectors as temporary solution.

### Debug Tips

1. **Verbose Output**: Add `-v` flag to see detailed test execution
2. **Specific Tests**: Use `-f "test_name"` to run individual tests
3. **Serial Monitor**: For embedded tests, use `pio device monitor` after upload
4. **Build Issues**: Clean and rebuild with `pio run -t clean && pio test`

## Development Workflow

### Recommended Testing Approach

1. **Write Native Tests First**: Validate algorithm logic quickly
2. **Implement Feature**: Add actual functionality
3. **Add Embedded Tests**: Validate hardware integration
4. **Performance Testing**: Benchmark on target hardware
5. **Integration Testing**: Test complete workflows

### Continuous Integration

The testing framework supports CI/CD pipelines:

```yaml
# Example CI configuration
- name: Run Native Tests
  run: pio test -e native_test

- name: Generate Test Report
  run: pio test --json-output-path ci_test_results.json
```

## Test Data and Constants

### Mining Constants
```cpp
#define MAX_NONCE_STEP   5000000U      // Nonce increment per iteration
#define MAX_NONCE        25000000U     // Maximum nonce value
#define DEFAULT_DIFFICULTY 0.00015     // Default mining difficulty
#define TARGET_NONCE     471136297U    // Test target nonce
```

### Test Vectors
The testing framework includes standard Bitcoin test vectors for:
- SHA256 algorithm validation ("", "abc", "message digest")
- Double SHA256 verification
- Block header structure validation
- Difficulty target calculations

---

## Contributing

When adding new tests:

1. Follow the existing file organization (native vs embedded)
2. Use descriptive test function names (`test_feature_specific_case`)
3. Include helpful assertion messages
4. Add documentation for complex test logic
5. Update this README for significant additions

For questions about the testing framework, refer to the main project documentation or the comprehensive test plan in `../test_plan.txt`.