#!/usr/bin/env python3
"""Diagnostic tool to check device connectivity and configuration"""
import json
import os
import socket
import time

DEVICES_FILE = 'esp_devices.json'
MAPPING_FILE = 'instrument_mapping.json'

def load_json(filename):
    if os.path.exists(filename):
        try:
            with open(filename, 'r') as f:
                return json.load(f)
        except Exception as e:
            print(f"Error loading {filename}: {e}")
            return {}
    return {}

def check_device_connectivity(ip, port=49003, timeout=2):
    """Check if a device is reachable on the network"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        sock.sendto(b'PING', (ip, port))
        sock.close()
        return True
    except:
        return False

def main():
    print("=== Device Diagnostic ===\n")
    
    # Load configurations
    devices = load_json(DEVICES_FILE)
    mapping = load_json(MAPPING_FILE)
    
    # Get all configured instruments
    configured = set(mapping.get('instruments', {}).keys())
    online = set(devices.keys())
    offline = configured - online
    
    print(f"Configured instruments: {len(configured)}")
    print(f"Online devices: {len(online)}")
    print(f"Offline devices: {len(offline)}\n")
    
    if online:
        print("ONLINE DEVICES:")
        for esp_id, info in devices.items():
            ip = info.get('ip', '?')
            uptime = info.get('uptime', '?')
            last_seen = info.get('last_seen', 0)
            elapsed = time.time() - last_seen
            print(f"  ✓ {esp_id}")
            print(f"    IP: {ip}, Uptime: {uptime}s, Last seen: {elapsed:.1f}s ago")
    
    if offline:
        print("\nOFFLINE DEVICES:")
        for esp_id in sorted(offline):
            config = mapping.get('instruments', {}).get(esp_id, {})
            motors = len(config.get('motors', {}))
            print(f"  ✗ {esp_id} ({motors} motors)")
            print(f"    Status: Not sending heartbeats")
            print(f"    Action: Check WiFi connection, verify firmware flashed")
    
    print("\nDEVICE DETAILS:")
    for esp_id in sorted(configured):
        config = mapping.get('instruments', {}).get(esp_id, {})
        motors = config.get('motors', {})
        status = "ONLINE" if esp_id in online else "OFFLINE"
        print(f"\n{esp_id} [{status}]")
        print(f"  Motors: {len(motors)}")
        for mid, motor_config in motors.items():
            dref = motor_config.get('dref', 'N/A')
            print(f"    Motor {mid}: {dref}")

if __name__ == '__main__':
    main()
