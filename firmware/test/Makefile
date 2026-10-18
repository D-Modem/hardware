# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Neo-Desktop
# Host tests for the portable core: make -C firmware/test
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion -fsanitize=address,undefined

test_core: test_core.c fake_hal.c ../src/core/core.c ../src/core/core.h ../src/core/hal.h ../src/core/ring.h ../src/core/protocol.h fake_hal.h
	$(CC) $(CFLAGS) -o $@ test_core.c fake_hal.c ../src/core/core.c

.PHONY: run clean
run: test_core
	./test_core

clean:
	rm -f test_core
