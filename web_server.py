#!/usr/bin/env python3
import socket
import json
import time
import os
from datetime import datetime
from threading import Thread
from flask import Flask, render_template, jsonify, request

app = Flask(__name__)

DEBUG = True
HEARTBEAT_PORT = 49002
COMMAND_PORT = 49003
TIMEOUT = 15
CAL_FILE = 'calibrations.json'
DEVICES_FILE = 'esp_devices.json'
MAPPING_FILE = 'instrument_mapping.json'

esp_devices = {}
calibrations = {}
xplane_counters = {}
instrument_mapping = {}
encoder_events = {}  # New: Store latest encoder events
dref_data = {}  # Store DREF values and timestamps

# Instrument metadata
INSTRUMENT_METADATA = {
    'ESP_Airspeed': {'motor_count': 1, 'type': 'airspeed'},
    'ESP_AttitudeIndicator': {'motor_count': 2, 'type': 'attitude'},
    'ESP_Altimeter': {'motor_count': 2, 'type': 'altimeter'},
    'ESP_TurnIndicator': {'motor_count': 1, 'type': 'turn'},
    'ESP_Gyrocompass': {'motor_count': 2, 'type': 'heading'},
    'ESP_VertSpeed': {'motor_count': 1, 'type': 'vsi'},
    'ESP_Inputs': {'motor_count': 0, 'type': 'inputs'},
}

def load_devices():
    global esp_devices
    if os.path.exists(DEVICES_FILE):
        try:
            with open(DEVICES_FILE, 'r') as f:
                esp_devices = json.load(f)
        except:
            pass

def load_calibrations():
    global calibrations
    if os.path.exists(CAL_FILE):
        with open(CAL_FILE, 'r') as f:
            calibrations = json.load(f)

def load_instrument_mapping():
    global instrument_mapping
    if os.path.exists(MAPPING_FILE):
        try:
            with open(MAPPING_FILE, 'r') as f:
                instrument_mapping = json.load(f)
        except Exception as e:
            print(f"Error loading instrument mapping: {e}")
            instrument_mapping = {}

def save_calibrations():
    with open(CAL_FILE, 'w') as f:
        json.dump(calibrations, f, indent=2)

def heartbeat_listener():
    """Deprecated - heartbeats handled by rpi_hub.py"""
    pass

def send_command(esp_id, message):
    load_devices()
    if esp_id in esp_devices:
        ip = esp_devices[esp_id]['ip']
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.sendto(message.encode(), (ip, COMMAND_PORT))
        return True
    return False

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/health')
def health():
    """Health check - verify rpi_hub connection"""
    return jsonify({
        'status': 'ok',
        'rpi_hub_connected': len(esp_devices) > 0,
        'connected_devices': list(esp_devices.keys())
    })

@app.route('/api/device-info/<esp_id>')
def get_device_info(esp_id):
    """Return instrument metadata including motor count"""
    info = INSTRUMENT_METADATA.get(esp_id, {'motor_count': 1, 'type': 'unknown'})
    return jsonify(info)

