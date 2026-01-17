# SixPack
x-plane instrument panel implementation.  
This is very much a work in progress, and I will update as often as possible.

# Requirements
1. x-plane 11 (may also work on x-plane, but haven't tested it)
1. Raspberry Pi running headless OS - any variant with standard GPIO pinout and networking.
1. ESP32-C3 - one for each instrument
1. 28BYJ-48 stepper motor (usually comes with ULN2003 controller)
1. Breadboard and/or jumper wires
1. DC power source (e.g., ToolkitRC P200)
1. 3-D printer to print the instrument structures and the 6-pack frame
1. Miscellaneous tool/hardware (e.g., M4, M3 screws, etc.)

# Software Stucture
Truth be told, I did not write a single line of code (though I have significant software development experience).  All code written by a variety of AI agents.  Even so, I challenge anyone to prompt AI agents to develop a similar framework.  

## Basic Software Components

* **ESP firmware** - written in C and built/flashed with esp-idf tools (link below) - Each ESP has different firmware, depending on which instrument it's controlling.
* **Web Server** - (Python) Intuitively, I knew there would be a need to test, calibrate, and zero the instruments.
* **Raspberry Pi Hub** - (Python) receives/processes/sends UDP packets from xplane / to ESPs (and web_server.py)


# Full System Setup

## ESP32-C3 Setup

### Prerequisites
1. Install ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/
2. Clone this repo and navigate to `esp-panel/`

### Configuration
1. Copy `.env.example` to `.env` and update with your WiFi credentials:
   ```bash
   cp .env.example .env
   # Edit .env with your WiFi SSID, password, and RPi IP
   ```

2. Run menuconfig to set WiFi and network settings:
   ```bash
   source <ESP-IDF-PATH>/export.sh
   idf.py menuconfig
   ```
   Navigate to "WiFi Configuration" and "Network Configuration" to set values

### Build and Flash
There are diffent types of ESPs that can be built (e.g., Airspeed, Gyro, etc.), so we have to update the setup before each build (improve this process?):  
* udpdate the .env file to reflect the which ESP is being built
   
```bash
idf.py --list-targets
iff.py set-target esp32c3           # or whatever variant you're using
idf.py menuconfig                   # update Instrument Config and Network seetings to match ESP build
idf.py -D INSTRUMENT=airspeed build # use actual instrument being built
idf.py flash monitor
```

Watch the logs for WiFi connection and heartbeat messages.

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

## Docker Mode

When running in Docker containers, GPIO output is disabled and motor movements are logged to stdout instead.

Detection is automatic via `/.dockerenv` file presence.
