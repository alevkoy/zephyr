/*
 * Copyright (c) 2022 Google Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT vnd_gpio_device

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>

static int emul_init(const struct emul *dev_emul, const struct device *parent)
{
	return 0;
}

/*
 * This emulator definition is deliberately in a different compilation unit than
 * test_emuL_dt_get to verify that emulators are externally accessible.
 */
#define EMUL_INIT(n) EMUL_DT_INST_DEFINE(n, emul_init, NULL, NULL)
DT_INST_FOREACH_STATUS_OKAY(EMUL_INIT)
