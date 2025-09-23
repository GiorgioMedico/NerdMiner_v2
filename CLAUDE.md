# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**NerdMiner_v2** is an ESP32-based Bitcoin solo mining device implementing the Stratum protocol. The project supports 30+ different ESP32 board variants including ESP32-S3, ESP32-C3, ESP32-WROOM, and various development boards with displays (TTGO T-Display, M5Stack, AMOLED, OLED, etc.).

### Key Features
- Multi-core/multi-threaded Bitcoin mining using hardware SHA256 acceleration
- Stratum protocol implementation for solo pool mining
- WiFi configuration management via captive portal
- Multiple display interfaces with custom screens (NerdMiner, ClockMiner, GlobalStats)
- SD card configuration support
- Supports both hardware and software SHA256 implementations

## Build System Commands

This project uses **PlatformIO** as the build system. Here's a comprehensive reference for all PlatformIO capabilities:

### Core Build Commands
```bash
# Build for specific board (30+ environments available)
pio run -e [environment_name]

# Upload firmware to device
pio run -e [environment_name] -t upload

# Monitor serial output
pio device monitor

# Clean build files
pio run -t clean

# List all available environments
pio project config
```

### Advanced Build Options
```bash
# Build with verbose output
pio run -v

# Build multiple environments in parallel
pio run -e NerdminerV2 -e ESP32-devKitv1

# Upload with custom port
pio run -e [environment_name] -t upload --upload-port /dev/ttyUSB0

# Build and upload then monitor
pio run -e [environment_name] -t upload -t monitor

# Control parallel build jobs (useful for faster builds)
pio run -j 4

# List all available targets for current project
pio run --list-targets

# Build with specific build flags
pio run -e [environment_name] --project-option "build_flags=-DDEBUG_MINING=1"
```

### Project Management Commands
```bash
# Initialize new project
pio project init --board esp32dev

# Initialize with multiple boards
pio project init --board esp32dev --board nodemcuv2

# Generate project metadata
pio project metadata

# Validate project configuration
pio project config --environment [env_name]

# Show project structure and settings
pio project data
```

### Package & Library Management
```bash
# Install project dependencies (replaces old pio lib commands)
pio pkg install

# Install specific library
pio pkg install --library "bblanchon/ArduinoJson@^6.21.5"

# Install development platform
pio pkg install --platform "espressif32@^6.6.0"

# Search for libraries
pio pkg search "ArduinoJson"

# List installed packages
pio pkg list

# Update all packages
pio pkg update

# Uninstall package
pio pkg uninstall --library "ArduinoJson"

# Show package information
pio pkg show "bblanchon/ArduinoJson"

# List outdated packages
pio pkg outdated
```

### Device Management & Monitoring
```bash
# List all connected devices
pio device list

# Monitor with specific baud rate
pio device monitor --baud 115200

# Monitor with filters (useful for debugging)
pio device monitor --filter esp32_exception_decoder --filter time

# Monitor specific port
pio device monitor --port /dev/ttyUSB0

# Monitor with custom encoding
pio device monitor --encoding utf-8

# Raw monitor without filters
pio device monitor --raw
```

### Platform Management
```bash
# List installed platforms
pio platform list

# List available platforms
pio platform search

# Install platform
pio platform install espressif32

# Update platform
pio platform update espressif32

# Show platform information
pio platform show espressif32

# List platform frameworks
pio platform frameworks espressif32

# Uninstall platform
pio platform uninstall espressif32
```

### Key Board Environments
- `NerdminerV2` - Main TTGO T-Display S3 board
- `ESP32-devKitv1` - Standard ESP32 development board
- `TTGO-T-Display` - TTGO T-Display v1
- `M5Stick-C`, `M5Stick-CPlus`, `M5Stick-C-Plus2` - M5Stack variants
- `ESP32-2432S028R` - 2.8" touch display board
- `esp32cam` - ESP32-CAM module
- `ESP32-C3-super-mini` - Compact ESP32-C3 board

### Build Configuration
All boards use `huge_app.csv` partition scheme except where specified. The build system includes:
- Automatic firmware versioning via `auto_firmware_version.py`
- Post-build binary merging via `post_build_merge.py`
- Board-specific compilation flags and pin mappings

## Architecture & Code Structure

### Core Mining Components
- **`src/mining.cpp`** - Core mining logic with hardware/software SHA256, nonce calculation, and work distribution
- **`src/stratum.cpp`** - Complete Stratum protocol implementation (subscribe, authorize, notify, submit)
- **`src/monitor.cpp`** - Mining performance monitoring, statistics tracking, and system health

