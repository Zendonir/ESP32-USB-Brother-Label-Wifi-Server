# Brother TD-2020A Wi-Fi print server for ESP32-S3
# Phase 2: USB diagnostic firmware
cmake_minimum_required(VERSION 3.16)

# CI passes the version (e.g. release tag) via the FW_VERSION environment variable.
if(DEFINED ENV{FW_VERSION} AND NOT "$ENV{FW_VERSION}" STREQUAL "")
    set(PROJECT_VER "$ENV{FW_VERSION}")
else()
    set(PROJECT_VER "0.2.2-diag")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(brother_printserver)
