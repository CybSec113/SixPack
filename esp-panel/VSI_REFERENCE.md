# Vertical Speed Indicator (VSI) - Quick Reference

## Specifications
- **Range**: -2000 to +2000 fpm
- **Zero Position**: 270° (9 o'clock)
- **+2000 fpm**: 85° (175° clockwise from zero)
- **-2000 fpm**: 95° (175° counter-clockwise from zero)
- **Motors**: 1 (same pinout as airspeed)
- **X-Plane DREF**: `sim/cockpit2/gauges/indicators/vvi_fpm_pilot`

## Calibration Points
```c
{-2000,  95},   // -2000 fpm at 95°
{-1000, 182},   // -1000 fpm at 182°
{    0, 270},   // 0 fpm at 270° (9 o'clock)
{ 1000, 358},   // +1000 fpm at 358°
{ 2000,  85},   // +2000 fpm at 85°
```

## Build Instructions

```bash
cd esp-panel
source <ESP-IDF-PATH>/export.sh
idf.py set-target esp32c3
idf.py fullclean

# Configure
idf.py menuconfig
# Select: Instrument Configuration → Vertical Speed Indicator
# Set WiFi SSID, Password, RPI IP, ESP Device ID: ESP_VertSpeed

# Build and flash
idf.py -D INSTRUMENT=vertspeed build
idf.py -p /dev/tty.usbmodem13301 flash monitor
```

## Or use sdkconfig defaults:
```bash
cp sdkconfig.defaults.esp32c3.vertspeed sdkconfig.defaults
idf.py build flash monitor
```

## Testing
```bash
# From rpi_hub.py:
ESP_VertSpeed:0:VALUE:0      # Zero (270°)
ESP_VertSpeed:0:VALUE:1000   # +1000 fpm (358°)
ESP_VertSpeed:0:VALUE:-1000  # -1000 fpm (182°)
ESP_VertSpeed:0:VALUE:2000   # +2000 fpm (85°)
ESP_VertSpeed:0:VALUE:-2000  # -2000 fpm (95°)
```

## Notes
- Values exceeding ±2000 fpm are clamped to the limits
- ZERO command sets position to 270° (0 fpm)
- Uses same GPIO pins as airspeed indicator (3,4,5,6)
