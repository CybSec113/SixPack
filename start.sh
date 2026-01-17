#!/bin/bash
cd "$(dirname "$0")"
python3 web_server.py > /dev/null 2>&1 &
python3 rpi_hub.py > /dev/null 2>&1 &
echo "Web server and RPi hub started in background"
echo "View logs with: tail -f web_server.log rpi_hub.log"
