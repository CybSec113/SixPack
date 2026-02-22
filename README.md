# SixPack
X-Plane instrument panel implementation using ESP32-C3 controllers and stepper motors.
This is a work in progress with continuous improvements.

# Requirements
1. X-Plane 11+ (may work on earlier versions)
1. Raspberry Pi running headless OS (any variant with standard GPIO and networking)
1. ESP32-C3 - one for each instrument (7 total for full panel; 1 dedicated to input data (e.g., heading bug, baro, etc.))
1. 28BYJ-48 stepper motor + ULN2003 controller (per motor: 10 total)
1. Variety of 20-22 gauge wires and male/female connectors
1. Miscellaneous electronics components: capacitors, resistors, soldering iron, etc.
1. DC power supply (5V, up to 4 amps, common ground, for ESCs and steppers)
1. 3D printer for instrument housings and frame (optional)
1. Miscellaneous hardware (M3/M4 screws, etc.)

# Software Architecture

All code generated with AI assistance. The system consists of three main components:

## Components

- **ESP Firmware** (C, ESP-IDF): Per-instrument firmware controlling 1-2 stepper motors
- **RPi Hub** (Python): UDP router for X-Plane data → ESP commands
- **Web Server** (Python/Flask): Calibration, testing, and device monitoring dashboard

### Data Flow
```
X-Plane → RPi Hub → ESPs
```

## Basic Software Components

* **ESP firmware** - written in C and built/flashed with esp-idf tools (link below) - Each ESP has different firmware, depending on which instrument it's controlling.
* **Web Server** - (Python) Intuitively, I knew there would be a need to test, calibrate, and zero the instruments.
* **Raspberry Pi Hub** - (Python) receives/processes/sends UDP packets from xplane / to ESPs (and web_server.py)


# Full System Setup

## ESP32-C3 Setup

### Prerequisites
1. Install ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/
2. Clone this repo and navigate to `esp-panel/`

### Configuraion
See [BUILD_INSTRUMENTS](./esp-panel/BUILD_INSTRUMENTS.md) for details on building ESPs.


## Raspberry Pi Setup

### Prerequisites
1. Raspberry Pi with Python 3 and pip installed
1. Run RPi, ESPs, xplane on SAME network
1. RPi on WiFi is *possible*, but I got more stable results on ethernet

### Installation
```bash
pip install -r requirements.txt
```

### Running the Hub
```bash
python3 rpi_hub.py
```

The hub will:
- Listen for heartbeats from ESP32 devices on port 49002
- Display connected devices
- Allow sending commands to specific ESPs

Commands:
- `list` - Show online ESP devices
- `ESP_ID:motor:message` - Send message to specific ESP (e.g., `ESP32_Gyrocompass:0:MOVE:180`)

### Web Dashboard

Optionally, run the web server for a graphical interface and to Zero the instruments:
```bash
python3 web_server.py
```

Access the dashboard at `http://<rpi-ip>:5000`

Features:
- Real-time ESP device monitoring
- Motor control with angle slider
- Calibration point configuration  
   * although this features works, the ESP doesn't have a partition to same the calibration file.  So, for now, calibration (e.g., airspeed -> circular angle) is hardcoded in the respective firmware.c file.
- Live device status and uptime

## X-Plane Integration
1. Set up Data Output in X-Plane for airspeed to RPi IP:49001
1. Select the DREFs (write) as mapped in [instrument_mapping.json](./instrument_mapping.json)


# Quick Reference - Important Stuff
## Ports
|Port|From|To|
|---|---|---|
|49000|RPi rpi_hub|x-plane|
|49001|x-plane|RPi rpi_hub|
|49002|ESP (Heatbeat)|RPi|
|49003|RPi|ESP|
|49004|ESP Encoder|RPi|
|5000|Any|RPi Webserver|
|9999|ESP Logger|Any|


# Dockerfiles
I originally thought of using Docker as a testbed, but the relaity is, there's no substitute for testing on the actual RPi and instruments.  The Docker files are there for future use...
1. **xplane-sim**: this container will transmit a file with pre-recorded x-plane DREF messages
1. **pi-dispatcher**: this container will receive x-plane message and relay UDP messages to the appropriate ESP32 (e.g., airspeed --> ESP32-01)
1. **esp-sim**: this container will receive the UDP messages from pi-dispatcher and translate the parameter value to motor controls (e.g., 70 KIAS --> 90 degrees CW from 12-O'Clock)

## Docker Testing

Run the simulation stack:
```bash
cd panel-sim
docker-compose up
```

This creates three containers:
- **xplane-sim**: Transmits pre-recorded X-Plane DREF messages
- **pi-dispatcher**: Receives X-Plane data and relays to ESP simulators
- **esp-sim**: Simulates ESP32 receiving motor commands
