| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- |

# SixPack Panel - Instrument Firmware

ESP32 firmware for controlling stepper motor-driven aircraft instruments. Each ESP runs independent firmware compiled with instrument-specific configuration.

## Overview

This firmware enables an ESP32-C3 to:
- Receive real-time X-Plane data values via UDP (port 49003)
- Convert values to stepper motor angles using calibration curves
- Control up to 2 × 28BYJ-48 stepper motors via ULN2003 modules
- Send periodic heartbeat messages to the Raspberry Pi hub (port 49002)
- Support WiFi logging via TCP (port 9998/9999)
- Support multiple instrument types (airspeed, heading, altitude, VSI, attitude, turn indicator)

## Quick Start

For detailed build instructions for each instrument type, see [BUILD_INSTRUMENTS.md](BUILD_INSTRUMENTS.md).

### Prerequisites

1. [Install ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/)
2. ESP32-C3 development board
3. 28BYJ-48 stepper motor(s) with ULN2003 controller(s)
4. Connected to same WiFi as Raspberry Pi hub

### Build for Gyrocompass (Dual Motor)

```bash
source <ESP-IDF-PATH>/export.sh
idf.py set-target esp32c3
idf.py menuconfig
# Select: Instrument Configuration → Gyro Compass / Heading
idf.py build
idf.py -p /dev/tty.usbmodem13301 flash monitor
```

### Monitor Logs via WiFi

While serial monitor also works, you can stream logs over TCP:

```bash
# Terminal 1: Listen for logs (Gyrocompass on 9999)
nc -l 9999

# Terminal 2: Monitor serial output
idf.py monitor
```

## Firmware Architecture

### Dual Motor Support
Instruments can control 1 or 2 motors concurrently:
- **Motor 0**: Primary instrument (compass rose, airspeed needle, etc.)
- **Motor 1**: Secondary (heading bug, attitude offset, etc.)

Each motor has its own FreeRTOS task and queue for parallel movement.

### Calibration
Calibration is hardcoded per instrument type. The `value_to_angle()` function performs linear interpolation between calibration points.

**Example (Airspeed):**
- 40 knots → 32°
- 100 knots → 161°
- 200 knots → 315°

### Motor Control
- **Resolution**: Full stepping (4-step sequence, 2048 steps/360°)
- **Speed**: 10ms delay between steps
- **Parallel Execution**: Both motors step independently

### UDP Protocol

**Incoming Commands (from rpi_hub):**
- `VALUE:0:75` - Motor 0: Convert 75 knots to angle
- `VALUE:1:45` - Motor 1: Convert 45° (heading bug) to angle
- `ANGLE:0:180` - Motor 0: Move directly to 180°
- `ZERO:0` - Motor 0: Reset to 0°

**Outgoing:**
- `HEARTBEAT:ESP_Gyrocompass:12345` - Every 5 seconds (uptime in seconds)

## Motor Specifications

| Parameter | Value |
|-----------|-------|
| Resolution | Full step (4-step) |
| Steps per revolution | 2048 |
| Delay per step | 10ms |
| Full rotation time | ~204ms |
| Max concurrent motors | 2 |

## Adding New Instruments

See [BUILD_INSTRUMENTS.md](BUILD_INSTRUMENTS.md#adding-new-instruments).

Quick summary:
1. Add Kconfig option in `main/Kconfig.projbuild`
2. Add calibration array in appropriate source file
3. Configure and build

## Configuration

Run `idf.py menuconfig` to set:

- **WiFi SSID & Password**: Network details
- **RPi IP Address**: Hub location (e.g., 192.168.1.100)
- **ESP Device ID**: Unique name (e.g., ESP_Gyrocompass)
- **Instrument Type**: Select from configured options

## WiFi Logging

Logs stream to TCP after WiFi connects:

```bash
# Python listener (auto-reconnect friendly)
python3 receive_logs.py <ESP_IP> 9999
```

Ports by instrument:
- Gyrocompass: `9999`
- Airspeed: `9998`

## Debugging

Monitor serial output:
```bash
idf.py monitor
```

Watch for:
- `WiFi logging server listening on port XXXX`
- `Got IP: 192.168.x.x`
- `Heartbeat task started`
- Motor 0 & 1 task creation
- `Received: VALUE:` and command processing

## Hardware Connections
````
<userPrompt>
Provide the fully rewritten file, incorporating the suggested code change. You must produce the complete file.
</userPrompt>
