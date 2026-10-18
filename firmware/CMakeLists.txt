# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Neo-Desktop
cmake_minimum_required(VERSION 3.13)

# Which board: "hood" (the DB25 hood board), "w" (pico-serial W, with the
# RM2 Bluetooth module, SPEC-W.md) or "pico" (bench rig: a Raspberry Pi Pico
# wired to the transceivers as SPEC.md 4.5).
set(PS_BOARD hood CACHE STRING "pico-serial board: hood | w | pico")
if (PS_BOARD STREQUAL "hood")
    set(PICO_BOARD pico_serial_hood)
    set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
elseif (PS_BOARD STREQUAL "w")
    set(PICO_BOARD pico_serial_w)
    set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
elseif (PS_BOARD STREQUAL "pico")
    set(PICO_BOARD pico)
else ()
    message(FATAL_ERROR "PS_BOARD must be hood, w or pico")
endif ()

# Pico SDK: PICO_SDK_PATH, or fetched at this tag.
if (NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_FETCH_FROM_GIT ON)
    set(PICO_SDK_FETCH_FROM_GIT_TAG 2.3.1)
endif ()

# TinyUSB >= 0.19 for CDC SERIAL_STATE notifications (the SDK bundles an older one).
set(PS_TINYUSB_TAG 0.21.0)
if (NOT PICO_TINYUSB_PATH AND NOT DEFINED ENV{PICO_TINYUSB_PATH})
    include(FetchContent)
    FetchContent_Declare(tinyusb GIT_REPOSITORY https://github.com/hathach/tinyusb.git
                         GIT_TAG ${PS_TINYUSB_TAG} GIT_SHALLOW TRUE)
    FetchContent_GetProperties(tinyusb)
    if (NOT tinyusb_POPULATED)
        FetchContent_Populate(tinyusb)
    endif ()
    set(PICO_TINYUSB_PATH ${tinyusb_SOURCE_DIR})
endif ()

include(pico_sdk_import.cmake)
project(pico_serial C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(PS_FW_MAJOR 0)
set(PS_FW_MINOR 1)
set(PS_FW_PATCH 0)

add_executable(pico_serial
    src/rp2040/main.c
    src/rp2040/hal_rp2040.c
    src/rp2040/usb.c
    src/core/core.c
    src/core/autobaud.c
    src/core/link.c
)
pico_generate_pio_header(pico_serial ${CMAKE_CURRENT_LIST_DIR}/src/rp2040/autobaud.pio)
target_include_directories(pico_serial PRIVATE src src/core src/rp2040)
target_compile_options(pico_serial PRIVATE -Wall -Wextra)
target_compile_definitions(pico_serial PRIVATE
    PS_FW_MAJOR=${PS_FW_MAJOR} PS_FW_MINOR=${PS_FW_MINOR} PS_FW_PATCH=${PS_FW_PATCH}
    $<$<STREQUAL:${PS_BOARD},pico>:PS_BOARD_PICO>
    $<$<STREQUAL:${PS_BOARD},w>:PS_BOARD_W>
    # picotool can reboot it (into BOOTSEL or the app) through the reset interface;
    # no "1200 baud means reboot": 1200 baud is a real modem speed
    PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
    PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0
    PICO_USB_RESET_INCLUDE_DEFAULT_APP_DRIVER_CB=0
    PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=0
)
target_link_libraries(pico_serial PRIVATE
    pico_stdlib pico_unique_id pico_usb_reset
    hardware_uart hardware_flash hardware_watchdog hardware_irq hardware_pio hardware_clocks
    tinyusb_device
)
if (PS_BOARD STREQUAL "w")
    # Bluetooth: BTstack (classic SPP + LE GATT) on the RM2, polled from the main loop
    target_sources(pico_serial PRIVATE src/rp2040/bt.c)
    pico_btstack_make_gatt_header(pico_serial PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/rp2040/pico_serial_w.gatt)
    target_link_libraries(pico_serial PRIVATE
        pico_cyw43_arch_poll pico_btstack_cyw43 pico_btstack_ble pico_btstack_classic)
    target_compile_definitions(pico_serial PRIVATE CYW43_LWIP=0)   # no Wi-Fi networking (yet)
endif ()
pico_add_extra_outputs(pico_serial)
set_target_properties(pico_serial PROPERTIES OUTPUT_NAME pico-serial-${PS_BOARD})
