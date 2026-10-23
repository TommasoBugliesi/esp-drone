# USB-to-ESP-NOW bridge for the ESPDrone control link: a laptop or phone sends control frames over
# USB serial, the bridge forwards them to the drone and returns the drone's status and telemetry.
# Build it as its own project:  cd examples/espnow_bridge && idf.py set-target esp32 build
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espnow_bridge)
