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

This project uses **PlatformIO** as the build system:

### Primary Build Commands
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

### Serial Monitoring
```bash
pio device monitor --baud 115200
```

### Debug Flags
Add to build_flags in platformio.ini:
- `-D DEBUG_MINING=1` - Mining debug output
- `-D DEBUG_MEMORY=1` - Memory usage tracking

### Hardware Testing
- SHA256 hardware test available in `src/ShaTests/nerdSHA_HWTest.cpp`
- Button testing via `OneButton` library integration
- Display testing through screen cycling functionality

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