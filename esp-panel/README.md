| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- |

# SixPack Panel - Instrument Firmware

ESP32 firmware for controlling stepper motor-driven aircraft instruments. Each ESP runs independent firmware compiled with instrument-specific configuration.

## Overview

This firmware enables an ESP32-C3 to:
- Receive real-time X-Plane data values via UDP (port 49003)
- Convert values to stepper motor angles using calibration curves
- Control 1-2 × 28BYJ-48 stepper motors via ULN2003 modules
- Send periodic heartbeat messages to the Raspberry Pi hub (port 49002)
- Support WiFi logging via TCP (port 9998)

## Supported Instruments

| Instrument | ESP_ID | Motors | X-Plane DREF |
|------------|--------|--------|--------------|
| Airspeed | ESP_Airspeed | 1 | `sim/flightmodel/position/indicated_airspeed` |
| Gyro Compass | ESP_Gyrocompass | 2 | `sim/cockpit2/gauges/indicators/heading_vacuum_deg_mag_pilot` |
| Altimeter | ESP_Altimeter | 2 | `sim/cockpit2/gauges/indicators/altitude_ft_pilot` |
| Vertical Speed | ESP_VertSpeed | 1 | `sim/cockpit2/gauges/indicators/vvi_fpm_pilot` |

## Quick Start

### Prerequisites

1. [Install ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/)
2. ESP32-C3 development board
3. 28BYJ-48 stepper motor(s) with ULN2003 controller(s)
4. WiFi network (same as Raspberry Pi hub)

### Build and Flash

```bash
source <ESP-IDF-PATH>/export.sh
idf.py set-target esp32c3
idf.py menuconfig
# Select: Instrument Configuration → [Choose instrument]
# Set: WiFi SSID, Password, RPI IP, ESP Device ID
idf.py build
idf.py -p /dev/tty.usbmodem13301 flash monitor
```

### Using sdkconfig Defaults

```bash
cp sdkconfig.defaults.esp32c3.airspeed sdkconfig.defaults
idf.py build flash monitor
```

## Instrument Specifications

### Airspeed Indicator
- **Range**: 40-200 knots
- **Calibration**: 40kt→32°, 100kt→161°, 200kt→315°
- **Motors**: 1

### Gyro Compass
- **Range**: 0-360°
- **Motors**: 2 (Motor 0: compass rose, Motor 1: heading bug)

### Altimeter
- **Range**: 0-10,000+ feet
- **Motors**: 2 (Motor 0: altitude needle, Motor 1: barometer setting)

### Vertical Speed Indicator (VSI)
- **Range**: -2000 to +2000 fpm
- **Zero Position**: 270° (9 o'clock)
- **+2000 fpm**: 85° (175° CW from zero)
- **-2000 fpm**: 95° (175° CCW from zero)
- **Motors**: 1
- **Calibration**:
  - -2000 fpm → 95°
  - -1000 fpm → 182°
  - 0 fpm → 270°
  - +1000 fpm → 358°
  - +2000 fpm → 85°

## Hardware Connections

### Motor Pinout (All Instruments)
```
ESP32-C3    ULN2003
GPIO 3  →   IN1
GPIO 4  →   IN2
GPIO 5  →   IN3
GPIO 6  →   IN4
```

### Power
- ESP32: USB 5V
- Stepper motors: External 5V supply (ULN2003 VCC)

## UDP Protocol

### Incoming Commands (from rpi_hub)
```
VALUE:0:75      # Motor 0: Convert value (75 knots/fpm/degrees) to angle
VALUE:1:45      # Motor 1: Convert value to angle
ANGLE:0:180     # Motor 0: Move directly to 180°
ZERO:0          # Motor 0: Reset to zero position
```

### Outgoing (to rpi_hub)
```
HEARTBEAT:ESP_Airspeed:12345    # Every 5 seconds (uptime in seconds)
```

## Testing

From `rpi_hub.py` command line:
```bash
# Airspeed
ESP_Airspeed:0:VALUE:100

# VSI
ESP_VertSpeed:0:VALUE:0      # Zero (270°)
ESP_VertSpeed:0:VALUE:1000   # +1000 fpm
ESP_VertSpeed:0:VALUE:-1000  # -1000 fpm

# Gyro Compass
ESP_Gyrocompass:0:VALUE:180  # Compass rose to 180°
ESP_Gyrocompass:1:VALUE:90   # Heading bug to 90°
```

## Configuration

Run `idf.py menuconfig` to set:
- **WiFi Configuration**: SSID and password
- **Network Configuration**: RPi IP address and ESP Device ID
- **Instrument Configuration**: Select instrument type

## Motor Specifications

| Parameter | Value |
|-----------|-------|
| Resolution | Full step (4-step sequence) |
| Steps per revolution | 2048 |
| Step period | 5ms |
| Full rotation time | ~10 seconds |
| Max concurrent motors | 2 |

## Debugging

### Serial Monitor
```bash
idf.py monitor
```

### WiFi Logging
Logs stream to TCP port 9998 after WiFi connects:
```bash
nc <ESP_IP> 9998
```

Watch for:
- `Got IP: 192.168.x.x`
- `Heartbeat task started`
- `Socket bound, listening on port 49003`
- `Received: VALUE:` command processing
- `Motor START: current=X°, target=Y°`

## Adding New Instruments

See [BUILD_INSTRUMENTS.md](BUILD_INSTRUMENTS.md) for detailed instructions.

Summary:
1. Create `main/newinstrument.c` with calibration array
2. Add Kconfig option in `main/Kconfig.projbuild`
3. Update `main/CMakeLists.txt`
4. Create `sdkconfig.defaults.esp32c3.newinstrument`
5. Update `instrument_mapping.json`

## Troubleshooting

| Issue | Solution |
|-------|----------|
| WiFi connection fails | Check SSID/password in menuconfig |
| No heartbeats | Verify RPI_IP and network connectivity |
| Motors don't move | Check UDP port 49003, verify wiring |
| Erratic movement | Check power supply, ensure 5V for motors |
