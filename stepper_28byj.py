#!/usr/bin/python

"""
Stepper28BYJ48 is a low-level class to control the stepper motor.
Use this first to make sure stepper motor can be controlled from 
the Raspberry Pi (or other).
"""

import time
import sys
import os

# Detect if running in Docker
IN_DOCKER = os.path.exists('/.dockerenv')

if not IN_DOCKER:
    import RPi.GPIO as GPIO

# GPIO Pins for 28BYJ-48 stepper motor
IN1 = 17
IN2 = 27
IN3 = 22
IN4 = 23

# Motor constants
FULL_STEPS_PER_REVOLUTION = 2048   # 28BYJ-48 full step mode
HALF_STEPS_PER_REVOLUTION = 4096   # 28BYJ-48 half step mode (default)

# Resolution modes and their steps per revolution
RESOLUTIONS = {
    'full': FULL_STEPS_PER_REVOLUTION,
    'half': HALF_STEPS_PER_REVOLUTION,
}

# Sequences for different resolutions
SEQUENCE_HALF = [  # 8-step half stepping (default)
    [1, 0, 0, 0],
    [1, 1, 0, 0],
    [0, 1, 0, 0],
    [0, 1, 1, 0],
    [0, 0, 1, 0],
    [0, 0, 1, 1],
    [0, 0, 0, 1],
    [1, 0, 0, 1],
]

SEQUENCE_FULL = [  # 4-step full stepping
    [1, 1, 0, 0],
    [0, 1, 1, 0],
    [0, 0, 1, 1],
    [1, 0, 0, 1],
]

CW = 1   # Clockwise
CCW = -1 # Counterclockwise


class Stepper28BYJ48:
    """Control class for 28BYJ-48 stepper motor"""

    def __init__(self, delay=0.001, resolution='half'):
        """
        Initialize the stepper motor.
        
        Args:
            delay: Time (in seconds) between steps. Default 0.001 (1ms)
            resolution: Stepping resolution - 'full' or 'half' (default)
        """
        # Initialize GPIO (skip in Docker)
        if not IN_DOCKER:
            GPIO.setmode(GPIO.BCM)
            GPIO.setwarnings(False)
            GPIO.setup([IN1, IN2, IN3, IN4], GPIO.OUT)
            GPIO.output([IN1, IN2, IN3, IN4], GPIO.LOW)

        self.delay = delay
        self.resolution = resolution.lower()
        if self.resolution not in RESOLUTIONS:
            raise ValueError(f"Invalid resolution: {self.resolution}. Choose from: {list(RESOLUTIONS.keys())}")
        
        self.steps_per_revolution = RESOLUTIONS[self.resolution]
        self.sequence = SEQUENCE_FULL if self.resolution == 'full' else SEQUENCE_HALF
        
        self.current_position = 0.0  # Current position in degrees
        self.sequence_index = 0      # Current position in coil sequence

    def _set_coils(self, sequence):
        """Activate coils according to the given sequence"""
        if not IN_DOCKER:
            GPIO.output(IN1, sequence[0])
            GPIO.output(IN2, sequence[1])
            GPIO.output(IN3, sequence[2])
            GPIO.output(IN4, sequence[3])

    def rotate(self, direction, degrees):
        """
        Rotate the motor by a specified number of degrees.
        
        Args:
            direction: CW (1) for clockwise, CCW (-1) for counterclockwise
            degrees: Number of degrees to rotate (positive value)
        """
        # Convert degrees to steps (always based on physical degrees)
        steps = int(abs(degrees) / (360.0 / self.steps_per_revolution))
        
        # Determine direction in sequence
        step_direction = direction  # CW=1, CCW=-1

        print(f"Rotating {degrees}° ({steps} steps, {self.resolution} step mode) {'CW' if direction == CW else 'CCW'}...")

        for _ in range(steps):
            # Move to next position in sequence
            self.sequence_index = (self.sequence_index + step_direction) % len(self.sequence)
            self._set_coils(self.sequence[self.sequence_index])
            time.sleep(self.delay)

        # Update position
        self.current_position += degrees if direction == CW else -degrees
        self.current_position = self.current_position % 360  # Keep within 0-360

        print(f"Current position: {self.current_position:.2f}°")

    def move_to(self, target_degrees):
        """
        Move to an absolute position (in degrees from start).
        
        Args:
            target_degrees: Target position in degrees (0-360)
        """
        target_degrees = target_degrees % 360
        current = self.current_position % 360

        # Calculate shortest path
        diff = target_degrees - current
        if diff > 180:
            diff -= 360
        elif diff < -180:
            diff += 360

        direction = CW if diff >= 0 else CCW
        self.rotate(direction, abs(diff))

    def set_speed(self, delay):
        """
        Set motor speed by adjusting delay between steps.
        
        Args:
            delay: Time in seconds between steps (lower = faster)
        """
        self.delay = delay

    def stop(self):
        """Stop the motor and release coils"""
        if not IN_DOCKER:
            GPIO.output([IN1, IN2, IN3, IN4], GPIO.LOW)

    def cleanup(self):
        """Clean up GPIO resources"""
        self.stop()
        if not IN_DOCKER:
            GPIO.cleanup()

    def get_position(self):
        """Return current position in degrees"""
        return self.current_position % 360


if __name__ == "__main__":
    """
    Command-line usage:
    python 28byj.py CW 90                    # Rotate 90° clockwise (half step)
    python 28byj.py CCW 45 full              # Rotate 45° counterclockwise (full step)
    python 28byj.py goto 180 half            # Move to absolute position 180° (half step)
    """
    try:
        resolution = 'half'
        delay = 0.002
        
        # Parse arguments
        if len(sys.argv) < 2:
            print("Usage:")
            print("  python 28byj.py CW <degrees> [resolution]      - Rotate clockwise")
            print("  python 28byj.py CCW <degrees> [resolution]     - Rotate counterclockwise")
            print("  python 28byj.py goto <degrees> [resolution]    - Move to absolute position")
            print("\nResolution options: 'full' (faster) or 'half' (smoother, default)")
            sys.exit(1)

        command = sys.argv[1].upper()
        
        # Check for resolution argument (last argument)
        if len(sys.argv) > 3 and sys.argv[-1].lower() in ['full', 'half']:
            resolution = sys.argv[-1].lower()
        
        motor = Stepper28BYJ48(delay=delay, resolution=resolution)
        
        if command in ['CW', 'CCW']:
            degrees = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
            direction = CW if command == 'CW' else CCW
            motor.rotate(direction, degrees)
        
        elif command == 'GOTO':
            target = float(sys.argv[2]) if len(sys.argv) > 2 else 0
            motor.move_to(target)
        
        else:
            print(f"Unknown command: {command}")
            sys.exit(1)

    except KeyboardInterrupt:
        print("\nInterrupted by user")
    finally:
        motor.cleanup()
