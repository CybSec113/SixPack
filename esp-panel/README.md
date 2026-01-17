| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- |

# SixPack Panel - Instrument Firmware

ESP32 firmware for controlling stepper motor-driven aircraft instruments. Each ESP runs independent firmware compiled with instrument-specific calibration.

## Overview

This firmware enables an ESP32-C3 to:
- Receive real-time X-Plane data values via UDP
- Convert values to stepper motor angles using hardcoded calibration curves
- Control a 28BYJ-48 stepper motor via ULN2003 module
- Send periodic heartbeat messages to the Raspberry Pi hub
- Support multiple instrument types (airspeed, heading, altitude, VSI, etc.)

## Quick Start

For detailed build instructions for each instrument type, see [BUILD_INSTRUMENTS.md](BUILD_INSTRUMENTS.md).

### Prerequisites

1. [Install ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/)
2. ESP32-C3 development board
3. 28BYJ-48 stepper motor with ULN2003 controller
4. Connected to same WiFi as Raspberry Pi hub

### Build for Airspeed

```bash
source <ESP-IDF-PATH>/export.sh
idf.py set-target esp32c3
idf.py menuconfig
# Select: Instrument Configuration → Airspeed Indicator
idf.py build
idf.py -p /dev/tty.usbmodem13301 flash monitor
```

## Firmware Architecture

### Hardcoded Calibration
Each instrument type has 10 calibration points compiled into the firmware. The `value_to_angle()` function performs linear interpolation between points for smooth response.

**Example (Airspeed):**
- 0 knots → 0°
- 100 knots → 180° (top of dial)
- 200 knots → 360°

### Motor Control
The firmware uses:
- **Resolution**: Full stepping (4 coils per revolution)
- **Speed**: 10ms delay between steps (~100 ms per 360° rotation)
- **Sequence**: SEQUENCE_FULL defines the coil activation pattern

### UDP Protocol

**Incoming Commands:**
- `VALUE:75` - Convert 75 knots to angle and move motor
- `ANGLE:90` - Move directly to 90 degrees (bypass calibration)
- `ZERO:` - Reset position to 0 degrees

**Outgoing:**
- `HEARTBEAT:ESP_Airspeed:12345` - Sent every 5 seconds (uptime in seconds)

## Adding New Instruments

See [BUILD_INSTRUMENTS.md](BUILD_INSTRUMENTS.md#adding-new-instruments) for detailed instructions.

Quick summary:
1. Add instrument option to `main/Kconfig.projbuild`
2. Add calibration array in `main/airspeed.c`
3. Build and flash with new configuration

## Configuration

Run `idf.py menuconfig` to set:

- **WiFi SSID & Password**: Network details
- **RPi IP Address**: Hub location (e.g., 192.168.7.219)
- **ESP Device ID**: Unique name (e.g., ESP_Airspeed)
- **Instrument Type**: Select from configured options

## Debugging

Monitor serial output:
```bash
idf.py monitor
```

Watch for:
- `WiFi connection successful`
- `Socket bound, listening on port 49003`
- `Heartbeat task started` and periodic heartbeat sends
- `VALUE:` and `ANGLE:` command processing
- Motor angle conversions and movements

## Hardware Connections

```
ESP32-C3    ULN2003    28BYJ-48
GPIO3  ──── IN1
GPIO4  ──── IN2
GPIO5  ──── IN3
GPIO6  ──── IN4
GND    ──── GND ────── GND
       +5V ──── +5V (external supply)
```

Ensure common ground between ESP32, ULN2003, and external 5V supply.

## Performance Notes

- **Motor Response**: ~100ms per full rotation
- **Calibration Resolution**: 10-point linear interpolation (smooth curve)
- **Uptime Overflow**: ~50 days at max portTICK_PERIOD_MS

## For More Information

See parent [README.md](../README.md) for system architecture and RPi hub details.

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

## Example Output

As you run the example, you will see the LED blinking, according to the previously defined period. For the addressable LED, you can also change the LED color by setting the `led_strip_set_pixel(led_strip, 0, 16, 16, 16);` (LED Strip, Pixel Number, Red, Green, Blue) with values from 0 to 255 in the [source file](main/airspeed.c).

```text
I (315) example: Example configured to blink addressable LED!
I (325) example: Turning the LED OFF!
I (1325) example: Turning the LED ON!
I (2325) example: Turning the LED OFF!
I (3325) example: Turning the LED ON!
I (4325) example: Turning the LED OFF!
I (5325) example: Turning the LED ON!
I (6325) example: Turning the LED OFF!
I (7325) example: Turning the LED ON!
I (8325) example: Turning the LED OFF!
```

Note: The color order could be different according to the LED model.

The pixel number indicates the pixel position in the LED strip. For a single LED, use 0.

## Troubleshooting

* If the LED isn't blinking, check the GPIO or the LED type selection in the `Example Configuration` menu.

For any technical queries, please open an [issue](https://github.com/espressif/esp-idf/issues) on GitHub. We will get back to you soon.
