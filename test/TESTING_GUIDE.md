# NerdMiner_v2 Testing Guide

This guide provides comprehensive instructions for running, developing, and maintaining tests for the ESP32-2432S028R NerdMiner hardware.

## Quick Start

### Running All Tests

```bash
# Run all native tests (computer-based)
pio test -e native_test

# Build and verify embedded tests (ESP32-2432S028R)
pio run -e ESP32-2432S028R-test

# Run specific test file
pio test -e native_test -f "test_sha256*"
```

### Hardware Testing (requires ESP32-2432S028R device)

```bash
# Upload and run hardware tests
pio test -e ESP32-2432S028R-test --upload-port /dev/ttyUSB0

# Monitor test output
pio device monitor --baud 115200 --filter esp32_exception_decoder
```

## Test Environments

### 1. Native Test Environment (`native_test`)

**Purpose**: Fast algorithm validation on development computer
**Location**: Runs on your local machine
**Tests Include**:
- SHA256 algorithm validation
- Bitcoin protocol parsing
- Stratum protocol message handling
- Mining workflow logic
- JSON parsing and validation

**Commands**:
```bash
# Run all native tests
pio test -e native_test --verbose

# Run specific test categories
pio test -e native_test -f "*sha256*"     # SHA256 tests
pio test -e native_test -f "*stratum*"    # Stratum protocol tests
pio test -e native_test -f "*mining*"     # Mining integration tests

# Generate test report
pio test -e native_test --json-output-path test_results.json
```

### 2. ESP32-2432S028R Test Environment (`ESP32-2432S028R-test`)

**Purpose**: Hardware validation on actual ESP32 device
**Hardware Required**: ESP32-2432S028R board with 2.8" TFT display
**Tests Include**:
- GPIO control (LEDs, buttons)
- Display interface (TFT, touch)
- SPI communication (SD card)
- WiFi functionality
- Hardware SHA256 acceleration
- Performance benchmarking
- Memory management

**Commands**:
```bash
# Build test firmware
pio run -e ESP32-2432S028R-test

# Upload and run tests (replace /dev/ttyUSB0 with your port)
pio test -e ESP32-2432S028R-test --upload-port /dev/ttyUSB0

# Monitor test execution
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

## Test Categories

### Algorithm Tests (`test_native_all.cpp`)

**Coverage**: Core mining algorithms and Bitcoin protocol
**Run Time**: ~5 seconds
**Key Tests**:
- SHA256 with NIST test vectors
- Bitcoin block header validation
- Mining data structure verification
- Endian conversion utilities

```bash
# Run only algorithm tests
pio test -e native_test -f "test_native_all"
```

### Stratum Protocol Tests (`test_stratum_protocol.cpp`)

**Coverage**: Network protocol communication
**Run Time**: ~3 seconds
**Key Tests**:
- JSON message parsing/generation
- Mining job validation
- Error handling
- Message size limits

```bash
# Test Stratum protocol
pio test -e native_test -f "*stratum*"
```

### Hardware Interface Tests (`test_esp32_2432s028r_hardware.cpp`)

**Coverage**: ESP32-2432S028R specific hardware
**Run Time**: ~30 seconds
**Requirements**: ESP32-2432S028R board
**Key Tests**:
- TFT display initialization
- Touch interface calibration
- LED and button control
- SPI and SD card interface
- Memory allocation
- System performance

```bash
# Run hardware tests (requires device)
pio test -e ESP32-2432S028R-test -f "*hardware*"
```

### WiFi Connectivity Tests (`test_wifi_connectivity.cpp`)

**Coverage**: Network connectivity and WiFi Manager
**Run Time**: ~45 seconds
**Requirements**: ESP32 device (some tests work without network)
**Key Tests**:
- WiFi scanning and AP mode
- WiFi Manager configuration
- Network connectivity (if available)
- Configuration storage

```bash
# Run WiFi tests (requires ESP32)
pio test -e ESP32-2432S028R-test -f "*wifi*"
```

### Hardware SHA256 Tests (`test_hardware_sha256.cpp`)

**Coverage**: ESP32 hardware crypto acceleration
**Run Time**: ~20 seconds
**Requirements**: ESP32 device
**Key Tests**:
- Hardware vs software SHA256 comparison
- Performance benchmarking
- Memory usage validation
- Concurrent operation testing

```bash
# Run SHA256 hardware tests
pio test -e ESP32-2432S028R-test -f "*sha256*"
```

### Mining Integration Tests (`test_mining_integration.cpp`)

**Coverage**: End-to-end mining workflow
**Run Time**: ~15 seconds
**Key Tests**:
- Complete mining cycle
- Job parsing to solution submission
- Performance measurement
- Error handling

```bash
# Run mining workflow tests
pio test -e native_test -f "*integration*"
pio test -e ESP32-2432S028R-test -f "*integration*"
```

### Performance Benchmark Tests (`test_performance_benchmark.cpp`)

**Coverage**: System performance analysis
**Run Time**: ~60 seconds
**Requirements**: ESP32-2432S028R device
**Key Tests**:
- Hash rate measurement
- Display rendering performance
- Memory bandwidth testing
- WiFi performance
- System stress testing

```bash
# Run performance benchmarks
pio test -e ESP32-2432S028R-test -f "*performance*"
```

## Test Development

### Adding New Tests

1. **Choose the Right Environment**:
   - Use `native_test` for algorithm/logic tests
   - Use `ESP32-2432S028R-test` for hardware-specific tests

2. **Create Test Function**:
```cpp
void test_my_new_feature(void) {
    // Test implementation
    TEST_ASSERT_EQUAL(expected, actual);
}
```

3. **Register Test**:
```cpp
// In setup() for embedded tests
RUN_TEST(test_my_new_feature);

