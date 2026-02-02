#!/usr/bin/env python3
import socket
import time
import struct
import json
from threading import Thread

HEARTBEAT_PORT = 49002
COMMAND_PORT = 49003
XPLANE_PORT = 49001
ENCODER_PORT = 49004  # New: Inputs ESP sends encoder events here
TIMEOUT = 30  # Increased from 15 to 30 seconds
DEVICES_FILE = 'esp_devices.json'
CALIBRATION_FILE = 'calibration.json'
MAPPING_FILE = 'instrument_mapping.json'

esp_devices = {}
calibration_points = []
instrument_mapping = {}

def load_calibration():
    """Load calibration points from file"""
    global calibration_points
    try:
        with open(CALIBRATION_FILE, 'r') as f:
            data = json.load(f)
            if 'ESP_Airspeed' in data and 'points' in data['ESP_Airspeed']:
                calibration_points = sorted(data['ESP_Airspeed']['points'], key=lambda p: p['value'])
                print(f"✓ Loaded {len(calibration_points)} calibration points")
                for p in calibration_points:
                    print(f"    {p['value']} knots -> {p['angle']}°")
            elif isinstance(data, list):
                # Fallback for flat array format
                calibration_points = sorted(data, key=lambda p: p['value'])
                print(f"✓ Loaded {len(calibration_points)} calibration points")
                for p in calibration_points:
                    print(f"    {p['value']} knots -> {p['angle']}°")
            else:
                print(f"✗ Calibration format error")
    except FileNotFoundError:
        print(f"✗ No calibration file found: {CALIBRATION_FILE}")
    except Exception as e:
        print(f"✗ Error loading calibration: {e}")

def load_instrument_mapping():
    """Load instrument mapping configuration"""
    global instrument_mapping
    try:
        with open(MAPPING_FILE, 'r') as f:
            instrument_mapping = json.load(f)
            print(f"✓ Loaded instrument mapping with {len(instrument_mapping.get('instruments', {}))} instruments")
    except FileNotFoundError:
        print(f"✗ No mapping file found: {MAPPING_FILE}")
    except Exception as e:
        print(f"✗ Error loading mapping: {e}")

def save_devices():
    try:
        with open(DEVICES_FILE, 'w') as f:
            json.dump(esp_devices, f, indent=2)
        print(f"[SAVE] Devices saved: {list(esp_devices.keys())}")
    except Exception as e:
        print(f"[ERROR] Failed to save devices: {e}")

def parse_dref_message(data):
    """Parse X-Plane DREF message"""
    data_stripped = data.rstrip(b'\x00')
    if len(data_stripped) < 9 or data_stripped[:5] != b'DREF+':
        return None
    try:
        value = struct.unpack('<f', data_stripped[5:9])[0]
        field = data_stripped[9:].decode('ascii', errors='replace')
        return {'value': value, 'field': field}
    except:
        return None

def heartbeat_listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('', HEARTBEAT_PORT))
    print(f"Listening for heartbeats on port {HEARTBEAT_PORT}")
    
    while True:
        try:
            data, addr = sock.recvfrom(1024)
            msg = data.decode()
            if msg.startswith("HEARTBEAT:"):
                parts = msg.split(":")
                if len(parts) >= 2:
                    esp_id = parts[1]
                    uptime = parts[2] if len(parts) > 2 else "?"
                    esp_devices[esp_id] = {'ip': addr[0], 'last_seen': time.time(), 'uptime': uptime}
                    save_devices()
                else:
                    print(f"[ERROR] Malformed heartbeat: {msg}")
        except Exception as e:
            print(f"[ERROR] Heartbeat listener: {e}")

def check_offline():
    while True:
        time.sleep(5)
        now = time.time()
        offline_devices = []
        for esp_id in list(esp_devices.keys()):
            if now - esp_devices[esp_id]['last_seen'] > TIMEOUT:
                offline_devices.append(esp_id)
        
        for esp_id in offline_devices:
            if esp_id in esp_devices:  # Double-check device still exists
                print(f"[OFFLINE] {esp_id}")
                del esp_devices[esp_id]

