/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 * Copyright 2020 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_EMUL_H_
#define ZEPHYR_INCLUDE_DRIVERS_EMUL_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/**
 * @brief Emulators used to test drivers and higher-level code that uses them
 * @defgroup io_emulators Emulator interface
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct emul;

/**
 * Structure uniquely identifying a device to be emulated
 *
 * Currently this uses the device node label, but that will go away by 2.5.
 */
struct emul_link_for_bus {
	const char *label;
};

/** List of emulators attached to a bus */
struct emul_list_for_bus {
	/** Identifiers for children of the node */
	const struct emul_link_for_bus *children;
	/** Number of children of the node */
	unsigned int num_children;
};

/**
 * Standard callback for emulator initialisation providing the initialiser
 * record and the device that calls the emulator functions.
 *
 * @param emul Emulator to init
 * @param parent Parent device that is using the emulator
 */
typedef int (*emul_init_t)(const struct emul *emul,
			   const struct device *parent);

/** An emulator instance */
struct emul {
	/** function used to initialise the emulator state */
	emul_init_t init;
	/** handle to the device for which this provides low-level emulation */
	const char *dev_label;
	/** Emulator-specific configuration data */
	const void *cfg;
	/** Emulator-specific data */
	void *data;
};

/**
 * Emulators are aggregated into an array at link time, from which emulating
 * devices can find the emulators that they are to use.
 */
extern const struct emul __emul_list_start[];
extern const struct emul __emul_list_end[];

/**
 * @brief Use the devicetree node identifier as a unique name.
 */
#define EMUL_DT_NAME_GET(node_id) (_CONCAT(__emulreg_, node_id))

/**
 * @brief Use the devicetree node identifier as a unique name.
 *
 * @deprecated Use EMUL_DT_NAME_GET instead
 */
#define EMUL_REG_NAME(node_id) (_CONCAT(__emulreg_, node_id))

/**
 * @brief Define a new emulator
 *
 * This adds a new struct emul to the linker list of emulations. This is
 * typically used in your emulator's DT_INST_FOREACH_STATUS_OKAY() clause.
 *
 * @param node_id Node ID of the driver to emulate (e.g. DT_DRV_INST(n))
 * @param init_fn function to call to initialise the emulator (see emul_init
 *	typedef)
 * @param data_ptr emulator-specific data
 * @param cfg_ptr emulator-specific configuration data
 */
#define EMUL_DT_DEFINE(node_id, init_fn, data_ptr, cfg_ptr)                                        \
	const struct emul EMUL_DT_NAME_GET(node_id)                                                \
	__attribute__((__section__(".emulators")))                                                 \
	__used = {                                                                                 \
		.init = (init_fn),                                                                 \
		.dev_label = DT_LABEL(node_id),                                                    \
		.cfg = (cfg_ptr),                                                                  \
		.data = (data_ptr),                                                                \
	};

/**
 * @brief Define a new emulator
 *
 * Prefer EMUL_DT_DEFINE
 */
#define EMUL_DEFINE(init_ptr, node_id, cfg_ptr, data_ptr) \
	EMUL_DT_DEFINE(node_id, init_ptr, data_ptr, cfg_ptr)

/**
 * @def EMUL_DT_INST_DEFINE
 *
 * @brief Like EMUL_DT_DEFINE(), but uses an instance of a
 * DT_DRV_COMPAT compatible instead of a node identifier.
 *
 * @param inst instance number. The @p node_id argument to
 * EMUL_DT_DEFINE is set to <tt>DT_DRV_INST(inst)</tt>.
 *
 * @param ... other parameters as expected by DEVICE_DT_DEFINE.
 */
#define EMUL_DT_INST_DEFINE(inst, ...) \
	EMUL_DT_DEFINE(DT_DRV_INST(inst), __VA_ARGS__)

/**
 * @def EMUL_DT_GET
 *
 * @brief Get a <tt>const struct emul*</tt> from a devicetree node identifier
 *
 * @details Returns a pointer to an emulator object created from a devicetree
 * node, if any device was allocated by an emulator implementation.
 *
 * If no such device was allocated, this will fail at linker time. If you get an
 * error that looks like <tt>undefined reference to __device_dts_ord_<N></tt>,
 * that is what happened. Check to make sure your emulator implementation is
 * being compiled, usually by enabling the Kconfig options it requires.
 *
 * @param node_id A devicetree node identifier
 * @return A pointer to the emul object created for that node
 */
#define EMUL_DT_GET(node_id) (&EMUL_DT_NAME_GET(node_id))

/**
 * Set up a list of emulators
 *
 * @param dev Device the emulators are attached to (e.g. an I2C controller)
 * @param list List of devices to set up
 * @return 0 if OK
 * @return negative value on error
 */
int emul_init_for_bus_from_list(const struct device *dev,
				const struct emul_list_for_bus *list);

/**
 * @brief Retrieve the emul structure for an emulator by name
 *
 * @details Emulator objects are created via the EMUL_DEFINE() macro and placed in memory by the
 * linker. If the emulator structure is needed for custom API calls, it can be retrieved by the name
 * that the emulator exposes to the system (this is the devicetree node's label by default).
 *
 * @param name Emulator name to search for.  A null pointer, or a pointer to an
 * empty string, will cause NULL to be returned.
 *
 * @return pointer to emulator structure; NULL if not found or cannot be used.
 */
const struct emul *emul_get_binding(const char *name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_EMUL_H_ */
