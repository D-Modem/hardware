/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
// pico-serial Line, the phone-line stick (SPEC-L.md): bare RP2040, W25Q128JV,
// 12 MHz ABM8-272-T3, Si3056 + Si3018 DAA.
#ifndef _BOARDS_PICO_SERIAL_LINE_H
#define _BOARDS_PICO_SERIAL_LINE_H

pico_board_cmake_set(PICO_PLATFORM, rp2040)

#define PICO_SERIAL_LINE

// No UART/I2C/SPI defaults; LEDs are the firmware's (OH, RI).

// W25Q128JV works with the generic W25Q080 boot stage 2
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// SPEC.md 4.2: margin for the crystal to start
#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif

#ifndef PICO_RP2040_B0_SUPPORTED
#define PICO_RP2040_B0_SUPPORTED 0
#endif

#endif
