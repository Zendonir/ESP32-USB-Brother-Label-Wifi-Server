# Brother TD-2020A Wi-Fi print server for ESP32-S3
# Phase 2: USB diagnostic firmware
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.2.0-diag")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(brother_printserver)
