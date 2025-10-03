# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

NerdMiner v2 is an ESP32-based Bitcoin mining device that implements the Bitcoin stratum protocol to mine on solo pools. The project supports 40+ different ESP32 board variants with hardware abstraction layers for displays, input devices, and storage.

## Build Commands

This project uses PlatformIO as the build system:

```bash
# Build for specific board (see platformio.ini for all 40+ environments)
pio run -e ESP32-2432S028R


# Build all default environments (will take significant time)
pio run

# Upload to device
pio run -e ESP32-2432S028R -t upload

# Monitor serial output
pio device monitor -p /dev/ttyUSB0 -b 115200

# Test compilation without hardware
pio test
```

## Architecture

### Core Components

**Task Architecture (FreeRTOS)**
- **Monitor Task** (`runMonitor`): Handles display updates, data collection, and screen management
- **Stratum Task** (`runStratumWorker`): Manages pool connection, work distribution, and share submission
- **Miner Tasks** (`minerWorkerHw`/`minerWorkerSw`): Core mining computation on both CPU cores
- **Main Loop**: Button handling, WiFi management, and system coordination

**Key Modules**

1. **Mining Engine** (`src/mining.cpp/.h`)
   - Hardware SHA256 acceleration when available (`HARDWARE_SHA265`)
   - Software fallback for unsupported chips
   - Dual-core mining with work distribution
   - Target/difficulty management

2. **Stratum Protocol** (`src/stratum.cpp/.h`)
   - JSON-RPC Bitcoin stratum client implementation
   - Mining job parsing and work unit creation
   - Share submission and pool communication
   - Automatic reconnection and error handling

3. **Display System** (`src/monitor.cpp/.h`)
   - Multi-screen interface: Mining stats, Clock, Global Bitcoin stats
   - Real-time data fetching from multiple APIs (price, difficulty, blocks)
   - Screen rotation and power management
   - Hardware-specific display drivers in `src/drivers/displays/`

4. **WiFi Management** (`src/wManager.cpp/.h`)
   - Captive portal for initial configuration (NerdMinerAP)
   - Settings persistence to SPIFFS/NVS
   - SD card configuration support
   - Network reconnection logic

### Hardware Abstraction

**Board Support** (`platformio.ini`)
- 40+ board environments with specific configurations
- Board-specific build flags (display types, pin mappings, memory configurations)
- Environment-specific library dependencies and ignore lists
- Partition schemes optimized for each board type

**Driver Architecture** (`src/drivers/`)
- `displays/`: TFT, OLED, AMOLED display drivers
- `storage/`: SD card and file system management
- `devices/`: Board-specific hardware initialization

### Memory Management

**ESP32 Classic Considerations**
- Reduced stack sizes for compatibility (`5000` vs `6000` bytes for miner tasks)
- Memory-optimized build flags and partition schemes
- Hardware acceleration prioritized due to memory constraints

**PSRAM Boards**
- Larger memory allocations for UI and buffers
- Enhanced graphics capabilities
- More concurrent mining threads

## Configuration System

**WiFi Setup**
- Access point: `NerdMinerAP` / Password: `MineYourCoins`
- Web interface for pool configuration (URL, port, BTC address)
- Timezone and worker name settings

**SD Card Config** (optional)
```json
{
  "SSID": "network_name",
  "WifiPW": "password",
  "PoolUrl": "public-pool.io",
  "PoolPort": 21496,
  "BtcWallet": "your_btc_address",
  "Timezone": 2
}
```

## Button Controls

> **Note**: Multiple screen modes (Clock, Global Hash, BTC Price) have been removed. Only the Mining screen is displayed.

**Single Button Devices**
- Click: Toggle screen on/off
- Hold 2s: Reset configuration

**Dual Button Devices**
- Button 1: Toggle screen on/off, Double-click to rotate display
- Button 2: Reset configuration (hold 5s)

## Development Notes

