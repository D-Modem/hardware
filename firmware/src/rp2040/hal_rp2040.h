/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2026 D-Modem Authors
 */
#pragma once
// Sets every board pin to a safe state (transceivers shut down, outputs
// deasserted, LEDs off), starts the UART and its interrupt, and loads the
// baud-learning PIO program. Call first in main(), before core_init().
void hal_init(void);
