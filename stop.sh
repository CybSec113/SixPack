#!/bin/bash
pkill -f "python3 web_server.py"
pkill -f "python3 rpi_hub.py"
echo "Stopped web server and RPi hub"
