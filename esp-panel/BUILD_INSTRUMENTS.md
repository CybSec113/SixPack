# Building Firmware for Different Instruments

Each ESP32 requires separate firmware compiled with its instrument-specific calibration data and motor configuration.

## Prerequisites

Ensure ESP-IDF is sourced:
```bash
source <ESP-IDF-PATH>/export.sh
idf.py set-target esp32c3
```

## Building for Each Instrument

All instruments use the same CMakeLists.txt with environment-based configuration:

```bash
idf.py fullclean
idf.py menuconfig
# Set: Network → WiFi SSID, Password, RPI_IP, ESP_DEVICE_ID
idf.py -D INSTRUMENT=altimeter build
idf.py -p /dev/tty.usbmodem13301 flash monitor
```

### Supported Instruments

| Instrument | ESP_ID | Motors | Build Command |
|------------|--------|--------|---------------|
| Airspeed Indicator | ESP_Airspeed | 1 | `CONFIG_INSTRUMENT_AIRSPEED=y` |
| Attitude Indicator | ESP_AttitudeIndicator | 2 | `CONFIG_INSTRUMENT_ATTITUDE=y` |
| Altimeter | ESP_Altimeter | 2 | `CONFIG_INSTRUMENT_ALTIMETER=y` |
| Turn Coordinator | ESP_TurnIndicator | 1 | `CONFIG_INSTRUMENT_TURN=y` |
| Gyro Compass | ESP_Gyrocompass | 2 | `CONFIG_INSTRUMENT_GYRO_COMPASS=y` |
| Vertical Speed | ESP_VertSpeed | 1 | `CONFIG_INSTRUMENT_VERTSPEED=y` |

## Example: Building Gyrocompass (Dual Motor)

```bash
# Clean
idf.py fullclean

# Configure
idf.py menuconfig
# Set:
#   - Instrument Configuration → Gyro Compass / Heading
#   - WiFi SSID & Password
#   - RPI IP Address: 192.168.x.x
#   - ESP Device ID: ESP_Gyrocompass

# Build and flash
idf.py build
idf.py -p /dev/tty.usbmodem13301 flash monitor

# In another terminal, monitor WiFi logs:
python3 receive_logs.py 192.168.x.x 9999
```

## Motor Configuration

Each instrument specifies motor count in `main/Kconfig.projbuild`:

```kconfig
config INSTRUMENT_GYRO_COMPASS
    bool "Gyro Compass / Heading (Dual Motor)"
    help
        Dual motor: Motor 0 = compass rose, Motor 1 = heading bug
```

Motors operate concurrently with individual FreeRTOS tasks and queues.

## Calibration Data

Calibration points are hardcoded in source files:

- **airspeed.c**: 40-200 knots → 32-315°
- **gyrocompass.c**: 0-360° heading → 0-360° motor angle
- Add more instruments by creating new source files

## Adding New Instruments

1. **Create source file** `main/newinstrument.c` with calibration:
   ```c
   static const cal_point_t calibration[N] = {
       {value1, angle1},
       {value2, angle2},
       // ... N points
   };
   ```

2. **Add Kconfig option** in `main/Kconfig.projbuild`:
   ```kconfig
   config INSTRUMENT_NEWINSTRUMENT
       bool "New Instrument Name"
       help
           Description
   ```

3. **Update CMakeLists.txt** `main/CMakeLists.txt`:
   ```cmake
   elseif(CONFIG_INSTRUMENT_NEWINSTRUMENT)
       list(APPEND SRCS "newinstrument.c")
   endif()
   ```

4. **Build** with new instrument selected in menuconfig

## WiFi Logging

Logs stream to TCP immediately after WiFi connects:

```bash
# Gyrocompass logs (port 9999)
python3 receive_logs.py 192.168.x.x 9999

# Airspeed logs (port 9998)
python3 receive_logs.py 192.168.x.x 9998
```

Serial monitor also works via `idf.py monitor`.

## Build Output

After successful build:
- Binary: `build/esp-panel-*.bin`
- ELF: `build/esp-panel-*.elf`

Flash to other devices:
```bash
esptool.py -p <PORT> write_flash 0x0 build/esp-panel-*.bin
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| WiFi connection fails | Check SSID/password in menuconfig |
| No heartbeats received | Verify RPI_IP and network connectivity |
| Motors don't move | Check UDP port 49003 connectivity, verify calibration |
| WiFi logs not streaming | Ensure logs enabled in menuconfig, connect with `receive_logs.py` |
