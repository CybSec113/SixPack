#!/usr/bin/python

"""
Monitor airspeed DREF messages from X-Plane and control a 28BYJ-48 stepper motor.
"""

import sys
import socket
import struct
import time
import os
import requests
from stepper_28byj import Stepper28BYJ48, CW, CCW

# Detect if running in Docker
IN_DOCKER = os.path.exists('/.dockerenv')

# 28BYJ-48 motor configuration
MOTOR_DELAY = 0.002  # 2ms delay between steps
MOTOR_RESOLUTION = 'full'  # 'full' or 'half'

# Calibration points will be loaded from web server
CALIBRATION_POINTS = []

def load_calibration():
    try:
        r = requests.get('http://localhost:5000/api/calibration/ESP_Airspeed', timeout=1)
        if r.status_code == 200:
            cal = r.json()
            if 'points' in cal:
                return [(p['value'], p['angle']) for p in cal['points']]
    except:
        pass
    return [(70, 90), (110, 180), (165, 270)]

def airspeed_to_angle(airspeed_knots):
    """
    Convert indicated airspeed to dial angle using calibration points.
    """
    if not CALIBRATION_POINTS:
        return 90
    if airspeed_knots <= CALIBRATION_POINTS[0][0]:
        k1, a1 = CALIBRATION_POINTS[0]
        k2, a2 = CALIBRATION_POINTS[1]
        return a1 + (airspeed_knots - k1) * (a2 - a1) / (k2 - k1)
    if airspeed_knots >= CALIBRATION_POINTS[-1][0]:
        k1, a1 = CALIBRATION_POINTS[-2]
        k2, a2 = CALIBRATION_POINTS[-1]
        return a2 + (airspeed_knots - k2) * (a2 - a1) / (k2 - k1)
    for i in range(len(CALIBRATION_POINTS) - 1):
        k1, a1 = CALIBRATION_POINTS[i]
        k2, a2 = CALIBRATION_POINTS[i + 1]
        if k1 <= airspeed_knots <= k2:
            return a1 + (airspeed_knots - k1) * (a2 - a1) / (k2 - k1)
    return CALIBRATION_POINTS[0][1]

def parse_dref_message(data):
    """
    Parse DREF message format: DREF + 4 bytes (decimal) + field name
    """
    data_stripped = data.rstrip(b'\x00')
    
    if len(data_stripped) < 9:  # "DREF" (4) + 4 bytes + at least 1 char for name
        return None
    
    # Check for DREF header
    if data_stripped[:5] != b'DREF+':
        return None
    
    # Extract 4-byte decimal value
    decimal_bytes = data_stripped[5:9]
    decimal_value = struct.unpack('<f', decimal_bytes)[0]  # Little-endian float
    
    # Extract field name (rest of the data after position 8)
    field_name = data_stripped[9:].decode('ascii', errors='replace')
    
    return {
        'data': data_stripped,
        'decimal': decimal_value,
        'field_name': field_name
    }

def capture_xplane_data(port=49001, host='0.0.0.0'):
    """
    Capture and parse DREF messages from X-Plane and control 28BYJ stepper motor.
    """
    global CALIBRATION_POINTS
    CALIBRATION_POINTS = load_calibration()
    print(f"Loaded calibration points: {CALIBRATION_POINTS}")
    
    # Initialize motor
    print(f"Initializing 28BYJ-48 stepper motor (resolution: {MOTOR_RESOLUTION})...")
    if IN_DOCKER:
        print("[DOCKER MODE] GPIO output disabled, using stdout")
    motor = Stepper28BYJ48(delay=MOTOR_DELAY, resolution=MOTOR_RESOLUTION)
    
    # Move to home position (0 degrees)
    print("Moving to home position (0 degrees)...")
    motor.move_to(0)
    
    # Initialize UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    
    print(f"\nListening for X-Plane data on {host}:{port}")
    print("Press Ctrl+C to stop...\n")
    
    last_airspeed = None
    try:
        packet_num = 0
        while True:
            data, addr = sock.recvfrom(4096)
            packet_num += 1
            
            parsed = parse_dref_message(data)
            
            print(f"--- Packet #{packet_num} ---")
            print(f"Source: {addr}")

            if parsed:
                print(f"Message Type: DREF")
                airspeed = parsed['decimal']
                print(f"Airspeed: {airspeed:.2f} knots")
                print(f"Field Name: {parsed['field_name']}")
                
                # Only move if airspeed changed by more than 1 knot
                if last_airspeed is None or abs(airspeed - last_airspeed) > 1:
                    last_airspeed = airspeed
                    
                    # Convert airspeed to motor angle and move
                    angle = airspeed_to_angle(airspeed)
                    print(f"Target Angle: {angle:.2f}°")
                    
                    if IN_DOCKER:
                        print(f"[MOTOR] Move to {angle:.2f}°")
                    else:
                        motor.move_to(angle)
                        print(f"Current Position: {motor.get_position():.2f}°")
                
                # Send to web server
                try:
                    requests.post('http://localhost:5000/api/xplane', json={'field_name': 'sim/flightmodel/position/indicated_airspeed', 'value': airspeed}, timeout=1)
                except Exception as e:
                    print(f"Failed to send to web server: {e}")
            else:
                # Fallback for non-DREF messages
                data_stripped = data.rstrip(b'\x00')
                print("Message Type: Unknown")
                print(f"Size: {len(data)} bytes")
            print()
            
    except KeyboardInterrupt:
        print("\nStopped listening.")
    finally:
        motor.move_to(0)
        motor.cleanup()
        sock.close()


if __name__ == "__main__":
    capture_xplane_data()
