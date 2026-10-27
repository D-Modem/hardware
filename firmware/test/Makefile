# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Neo-Desktop
# Host tests for the portable core: make -C firmware/test
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion -fsanitize=address,undefined
CORE = ../src/core/core.c ../src/core/autobaud.c
DEPS = $(CORE) $(wildcard ../src/core/*.h) fake_hal.h wave.h wave.c

all: test_core test_autobaud test_link test_line test_wb

test_core: test_core.c fake_hal.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ test_core.c fake_hal.c wave.c $(CORE) -lm

test_autobaud: test_autobaud.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ test_autobaud.c wave.c ../src/core/autobaud.c -lm

test_link: test_link.c fake_hal.c ../src/core/link.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ test_link.c fake_hal.c ../src/core/link.c $(CORE) -lm

LINE = ../src/core/daa_frame.c ../src/core/sstream.c ../src/core/line_dev.c
test_line: test_line.c $(LINE) $(wildcard ../src/core/*.h)
	$(CC) $(CFLAGS) -o $@ test_line.c $(LINE)

WB = ../src/core/sstream.c ../src/core/wb_audio.c ../src/core/wb_codec.c ../src/core/wb_dev.c
test_wb: test_wb.c $(WB) $(wildcard ../src/core/*.h)
	$(CC) $(CFLAGS) -DSFIFO_SIZE=4096u -o $@ test_wb.c $(WB)

.PHONY: all run clean
run: all
	./test_core
	./test_autobaud
	./test_link
	./test_line
	./test_wb

clean:
	rm -f test_core test_autobaud test_link test_line test_wb