### Hardware Abstraction Layer
- **`src/drivers/devices/`** - Board-specific configurations (pin mappings, hardware features)
- **`src/drivers/displays/`** - Display driver implementations for various screen types
- **`src/drivers/storage/`** - Storage management (NVS, SD card configuration)

### System Management
- **`src/wManager.cpp`** - WiFi Manager implementation with captive portal for configuration
- **`src/utils.cpp`** - Utility functions for time, string manipulation, and system operations
- **`src/NerdMinerV2.ino.cpp`** - Main application entry point with task management and button handling

### Key Data Structures
```cpp
typedef struct {
    uint8_t bytearray_target[32];
    uint8_t bytearray_pooltarget[32];
    uint8_t merkle_result[32];
    uint8_t bytearray_blockheader[128];
} miner_data;

typedef struct {
    String job_id;
    String prev_block_hash;
    String coinb1, coinb2;
    String nbits;
    JsonArray merkle_branch;
    String version;
    uint32_t target;
    String ntime;
    bool clean_jobs;
} mining_job;
```

## Device Support System

### Board Selection
Each board environment in `platformio.ini` defines:
- **Build flags** - Hardware-specific definitions (`-D NERDMINERV2=1`, `-D ESP32_CAM`, etc.)
- **Pin mappings** - Button pins, LED pins, display pins
- **Library dependencies** - Board-specific libraries (TFT_eSPI, M5Stack, etc.)
- **Partition scheme** - Memory layout configuration

### Display System
The display system uses a driver pattern:
1. Board-specific device headers define hardware capabilities
2. Display drivers implement screen-specific rendering
3. Common display interface handles screen switching and animations
4. Three main screen types: NerdMiner (mining stats), ClockMiner (time display), GlobalStats (network stats)

### Configuration Management
- **Runtime Config**: WiFi Manager captive portal (SSID: NerdMinerAP, Pass: MineYourCoins)
- **SD Card Config**: JSON configuration file support for batch deployment
- **NVS Storage**: Persistent settings storage in ESP32 flash

## Development Workflow

### Adding New Board Support
1. Create device header in `src/drivers/devices/[board_name].h`
2. Add environment in `platformio.ini` with appropriate build flags
3. Create display driver if needed in `src/drivers/displays/`
4. Test with `pio run -e [new_environment]`

### Mining Algorithm Development
- Hardware SHA256 is preferred when available (`HARDWARE_SHA265` flag)
- Software fallback in `src/ShaTests/` for compatibility
- Core mining loop in `runMiner()` function handles work distribution
- Nonce range: 0 to 25,000,000 with 5,000,000 step increments

### Key Constants
```cpp
#define MAX_NONCE_STEP  5000000U
#define MAX_NONCE       25000000U
#define DEFAULT_DIFFICULTY  0.00015
#define WDT_TIMEOUT 3              // 3 second watchdog
#define WDT_MINER_TIMEOUT 900      // 15 minute miner watchdog
```

## Testing & Debugging

### Unit Testing
```bash
# Run all tests
pio test

# Run tests for specific environment
pio test -e NerdminerV2

# Run specific test
pio test -f "test_sha256"

# Run tests with verbose output
pio test -v

# Upload and run tests on device
pio test --upload-port /dev/ttyUSB0

# Run tests without uploading (embedded tests)
pio test --without-uploading

# Generate test report
pio test --json-output-path test_results.json
```

### Debugging Commands
```bash
# Start debugging session
pio debug

# Debug specific environment
pio debug -e NerdminerV2

# Debug with specific interface
pio debug --interface=gdb

# Launch debugging server only
pio debug --gdb-server

# Debug with custom configuration
pio debug --project-option "debug_tool=esp-prog"
```

### Static Code Analysis
```bash
# Run static code analysis
pio check

# Check specific environment
pio check -e NerdminerV2

# Check specific files/directories
pio check --src-filters="+<src/mining.cpp>"

# Use specific analysis tool
pio check --tool cppcheck

# Generate analysis report
pio check --json-output-path analysis_report.json

# Check with high severity only
pio check --severity=high

# Verbose analysis output
pio check -v
```