def send_command(esp_id, message):
    if esp_id in esp_devices:
        ip = esp_devices[esp_id]['ip']
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.sendto(message.encode(), (ip, COMMAND_PORT))
            print(f"→ {esp_id}: {message}")
            sock.close()  # Close socket immediately after sending
        except Exception as e:
            print(f"Send error: {e}")
    else:
        print(f"✗ {esp_id} offline")

def notify_webserver_xplane(field_name, value, esp_id, motor_id=0):
    """Notify web_server about X-Plane message for counter updates"""
    try:
        import requests
        requests.post('http://localhost:5000/api/xplane', 
                     json={'field_name': field_name, 'value': value, 'esp_id': esp_id, 'motor_id': motor_id},
                     timeout=1)
    except Exception as e:
        pass  # Silently fail if web_server not available

def xplane_listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('', XPLANE_PORT))
    print(f"Listening for X-Plane on port {XPLANE_PORT}")
    
    last_values = {}
    last_logged_dref = {}
    motor_accumulator = {}
    gyro_data = {'heading': None, 'bug': None}  # Track both gyro values
    
    while True:
        try:
            data, _ = sock.recvfrom(4096)
            parsed = parse_dref_message(data)
            if parsed:
                field = parsed['field']
                value = int(parsed['value'])
                current_time = time.time()
                
                # Log DREF (once per unique field to reduce spam)
                if field not in last_logged_dref:
                    print(f"[DREF] Received: {field} = {value}")
                    last_logged_dref[field] = True
                
                # Find instrument by DREF
                found = False
                for instrument_name, config in instrument_mapping.get('instruments', {}).items():
                    motors = config.get('motors', {})
                    for mid, motor_config in motors.items():
                        # Check single dref or array of drefs
                        drefs_to_check = motor_config.get('drefs', [motor_config.get('dref')]) if motor_config.get('drefs') else [motor_config.get('dref')]
                        if field in drefs_to_check:
                            esp_id = config.get('esp_id')
                            motor_id = int(mid)
                            found = True
                            
                            key = f"{esp_id}:{motor_id}"
                            
                            # If motor has multiple DREFs, accumulate them
                            if motor_config.get('drefs'):
                                if key not in motor_accumulator:
                                    motor_accumulator[key] = {'sum': 0, 'drefs': {}}
                                
                                motor_accumulator[key]['drefs'][field] = value
                                motor_accumulator[key]['sum'] = sum(motor_accumulator[key]['drefs'].values())
                                
                                # Only send when all DREFs have been received at least once
                                if len(motor_accumulator[key]['drefs']) == len(drefs_to_check):
                                    combined_value = motor_accumulator[key]['sum']
                                    
                                    # Normalize to 0-360
                                    combined_value = combined_value % 360
                                    if combined_value < 0:
                                        combined_value += 360
                                    
                                    last_val = last_values.get(key, -999)
                                    
                                    if abs(combined_value - last_val) > 1:
                                        print(f"[X-Plane] {instrument_name}: {combined_value} {config.get('unit', '')} (Motor {motor_id}) [XPlane: {motor_accumulator[key]['sum']}] [sum of {list(motor_accumulator[key]['drefs'].keys())}]")
                                        send_command(esp_id, f"VALUE:{motor_id}:{combined_value}")
                                        notify_webserver_xplane(field, combined_value, esp_id, motor_id)
                                        last_values[key] = combined_value
                            else:
                                # Single DREF - send directly
                                final_value = value
                                
                                # Filter: Ignore airspeed values > 200 knots
                                if esp_id == 'ESP_Airspeed' and value > 200:
                                    continue
                                
                                # Gyrocompass: accumulate both motors and send together
                                if esp_id == 'ESP_Gyrocompass':
                                    if motor_id == 0:
                                        gyro_data['heading'] = value
                                    elif motor_id == 1:
                                        gyro_data['bug'] = value
                                    
                                    # Send both motors when we have both values
                                    if gyro_data['heading'] is not None and gyro_data['bug'] is not None:
                                        heading_val = gyro_data['heading']
                                        bug_offset = (gyro_data['bug'] - gyro_data['heading']) % 360
                                        
                                        key0 = f"{esp_id}:0"
                                        key1 = f"{esp_id}:1"
                                        
                                        if abs(heading_val - last_values.get(key0, -999)) > 1 or abs(bug_offset - last_values.get(key1, -999)) > 1:
                                            print(f"[X-Plane] Gyrocompass: heading={heading_val}° bug_offset={bug_offset}° [bug={gyro_data['bug']}°]")
                                            send_command(esp_id, f"VALUE:0:{heading_val}")
                                            send_command(esp_id, f"VALUE:1:{bug_offset}")
                                            notify_webserver_xplane(field, heading_val if motor_id == 0 else bug_offset, esp_id, motor_id)
                                            last_values[key0] = heading_val
                                            last_values[key1] = bug_offset
                                else:
                                    # Non-gyro instruments
                                    last_val = last_values.get(key, -999)
                                    if abs(final_value - last_val) > 1:
                                        print(f"[X-Plane] {instrument_name}: {final_value} {config.get('unit', '')} (Motor {motor_id}) [XPlane: {value}]")
                                        send_command(esp_id, f"VALUE:{motor_id}:{final_value}")
                                        notify_webserver_xplane(field, final_value, esp_id, motor_id)
                                        last_values[key] = final_value
                            break
                    if found:
                        break
        except Exception as e:
            print(f"X-Plane error: {e}")

