# Reference transmitter for the ESP-NOW control link of ESPDrone.
# Build it as its own project:  cd examples/espnow_transmitter && idf.py set-target esp32 build
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espnow_transmitter)
