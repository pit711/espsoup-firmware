cmake_minimum_required(VERSION 3.16)

# S2 uses the ROM USB-OTG CDC driver; keep its pinned compatibility fix
# scoped to this target so other chips retain their SDK components.
if(IDF_TARGET STREQUAL "esp32s2")
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/platform/esp32s2/esp_usb_cdc_rom_console")
endif()

option(ESP_SDR_STREAMING "Build the S31 Ethernet/high-speed USB receiver" OFF)
set(ESP_SDR_C5_ANTENNA "INTERNAL" CACHE STRING
    "Waveshare ESP32-C5-Zero antenna: INTERNAL, EXTERNAL, or UNMANAGED")
set_property(CACHE ESP_SDR_C5_ANTENNA PROPERTY STRINGS INTERNAL EXTERNAL UNMANAGED)
string(TOUPPER "${ESP_SDR_C5_ANTENNA}" ESP_SDR_C5_ANTENNA)
if(NOT ESP_SDR_C5_ANTENNA MATCHES "^(INTERNAL|EXTERNAL|UNMANAGED)$")
    message(FATAL_ERROR
        "ESP_SDR_C5_ANTENNA must be INTERNAL, EXTERNAL, or UNMANAGED")
endif()
if(ESP_SDR_STREAMING)
    if(NOT IDF_TARGET STREQUAL "esp32s31")
        message(FATAL_ERROR "ESP_SDR_STREAMING requires ESP32-S31")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/main/targets/esp32s31/streaming")
else()
    set(ENV{IDF_COMPONENT_MANAGER} "0")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(ESP_SDR_STREAMING "${ESP_SDR_STREAMING}")
project(esp_sdr)
