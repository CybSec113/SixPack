# Building Firmware for Different Instruments

Each ESP32 requires separate firmware compiled with its instrument-specific calibration data.

## Prerequisites

Ensure ESP-IDF is sourced:
```bash
source <ESP-IDF-PATH>/export.sh
```

## Building for Each Instrument

### 1. Airspeed Indicator (ESP_Airspeed)

```bash
# Clean previous build
idf.py fullclean

# Configure for Airspeed
idf.py menuconfig
# Navigate to: Instrument Configuration → Select Instrument Type → Airspeed Indicator
# Also set: Network Configuration → ESP Device ID = "ESP_Airspeed"

# Build and flash
idf.py build
idf.py -p /dev/tty.usbmodem13301 flash monitor
```

**Calibration:** 0-200 knots linearly mapped to 0-360 degrees

### 2. Gyro Compass / Heading (ESP_GyroCompass)

```bash
# Clean previous build
idf.py fullclean

# Configure for Gyro Compass
idf.py menuconfig
# Navigate to: Instrument Configuration → Select Instrument Type → Gyro Compass / Heading
# Also set: Network Configuration → ESP Device ID = "ESP_GyroCompass"

# Build and flash
idf.py build
idf.py -p /dev/tty.usbmodem13301 flash monitor
```

**Calibration:** 0-360 degrees heading directly mapped to 0-360 degrees motor angle

## Adding New Instruments

To add a new instrument:

1. **Add Kconfig option** in `main/Kconfig.projbuild`:
   ```kconfig
   config INSTRUMENT_YOUR_INSTRUMENT
       bool "Your Instrument Name"
       help
           Description of the instrument
   ```

2. **Add calibration data** in `main/airspeed.c`:
   ```c
   #elif CONFIG_INSTRUMENT_YOUR_INSTRUMENT
   static const cal_point_t calibration[10] = {
       {value1, angle1},
       {value2, angle2},
       // ... 10 calibration points
   };
   ```

3. **Build and flash** following the steps above, selecting your new instrument type.

## Configuration Settings

### WiFi Configuration
- SSID and password for RPi network connection

### Network Configuration
- **RPI_IP_ADDRESS**: IP address of Raspberry Pi hub
- **ESP_DEVICE_ID**: Unique identifier matching `instrument_mapping.json`

### Instrument Configuration
- **Select Instrument Type**: Choose the instrument for this ESP

## Debugging

Monitor serial output to verify:
- WiFi connection
- Heartbeat messages sent to RPi
- VALUE commands received and converted
- Motor angle calculations

```bash
idf.py monitor
# Press Ctrl+] to exit
```

## Build Artifacts

After successful build:
- Binary: `build/esp-panel.bin`
- ELF: `build/esp-panel.elf`

These can be flashed to other ESP32 devices with identical configurations using:
```bash
esptool.py -p <PORT> write_flash 0x0 build/esp-panel.bin
```