@app.route('/api/devices')
def get_devices():
    load_devices()  # Reload from file each time
    devices = []
    now = time.time()
    
    # Add X-Plane as special entry at top
    xplane_total_messages = sum(xplane_counters.values())
    devices.append({
        'id': 'X-Plane',
        'ip': 'localhost',
        'uptime': '-',
        'last_seen': '-',
        'is_xplane': True,
        'xplane_messages': xplane_total_messages
    })
    
    # Define instrument order: top row, then bottom row, then inputs
    instrument_order = [
        'ESP_Airspeed',
        'ESP_AttitudeIndicator',
        'ESP_Altimeter',
        'ESP_TurnIndicator',
        'ESP_Gyrocompass',
        'ESP_VertSpeed',
        'ESP_Inputs',
    ]
    
    # Add all instruments in order (online or offline)
    for esp_id in instrument_order:
        if esp_id in esp_devices:
            info = esp_devices[esp_id]
            last_seen_ts = info.get('last_seen', time.time())
            uptime_str = info.get('uptime', '?')
            
            # Handle uptime: if it's a string, use it; if it's a number, convert
            if isinstance(uptime_str, (int, float)):
                uptime_str = str(int(uptime_str))
            
            # Calculate time elapsed since last seen
            elapsed = now - last_seen_ts
            hours = int(elapsed // 3600)
            minutes = int((elapsed % 3600) // 60)
            seconds = int(elapsed % 60)
            last_seen_str = f"{hours:02d}:{minutes:02d}:{seconds:02d}"
            
            device_data = {
                'id': esp_id,
                'ip': info.get('ip', '?'),
                'uptime': uptime_str,
                'last_seen': last_seen_str,
                'calibration': calibrations.get(esp_id, {}),
                'xplane_messages': xplane_counters.get(esp_id, 0),
                'is_xplane': False,
                'online': True,
                'encoders': encoder_events.copy() if esp_id == 'ESP_Inputs' else {}
            }
            
            devices.append(device_data)
        else:
            # Instrument is offline
            device_data = {
                'id': esp_id,
                'ip': '?',
                'uptime': '?',
                'last_seen': '-',
                'calibration': calibrations.get(esp_id, {}),
                'xplane_messages': 0,
                'is_xplane': False,
                'online': False,
                'encoders': encoder_events.copy() if esp_id == 'ESP_Inputs' else {}
            }
            
            devices.append(device_data)
    
    return jsonify(devices)

@app.route('/api/move', methods=['POST'])
def move_motor():
    data = request.json
    esp_id = data.get('esp_id')
    
    # Reject motor commands for Inputs ESP (has no motors)
    if esp_id == 'ESP_Inputs':
        return jsonify({'status': 'error', 'message': 'ESP_Inputs has no motors'}), 400
    
    motor_id = data.get('motor_id', 0)  # Default to motor 0
    angle = data.get('angle')
    min_angle = data.get('min_angle', 0)
    max_angle = data.get('max_angle', 360)
    
    if send_command(esp_id, f"MOVE:{motor_id}:{angle}:{min_angle}:{max_angle}"):
        return jsonify({'status': 'ok'})
    return jsonify({'status': 'error', 'message': 'ESP not found'}), 404

@app.route('/api/xplane', methods=['POST'])
def xplane_data():
    data = request.json
    field_name = data.get('field_name', '')
    value = data.get('value', 0)
    esp_id = data.get('esp_id')
    motor_id = data.get('motor_id', 0)
    
    dref_data[field_name] = {'value': value, 'timestamp': time.time()}
    
    if not esp_id:
        for instrument_name, config in instrument_mapping.get('instruments', {}).items():
            motors = config.get('motors', {})
            for mid, motor_config in motors.items():
                if motor_config.get('dref') == field_name:
                    esp_id = config.get('esp_id')
                    motor_id = int(mid)
                    break
            if esp_id:
                break
    
    if esp_id:
        xplane_counters[esp_id] = xplane_counters.get(esp_id, 0) + 1
    
    if not data.get('esp_id') and esp_id:
        send_command(esp_id, f"VALUE:{motor_id}:{int(value)}")
    
    return jsonify({'status': 'ok'})

@app.route('/api/zero', methods=['POST'])
def zero_motor():
    data = request.json
    esp_id = data.get('esp_id')
    
    # Reject zero commands for Inputs ESP (has no motors)
    if esp_id == 'ESP_Inputs':
        return jsonify({'status': 'error', 'message': 'ESP_Inputs has no motors'}), 400
    
    motor_id = data.get('motor_id', 0)  # Default to motor 0
    
    if send_command(esp_id, f"ZERO:{motor_id}"):
        return jsonify({'status': 'ok'})
    return jsonify({'status': 'error', 'message': 'ESP not found'}), 404

@app.route('/api/calibration/<esp_id>', methods=['GET', 'POST'])
def calibration(esp_id):
    if request.method == 'POST':
        data = request.json
        if esp_id not in calibrations:
            calibrations[esp_id] = {}
        calibrations[esp_id]['points'] = data.get('points', [])
        calibrations[esp_id]['min_angle'] = data.get('min_angle', 0)
        calibrations[esp_id]['max_angle'] = data.get('max_angle', 360)
        save_calibrations()
        
        # Send calibration to ESP32 via UDP
        cal_json = json.dumps(calibrations[esp_id])
        send_command(esp_id, f"CAL:{cal_json}")
        
        return jsonify({'status': 'ok'})
    
    cal = calibrations.get(esp_id, {})
    return jsonify(cal)

@app.route('/api/calibration/<esp_id>/json')
def calibration_json(esp_id):
    cal = calibrations.get(esp_id, {})
    return jsonify(cal)

@app.route('/api/drefs')
def get_drefs():
    now = time.time()
    return jsonify([{'dref': d, 'value': v['value'], 'elapsed': now - v['timestamp']} for d, v in dref_data.items()])

@app.route('/api/calibration/<esp_id>/point', methods=['POST'])
def add_calibration_point(esp_id):
    if esp_id not in calibrations:
        calibrations[esp_id] = {'points': []}
    if 'points' not in calibrations[esp_id]:
        calibrations[esp_id]['points'] = []
    
    point = request.json
    calibrations[esp_id]['points'].append(point)
    calibrations[esp_id]['points'].sort(key=lambda p: p['value'])
    save_calibrations()
    return jsonify({'status': 'ok'})

@app.route('/api/calibration/<esp_id>/point/<int:idx>', methods=['DELETE'])
def delete_calibration_point(esp_id, idx):
    if esp_id in calibrations and 'points' in calibrations[esp_id]:
        if 0 <= idx < len(calibrations[esp_id]['points']):
            calibrations[esp_id]['points'].pop(idx)
            save_calibrations()
            return jsonify({'status': 'ok'})
    return jsonify({'status': 'error'}), 404

@app.route('/api/encoder_event', methods=['POST'])
def encoder_event():
    """Receive encoder events from Inputs ESP via rpi_hub"""
    global encoder_events
    data = request.json
    encoder_name = data.get('encoder')
    value = data.get('value')
    button = data.get('button')
    
    if encoder_name:
        encoder_events[encoder_name] = {
            'value': value,
            'button': button,
            'timestamp': time.time()
        }
        print(f"[ENCODER] {encoder_name}: value={value}, button={button}")
        print(f"[DEBUG] encoder_events now: {encoder_events}")  # Debug
    
    return jsonify({'status': 'ok'})

if __name__ == '__main__':
    import logging
    log = logging.getLogger('werkzeug')
    log.setLevel(logging.INFO)
    
    load_calibrations()
    load_instrument_mapping()
    
    app.run(host='0.0.0.0', port=5000, debug=True)