// In main() for native tests
RUN_TEST(test_my_new_feature);
```

### Test Utilities

Use common utilities from `test_utils.h`:

```cpp
// Hex string conversion
hex_string_to_bytes("deadbeef", bytes, 4);
bytes_to_hex_string(bytes, 4, hex_output);

// Memory comparison with error messages
assert_bytes_equal(expected, actual, 32, "Hash mismatch");

// Debug output
print_bytes_hex(data, 32, "Debug Data");

// Bitcoin-specific validation
TEST_ASSERT_TRUE(validate_sha256_hash(hash));
TEST_ASSERT_TRUE(validate_bitcoin_difficulty_target(target));
```

### Test Fixtures

Use existing test data from `fixtures/`:

```cpp
#include "fixtures/sha256_test_vectors.h"
#include "fixtures/mining_test_vectors.h"
#include "fixtures/stratum_test_vectors.h"

// Use predefined test vectors
uint8_t expected[32];
hex_string_to_bytes(SHA256_TV2_EXPECTED, expected, 32);
```

## Performance Targets

### ESP32-2432S028R Expected Performance

| Metric | Target | Test |
|--------|--------|------|
| SHA256 Hash Rate | >2000 H/s | Hardware SHA256 tests |
| Memory Bandwidth | >50 MB/s | Memory benchmark |
| Display Frame Rate | >10 FPS | Display rendering |
| Touch Response | >1000 reads/s | Touch interface |
| WiFi Scan Time | <10 seconds | WiFi performance |
| Free Heap | >200KB | Memory management |

### Monitoring Performance

```bash
# Run comprehensive performance suite
pio test -e ESP32-2432S028R-test -f "*benchmark*"

# Monitor specific metrics
pio device monitor --filter colorize
```

## Continuous Integration

### GitHub Actions Integration

Tests run automatically on:
- Push to `main`, `develop`, `prerelease` branches
- Pull requests to `main`, `develop` branches

**Workflow includes**:
1. Native test execution
2. Embedded test compilation
3. Code quality analysis
4. Performance regression check
5. Test result reporting

### Local CI Simulation

```bash
# Run the same tests as CI
pio test -e native_test --verbose
pio run -e ESP32-2432S028R-test
pio check --environment ESP32-2432S028R
```

## Troubleshooting

### Common Issues

#### 1. Native Tests Fail to Compile
```
Error: undefined reference to 'Arduino'
```
**Solution**: Ensure test files use `#ifdef NATIVE_TEST` guards around Arduino-specific code.

#### 2. ESP32 Tests Won't Upload
```
Error: Could not find a board ID
```
**Solution**:
- Check USB connection
- Verify correct port: `pio device list`
- Specify port: `--upload-port /dev/ttyUSB0`

#### 3. Memory Issues in Tests
```
*** HEAP CORRUPTION DETECTED ***
```
**Solution**:
- Check for memory leaks in test code
- Verify proper cleanup in `tearDown()`
- Use memory debugging flags

#### 4. WiFi Tests Fail
```
WiFi scan timeout
```
**Solution**:
- Tests may skip WiFi operations if no networks available
- Check for `TEST_IGNORE_MESSAGE` in output
- Ensure ESP32 has WiFi capability

### Debug Strategies

1. **Verbose Output**:
```bash
pio test -e ESP32-2432S028R-test --verbose
```

2. **Serial Monitor**:
```bash
pio device monitor --baud 115200 --filter esp32_exception_decoder
```

3. **Memory Debugging**:
```cpp
Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
```

4. **Test Isolation**:
```bash
pio test -e native_test -f "test_specific_function"
```

## Best Practices

### 1. Test Organization
- Keep tests focused and atomic
- Use descriptive test names
- Group related tests in same file
- Maintain clear setUp/tearDown

### 2. Error Handling
- Test both success and failure cases
- Use specific assertion messages
- Handle resource cleanup properly
- Test boundary conditions

### 3. Performance Testing
- Use consistent test conditions
- Measure multiple iterations
- Account for system variance
- Document expected ranges

### 4. Hardware Testing
- Handle missing hardware gracefully
- Use timeouts for long operations
- Test with different configurations
- Validate all interface pins

## Test Maintenance

### Regular Tasks

1. **Update Test Vectors**: Keep fixture data current with protocol changes
2. **Performance Baselines**: Update expected performance targets
3. **Coverage Analysis**: Ensure new features have corresponding tests
4. **Documentation**: Keep test documentation synchronized with code

### Release Testing

Before releases, run the complete test suite:

```bash
# Full native test suite
pio test -e native_test

# Hardware validation (if device available)
pio test -e ESP32-2432S028R-test

# Performance verification
pio test -e ESP32-2432S028R-test -f "*benchmark*"

# Build verification
pio run -e ESP32-2432S028R
```

## Support

### Getting Help

1. **Check Test Output**: Look for specific error messages and assertion failures
2. **Review Documentation**: This guide and the main README.md
3. **Check Issues**: GitHub issues for known problems
4. **Debug Locally**: Use serial monitor and verbose output

### Reporting Issues

When reporting test failures, include:
- PlatformIO version and environment
- Complete test output
- Hardware configuration (if applicable)
- Steps to reproduce

The test suite is designed to be comprehensive yet maintainable, providing confidence in the ESP32-2432S028R NerdMiner implementation while enabling rapid development and validation.