**Task Priority System**
- Monitor task: Priority 5 (highest)
- Stratum task: Priority 4
- Miner HW tasks: Priority 3
- Miner SW tasks: Priority 2
- Main loop: Priority 2

**Task Stack Sizes (Board-Specific)**
- Monitor: 9500 bytes (ESP32 classic) / 10000 bytes (others)
- Stratum: 12000 bytes (ESP32 classic) / 13500 bytes (ESP32-2432S028R) / 15000 bytes (others)
- Miner HW: 3584 bytes (ESP32 classic) / 4096 bytes (others)
- Miner SW: 5000 bytes (ESP32 classic) / 6000 bytes (others)

**Thread Safety & Synchronization**
- Mutexes: `s_id_mutex` (stratum ID counter), `best_diff_mutex` (difficulty tracking)
- Atomic variables: `hashes`, `Mhashes`, `shares`, `valids`, `templates`, `upTime`, `s_client_connected`
- Job queue system: `JOB_QUEUE_SIZE=8`, `RESULT_LIST_SIZE=16`, `SUBMISSION_MAP_MAX=16`

**Nonce Distribution Strategy**
- Hardware mining: `NONCE_PER_JOB_HW=64K` nonces per job
- Software mining: `NONCE_PER_JOB_SW=16K` nonces per job
- Random nonce masking: `0xFFFFC000` for 16KB alignment
- Start values: `NONCE_START_RANDOM=0xDA54E700`, `NONCE_START_I2C_SLAVE=0x10000000`

**Watchdog Configuration**
- General WDT: 3 seconds (`WDT_TIMEOUT`)
- Miner WDT: 900 seconds / 15 minutes (`WDT_MINER_TIMEOUT`)
- SHA hardware timeout: 50000 cycles (`SHA_HARDWARE_TIMEOUT_CYCLES`)
- Core 0 WDT: Disabled (full CPU utilization for mining)

**Statistics Persistence (NVS)**
- Progressive save intervals: 5min → 15min → 30min → 1hr → 3hr → 6hr → 12hr
- Tracks total hashes, shares, uptime, best difficulty across reboots
- Uses ESP32 Non-Volatile Storage (NVS) API

**Build Flags**
- Optimization: `-O3`, `-ffast-math`, `-funroll-loops`
- Memory optimization: `-ffunction-sections`, `-fdata-sections`, `-fomit-frame-pointer`, `-Wl,--gc-sections`
- Critical defines: `HARDWARE_SHA265` (SHA256 acceleration), `DEBUG_MINING` (debug output)
- Board-specific: `NERDMINERV2`, `M5STICK_C`, `ESP32_2432S028R`, etc.

**Build Scripts**
- `auto_firmware_version.py`: Git-based version tagging
- `post_build_merge.py`: Firmware packaging for distribution

**Key Constants**
- `MAX_NONCE_STEP`: 5M nonce range per mining iteration
- `KEEPALIVE_TIME_ms`: 30s pool connection maintenance
- `POOLINACTIVITY_TIME_ms`: 60s pool inactivity timeout
- `MAX_POOL_LINE_SIZE`: 4096 bytes for network safety

**External API Endpoints**
- BTC price: `api.coingecko.com/api/v3/simple/price` (updates every 1 min)
- Block height: `mempool.space/api/blocks/tip/height` (every 2 min)
- Global hashrate: `mempool.space/api/v1/mining/hashrate/3d` (every 2 min)
- Difficulty adjustment: `mempool.space/api/v1/difficulty-adjustment` (every 2 min)
- Fee estimates: `mempool.space/api/v1/fees/recommended` (every 2 min)
- Pool statistics: `public-pool.io:40557/api/client/{btc_address}` (every 1 min)

## Pool Compatibility

Optimized for low-difficulty pools that support small miners:
- public-pool.io (port 21496) - Primary target
- nerdminers.org (port 3333)
- Various community pools listed in README.md

Standard pools like solo.ckpool.org require higher difficulty and may not show miner activity.