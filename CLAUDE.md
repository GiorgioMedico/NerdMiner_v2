# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

NerdMiner v2 is an ESP32-based Bitcoin mining device that implements the Bitcoin stratum protocol to mine on solo pools. The project supports 40+ different ESP32 board variants with hardware abstraction layers for displays, input devices, and storage.

## Build Commands

This project uses PlatformIO as the build system:

```bash
# Build for specific board (see platformio.ini for all 40+ environments)
pio run -e NerdminerV2
pio run -e M5Stick-C-Plus2
pio run -e ESP32-devKitv1

# Build all default environments (will take significant time)
pio run

# Upload to device
pio run -e NerdminerV2 -t upload

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

**Single Button Devices**
- Click: Switch screen
- Double-click: Rotate display
- Triple-click: Toggle screen on/off
- Hold 5s: Reset configuration

**Dual Button Devices**
- Button 1: Screen switching, config reset
- Button 2: Display toggle, rotation

## Development Notes

**Task Priority System**
- Monitor task: Priority 5 (highest)
- Stratum task: Priority 4
- Miner tasks: Priority 1-3
- Main loop: Priority 4

**Build Scripts**
- `auto_firmware_version.py`: Git-based version tagging
- `post_build_merge.py`: Firmware packaging for distribution

**Key Constants**
- `MAX_NONCE_STEP`: 5M nonce range per mining iteration
- `KEEPALIVE_TIME_ms`: 30s pool connection maintenance
- `WDT_MINER_TIMEOUT`: 15min watchdog for mining tasks

## Pool Compatibility

Optimized for low-difficulty pools that support small miners:
- public-pool.io (port 21496) - Primary target
- nerdminers.org (port 3333)
- Various community pools listed in README.md

Standard pools like solo.ckpool.org require higher difficulty and may not show miner activity.