### Serial Monitoring (Enhanced)
```bash
# Basic monitoring
pio device monitor --baud 115200

# Monitor with exception decoder (ESP32 specific)
pio device monitor --filter esp32_exception_decoder

# Monitor with multiple filters
pio device monitor --filter time --filter colorize --filter log2file

# Monitor with custom line endings
pio device monitor --eol=CRLF

# Monitor and echo input
pio device monitor --echo

# Monitor with quiet output
pio device monitor --quiet

# Monitor with RTS/DTR control
pio device monitor --rts=0 --dtr=0
```

### Debug Flags & Build Options
Add to build_flags in platformio.ini:
- `-D DEBUG_MINING=1` - Mining debug output
- `-D DEBUG_MEMORY=1` - Memory usage tracking
- `-D CORE_DEBUG_LEVEL=5` - ESP32 core debug level (0-5)
- `-D CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL=5` - Arduino HAL log level
- `-D MONITOR_SPEED=115200` - Serial monitor speed

### Advanced Debugging Features
```bash
# Memory analysis and inspection (via PlatformIO Home)
pio home

# Firmware size analysis
pio run -t size

# Generate memory map
pio run -t size-data

# Check stack usage
pio run -t check-stack

# Profile memory usage
pio run -t meminfo
```

### Hardware Testing
- SHA256 hardware test available in `src/ShaTests/nerdSHA_HWTest.cpp`
- Button testing via `OneButton` library integration
- Display testing through screen cycling functionality
- Use `pio test` to run automated hardware validation tests

## Pool Configuration

### Supported Pools
- **public-pool.io:21496** - Primary supported pool
- **pool.nerdminers.org:3333** - Official NerdMiner pool
- Custom pools via configuration interface

### Stratum Implementation
- Full Stratum v1 protocol support
- Automatic difficulty adjustment
- Keep-alive mechanism (30s intervals)
- Clean jobs handling for optimal mining efficiency

## Additional PlatformIO Commands

### System & Maintenance Commands
```bash
# Update PlatformIO Core
pio upgrade

# Update all platforms and packages
pio update

# Show PlatformIO system information
pio system info

# Prune cached data
pio system prune

# Clean PlatformIO cache
pio system prune --cache

# Reset PlatformIO settings
pio settings reset

# Show current settings
pio settings get

# Set global setting
pio settings set check_platformio_interval 1000

# Enable/disable telemetry
pio settings set enable_telemetry false
```

### Remote Development (PlatformIO Cloud)
```bash
# List remote agents
pio remote agent list

# Start remote agent
pio remote agent start

# Remote run (build on cloud)
pio remote run -e NerdminerV2

# Remote device monitoring
pio remote device monitor

# Remote testing
pio remote test -e NerdminerV2
```

### Project Inspection & Analysis
```bash
# Launch PlatformIO Home for project inspection
pio home

# Generate project metadata for IDEs
pio project metadata --json-output

# Show project data and structure
pio project data

# Validate project configuration
pio project config

# Check project dependencies
pio pkg outdated

# Generate compilation database (for IDEs)
pio run -t compiledb

# Generate IDE project files
pio project init --ide vscode
pio project init --ide clion
pio project init --ide eclipse
```

### Continuous Integration Commands
```bash
# Install dependencies only
pio pkg install

# Run complete CI pipeline
pio run && pio test && pio check

# Build all environments
pio run

# Generate build artifacts with metadata
pio run --json-output build_output.json

# Upload firmware with verification
pio run -t upload -t verify

# Create release build
pio run -e NerdminerV2 --project-option "build_type=release"
```

### Troubleshooting & Diagnostics
```bash
# Force reinstall of packages
pio pkg install --force

# Rebuild from scratch
pio run -t clean && pio run

# Check for configuration issues
pio project config --json-output

# Verify toolchain installation
pio platform show espressif32

# Debug package dependencies
pio pkg list --only=dependencies

# Show detailed build information
pio run -v --dry-run

# Force update of package index
pio update --dry-run
```

### Environment-Specific Operations for NerdMiner
```bash
# Quick build and flash for main board
pio run -e NerdminerV2 -t upload

# Test different board variants
pio run -e M5Stick-C -e ESP32-devKitv1 -e esp32cam

# Debug mining functionality
pio run -e NerdminerV2 --project-option "build_flags=-DDEBUG_MINING=1" -t upload

# Build with memory optimization
pio run -e NerdminerV2 --project-option "build_flags=-Os -DNDEBUG"

# Monitor multiple devices
pio device monitor --port /dev/ttyUSB0 &
pio device monitor --port /dev/ttyUSB1

# Upload to multiple devices
for env in NerdminerV2 ESP32-devKitv1; do pio run -e $env -t upload; done
```