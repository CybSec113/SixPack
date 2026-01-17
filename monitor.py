"""
Use this file for monitoring airspeed DREF messages from x-plane
and translating airspeed value to NEMA 11 stepper motor rotation
through DRV8825 stepper motor controller.
"""
import sys
import socket
import struct
import RPi.GPIO as GPIO
import time

# GPIO configuration
DIR_PIN = 20  # Direction: CW=0, CCW=1
STEP_PIN = 21  # Step pulses
MODE_PINS = (14, 15, 18)  # Microstep resolution control pins

# Stepper motor configuration
SPR = 200  # Steps per Revolution (360 / 1.8)
CW = 0
CCW = 1

# Resolution modes
RESOLUTION = {
    'Full': (0, 0, 0),
    'Half': (1, 0, 0),
    '1/4': (0, 1, 0),
    '1/8': (1, 1, 0),
    '1/16': (0, 0, 1),
    '1/32': (1, 0, 1),
}

CURRENT_RESOLUTION = '1/8'  # Change this to adjust sensitivity

# GPIO initialization
GPIO.setmode(GPIO.BCM)
GPIO.setup(MODE_PINS, GPIO.OUT)
GPIO.setup(DIR_PIN, GPIO.OUT)
GPIO.setup(STEP_PIN, GPIO.OUT)
GPIO.output(MODE_PINS, RESOLUTION[CURRENT_RESOLUTION])
GPIO.output(DIR_PIN, GPIO.LOW)  # Initialize to CW
GPIO.output(STEP_PIN, GPIO.LOW)

# Stepper motor configuration
MICROSTEP_FACTOR = {
    'Full': 1,
    'Half': 2,
    '1/4': 4,
    '1/8': 8,
    '1/16': 16,
    '1/32': 32,
}
STEP_ANGLE = 1.8 / MICROSTEP_FACTOR[CURRENT_RESOLUTION]  # degrees per step

# Airspeed dial calibration
AIRSPEED_40KT_ANGLE = 30  # degrees (1 pm position)
AIRSPEED_200KT_ANGLE = 330  # degrees (11 pm position)
AIRSPEED_MIN = 40  # knots
AIRSPEED_MAX = 200  # knots

# Motor state
current_position = 0  # current position in steps (0 = 0 degrees)

def airspeed_to_angle(airspeed_knots):
    """
    Convert true airspeed to dial angle.
    Linear interpolation between calibration points.
    """
    # Linear mapping: 40 knots = 30°, 200 knots = 330°
    angle = AIRSPEED_40KT_ANGLE + (airspeed_knots - AIRSPEED_MIN) / (AIRSPEED_MAX - AIRSPEED_MIN) * (AIRSPEED_200KT_ANGLE - AIRSPEED_40KT_ANGLE)
    return angle

def angle_to_steps(angle_degrees):
    """
    Convert dial angle to stepper motor steps.
    Normalizes to 0-360° range.
    """
    # Normalize angle to 0-360°
    angle_degrees = angle_degrees % 360
    steps = round(angle_degrees / STEP_ANGLE)
    return steps

def move_stepper(target_steps):
    """
    Move stepper motor to target position using GPIO 20 (direction) and 21 (steps).
    Direction: 0 = CW (clockwise), 1 = CCW (counter-clockwise)
    Always chooses the shortest path.
    """
    global current_position
    
    # Normalize both positions to single rotation (0 to steps per rotation)
    steps_per_rotation = round(360 / STEP_ANGLE)
    target_steps = target_steps % steps_per_rotation
    current_normalized = current_position % steps_per_rotation
    
    steps_to_move = target_steps - current_normalized
    
    # Calculate shortest path on circular dial
    if steps_to_move > steps_per_rotation / 2:
        steps_to_move -= steps_per_rotation
    elif steps_to_move < -steps_per_rotation / 2:
        steps_to_move += steps_per_rotation
    
    if steps_to_move == 0:
        return
    
    # Set direction: CW=0, CCW=1
    direction = CCW if steps_to_move < 0 else CW
    GPIO.output(DIR_PIN, direction)
    
    steps_to_move = abs(steps_to_move)
    
    # Send step pulses
    for _ in range(steps_to_move):
        GPIO.output(STEP_PIN, GPIO.HIGH)
        time.sleep(0.001)  # 1ms pulse width
        GPIO.output(STEP_PIN, GPIO.LOW)
        time.sleep(0.001)  # 1ms delay between steps
    
    current_position = target_steps

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
    decimal_value = struct.unpack('<f', decimal_bytes)[0]  # Little-endian 
    
    # Extract field name (rest of the data after position 8)
    field_name = data_stripped[9:].decode('ascii', errors='replace')
    
    return {
        'data': data_stripped,
        'decimal': decimal_value,
        'field_name': field_name
    }

def capture_xplane_data(port=49001, host='0.0.0.0'):
    """
    Capture and print DREF messages from X-Plane on the specified port.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    
    print(f"Listening for X-Plane data on {host}:{port}")
    print("Initializing motor to 0 degrees...")
    move_stepper(0)  # Move to home position
    print("Press Ctrl+C to stop...\n")
    
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
                print(f"Airspeed: {parsed['decimal']:.2f} knots")
                print(f"Field Name: {parsed['field_name']}")
                
                # Convert airspeed to motor steps and move
                angle = airspeed_to_angle(parsed['decimal'])
                target_steps = angle_to_steps(angle)
                print(f"Target Angle: {angle:.2f}°, Motor Steps: {target_steps}")
                move_stepper(target_steps)
            else:
                # Fallback for non-DREF messages
                data_stripped = data.rstrip(b'\x00')
                print("Message Type: Unknown")
                print(f"Size: {len(data)} bytes")
                print(f"Hex: {data_stripped.hex()}")
                print(f"Raw: {data_stripped}")
            print()
    except KeyboardInterrupt:
        print("\nStopped listening.")
    finally:
        GPIO.cleanup()
        sock.close()
        sock.close()


if __name__ == "__main__":
    capture_xplane_data()
