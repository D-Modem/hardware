# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Neo-Desktop
# Host tests for the portable core: make -C firmware/test
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion -fsanitize=address,undefined
CORE = ../src/core/core.c ../src/core/autobaud.c
DEPS = $(CORE) $(wildcard ../src/core/*.h) fake_hal.h wave.h wave.c

all: test_core test_autobaud

test_core: test_core.c fake_hal.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ test_core.c fake_hal.c wave.c $(CORE) -lm

test_autobaud: test_autobaud.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ test_autobaud.c wave.c ../src/core/autobaud.c -lm

.PHONY: all run clean
run: all
	./test_core
	./test_autobaud

clean:
	rm -f test_core test_autobaud
