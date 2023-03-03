/*
 * Copyright (c) 2022  The Chromium OS Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_MOCK_TESTER_H_
#define ZEPHYR_MOCK_TESTER_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/usb_c/usbc_tc.h>

#include "mock_tcpc.h"

void tester_disconnected(void);
void tester_apply_cc(enum tc_cc_voltage_state cc1,
		enum tc_cc_voltage_state cc2);
void tester_apply_rp(enum tc_rp_value cc1, enum tc_rp_value cc2);
enum tc_cc_voltage_state tester_get_cc1(void);
enum tc_cc_voltage_state tester_get_cc2(void);
void tester_apply_vbus_level(enum tc_vbus_level level);
void tester_apply_vbus(int mv);
int tester_get_vbus(void);
void tester_send_msg_to_uut(struct pd_msg *msg);
void tester_get_msg_from_uut(struct pd_msg *msg);
void tester_set_tcpc_device(struct mock_tcpc_data *tcpc);
void tester_send_ctrl_msg(enum pd_ctrl_msg_type type, bool inc_msgid);
void tester_send_data_msg(enum pd_data_msg_type type, uint32_t *data, uint32_t len, bool inc_msgid);
void tester_set_rev_pd2(void);
void tester_set_rev_pd3(void);
enum pd_rev_type tester_get_rev(void);
void tester_set_power_role_source(void);
void tester_set_data_role_ufp(void);
void tester_set_data_role_dfp(void);
void tester_msgid_inc(void);
void tester_get_uut_tx_data(struct pd_msg *msg);
int tester_get_msgid(void);
bool tester_is_rx_msg_pending(void);
void tester_send_hard_reset(void);

void tester_start_sender_response_timer(void);

void start_policy_timer(void);
#endif /* ZEPHYR_MOCK_TESTER_H_ */