def encoder_listener():
    """Listen for encoder events from Inputs ESP"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('', ENCODER_PORT))
    print(f"Listening for encoder events on port {ENCODER_PORT}")
    
    # Track current values for relative encoders
    encoder_values = {}
    
    while True:
        try:
            data, addr = sock.recvfrom(1024)
            msg = data.decode()
            print(f"[DEBUG] Received raw message: {msg}")  # Debug
            
            if msg.startswith("ENCODER:"):
                parts = msg.split(":")
                if len(parts) >= 4:
                    encoder_name = parts[1]
                    value = int(parts[2])
                    button = parts[3]
                    
                    print(f"[ENCODER] {encoder_name}: value={value}, btn={button}")
                    
                    # Look up encoder configuration
                    inputs_config = instrument_mapping.get('instruments', {}).get('ESP_Inputs', {})
                    encoders = inputs_config.get('encoders', {})
                    encoder_config = encoders.get(encoder_name)
                    
                    if encoder_config:
                        dref_path = encoder_config.get('dref')
                        encoder_type = encoder_config.get('type', 'relative')
                        
                        if encoder_type == 'relative':
                            # Initialize if not exists
                            if encoder_name not in encoder_values:
                                encoder_values[encoder_name] = 0
                            
                            # Update value by the encoder delta
                            encoder_values[encoder_name] = (encoder_values[encoder_name] + value) % 360
                            if encoder_values[encoder_name] < 0:
                                encoder_values[encoder_name] += 360
                            
                            new_value = encoder_values[encoder_name]
                            print(f"[{encoder_name}] New value: {new_value}°")
                            
                            # Send to X-Plane via UDP
                            try:
                                xplane_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                                import struct
                                message = b"DREF\x00" + struct.pack('<f', float(new_value)) + dref_path.encode('utf-8')
                                xplane_sock.sendto(message, ('127.0.0.1', 49001))
                                xplane_sock.close()
                                print(f"[X-PLANE] Sent {encoder_name}: {new_value}° to {dref_path}")
                            except Exception as e:
                                print(f"[ERROR] Failed to send to X-Plane: {e}")
                    
                    # Notify web_server for UI updates
                    try:
                        import requests
                        payload = {'encoder': encoder_name, 'value': value, 'button': button}
                        print(f"[DEBUG] Sending to web_server: {payload}")  # Debug
                        
                        response = requests.post('http://localhost:5000/api/encoder_event',
                                     json=payload,
                                     timeout=2)
                        print(f"[DEBUG] Web server response: {response.status_code}")  # Debug
                    except Exception as e:
                        print(f"[ERROR] Failed to notify web_server: {e}")
            else:
                print(f"[ERROR] Unknown encoder message format: {msg}")
        except Exception as e:
            print(f"Encoder listener error: {e}")

if __name__ == "__main__":
    print("=== RPi Hub ===")
    load_calibration()
    load_instrument_mapping()
    Thread(target=heartbeat_listener, daemon=True).start()
    Thread(target=check_offline, daemon=True).start()
    Thread(target=xplane_listener, daemon=True).start()
    Thread(target=encoder_listener, daemon=True).start()  # New
    print("Ready. Press Ctrl+C to stop\n")
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nShutdown")
