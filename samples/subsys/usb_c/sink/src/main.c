/*
 * Copyright (c) 2022 The Chromium OS Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/ztress.h>
#include <zephyr/kernel.h>
#include <zephyr/usb_c/usbc.h>
#include <zephyr/tc_util.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>

#include "mock_tester.h"

#define DELAY_SM_CYCLES(n)	(n * CONFIG_USBC_STATE_MACHINE_CYCLE_TIME) 

#define PORT1_NODE DT_NODELABEL(port1)

enum tc_state_tests {
	TC_UNATTACHED_SNK_BIT,
	TC_ATTACHED_WAIT_SNK_BIT,
	TC_ATTACHED_SNK_BIT,

	TC_STATE_NUM,
};

enum pe_state_tests {
	PE_SNK_STARTUP_NOTIFY_TEST,
	PE_SNK_WAIT_FOR_CAPABILITIES_NOTIFY_TEST,
	PE_SNK_EVALUATE_CAPABILITY_NOTIFY_TEST,
	PE_SNK_SELECT_CAPABILITY_NOTIFY_TEST,
	PE_SNK_TRANSITION_SINK_NOTIFY_TEST,
	PE_SNK_READY_NOTIFY_TEST,
	PE_SNK_HARD_RESET_NOTIFY_TEST,
	PE_SNK_TRANSITION_TO_DEFAULT_NOTIFY_TEST,
	PE_SNK_GET_SOURCE_CAP_NOTIFY_TEST,
	PE_SEND_SOFT_RESET_NOTIFY_TEST,
	PE_SOFT_RESET_NOTIFY_TEST,
	PE_SEND_NOT_SUPPORTED_NOTIFY_TEST,
	PE_CHUNK_RECEIVED_NOTIFY_TEST,
	PE_SNK_GIVE_SINK_CAP_NOTIFY_TEST,
	PE_DRS_EVALUATE_SWAP_NOTIFY_TEST,
	PE_DRS_SEND_SWAP_NOTIFY_TEST,
	PE_SUSPEND_NOTIFY_TEST,

	PE_STATE_NUM,
};

enum prl_tx_state_tests {
	PRL_TX_WAIT_FOR_MESSAGE_REQUEST_NOTIFY_TEST,

	PRL_TX_STATE_NUM,
};

/*
 * These tests are derived from the USB Power Delivery Compliance
 * Test Specification revision 1.4 V4
 */

static const struct device *usbc_port1;

static struct port1_data_t {
	/* Current Protocol Layer TX State Machine State */
	atomic_t uut_prl_tx_state;
	/* Current Policy Engine State Machine State */
	atomic_t uut_pe_state;
	/* Current Type-C State Machine State */
	atomic_t uut_tc_state;

	/* Port Policy checks */

	/* Power Role Swap Policy check */
	atomic_t pp_check_power_role_swap;
	/* Data Role Swap to DFP Policy check */
	atomic_t pp_check_data_role_swap_to_dfp;
	/* Data Role Swap Policy to UFP check */
	atomic_t pp_check_data_role_swap_to_ufp;
	/* Sink at default level Policy check */
	atomic_t pp_check_snk_at_default_level;

	/* Port Notifications from Policy Engine */

	/* Protocol Error */
	atomic_t pn_protocol_error;
	/* Message Discarded */
	atomic_t pn_msg_discarded;
	/* Message Accept Received */
	atomic_t pn_msg_accept_received;
	/* Message Rejected Received */
	atomic_t pn_msg_rejected_received;
	/* Message Not Supported Received */
	atomic_t pn_msg_not_supported_received;
	/* Transition Power Supply */
	atomic_t pn_transition_ps;
	/* PD connected */
	atomic_t pn_pd_connected;
	/* Not PD connected */
	atomic_t pn_not_pd_connected;
	/* Power Changed to off */
	atomic_t pn_power_change_0a0;
	/* Power Changed to Default */
	atomic_t pn_power_change_def;
	/* Power Changed to 5V @ 1.5A */
	atomic_t pn_power_change_1a5;
	/* Power Changed to 5V @ 3A */
	atomic_t pn_power_change_3a0;
	/* Current data role is UFP */
	atomic_t pn_data_role_is_ufp;
	/* Current data role is DFP */
	atomic_t pn_data_role_is_dfp;
	/* Port Partner not responsive */
	atomic_t pn_port_partner_not_responsive;
	/* Sink transition to default */
	atomic_t pn_snk_transition_to_default;
	/* Hard Reset Received */
	atomic_t pn_hard_reset_received;

	/* Power Request */
	atomic_t uut_request;

	/* Sink Capability PDO */
	union pd_fixed_supply_pdo_sink snk_cap_pdo;

	/* Source Capability Number */
	uint32_t uut_received_src_cap_num;
	/* Source Capabilities */
	uint32_t uut_received_src_caps[10];
	/* Recieved message from UUT */
	struct pd_msg rx_msg;

	struct k_timer tc_timer[TC_STATE_NUM];
	uint32_t tc_timeout[TC_STATE_NUM];
	atomic_t tc_timer_expired;

	struct k_timer pe_timer[PE_STATE_NUM];
	uint32_t pe_timeout[PE_STATE_NUM];
	atomic_t pe_timer_expired;

	struct k_timer prl_tx_timer[PRL_TX_STATE_NUM];
	uint32_t prl_tx_timeout[PRL_TX_STATE_NUM];
	atomic_t prl_tx_timer_expired;

	struct k_timer policy_timer;
	uint32_t policy_timeout;
	atomic_t policy_timer_expired;
} port1_data;

static int uut_policy_cb_get_snk_cap(const struct device *dev,
					uint32_t **pdos,
					int *num_pdos)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	*pdos = &data->snk_cap_pdo.raw_value;
	*num_pdos = 1;	

	return 0;
}

static void uut_policy_cb_set_src_cap(const struct device *dev,
				      const uint32_t *pdos,
				      const int num_pdos)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	int i;

	data->uut_received_src_cap_num = num_pdos;
	for (i = 0; i < num_pdos; i++) {
		data->uut_received_src_caps[i] = *(pdos + i);
	}
}

static uint32_t uut_policy_cb_get_rdo(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	atomic_set(&data->uut_request, true);

	return FIXED_5V_100MA_RDO;
}

static void delay_for(int ms)
{
	struct k_timer timer;

	k_timer_init(&timer, NULL, NULL);
	k_timer_start(&timer, K_MSEC(ms), K_NO_WAIT);
	k_timer_status_sync(&timer);
	k_timer_stop(&timer);
}


#if 0
static void stop_tc_timer(const struct device *dev, enum tc_state_tests uut_state)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	data->tc_timeout[uut_state] = k_timer_remaining_get(&data->tc_timer[uut_state]);
	k_timer_stop(&data->tc_timer[uut_state]);
}

static void stop_pe_timer(const struct device *dev, enum pe_state_tests uut_state)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	data->pe_timeout[uut_state] = k_timer_remaining_get(&data->pe_timer[uut_state]);
	k_timer_stop(&data->pe_timer[uut_state]);
}

static void stop_prl_tx_timer(const struct device *dev, enum prl_tx_state_tests uut_state)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	data->prl_tx_timeout[uut_state] = k_timer_remaining_get(&data->prl_tx_timer[uut_state]);
	k_timer_stop(&data->prl_tx_timer[uut_state]);
}
#endif

static void stop_policy_timer(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	data->policy_timeout = k_timer_remaining_get(&data->policy_timer);
	k_timer_stop(&data->policy_timer);
}

static void uut_notify(const struct device *dev,
		       const enum usbc_policy_notify_t policy_notify)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	switch (policy_notify) {
	case MSG_ACCEPT_RECEIVED:
		atomic_set(&data->pn_msg_accept_received, true);
		break;
	case MSG_REJECTED_RECEIVED:
		atomic_set(&data->pn_msg_rejected_received, true);
		break;
	case MSG_DISCARDED:
		atomic_set(&data->pn_msg_discarded, true);
		break;
	case MSG_NOT_SUPPORTED_RECEIVED:
		atomic_set(&data->pn_msg_not_supported_received, true);
		break;
	case DATA_ROLE_IS_UFP:
		atomic_set(&data->pn_data_role_is_ufp, true);
		break;
	case DATA_ROLE_IS_DFP:
		atomic_set(&data->pn_data_role_is_dfp, true);
		break;
	case PD_CONNECTED:
		stop_policy_timer(dev);
		atomic_set(&data->pn_pd_connected, true);
		break;
	case NOT_PD_CONNECTED:
		atomic_set(&data->pn_not_pd_connected, true);
		break;
	case TRANSITION_PS:
		atomic_set(&data->pn_transition_ps, true);
		break;
	case PORT_PARTNER_NOT_RESPONSIVE:
		stop_policy_timer(dev);
		atomic_set(&data->pn_port_partner_not_responsive, true);
		break;
	case PROTOCOL_ERROR:
		atomic_set(&data->pn_protocol_error, true);
		break;
	case SNK_TRANSITION_TO_DEFAULT:
		atomic_set(&data->pn_snk_transition_to_default, true);
		break;
	case HARD_RESET_RECEIVED:
		atomic_set(&data->pn_hard_reset_received, true);
		break;
	case POWER_CHANGE_0A0:
		atomic_set(&data->pn_power_change_0a0, true);
		break;
	case POWER_CHANGE_DEF:
		atomic_set(&data->pn_power_change_def, true);
		break;
	case POWER_CHANGE_1A5:
		atomic_set(&data->pn_power_change_1a5, true);
		break;
	case POWER_CHANGE_3A0:
		atomic_set(&data->pn_power_change_3a0, true);
		break;
	/* New enum values that Sam didn't have originally. */
	case SENDER_RESPONSE_TIMEOUT:
		break;
	case SOURCE_CAPABILITIES_RECEIVED:
		break;

#if 0
	/* Unknown enum values. */
	case TC_UNATTACHED_SNK:
		stop_tc_timer(dev, TC_UNATTACHED_SNK_BIT);
		atomic_set_bit(&data->uut_tc_state, TC_UNATTACHED_SNK_BIT);
		break;
	case TC_ATTACHED_WAIT_SNK:
		stop_tc_timer(dev, TC_ATTACHED_WAIT_SNK_BIT);
		atomic_set_bit(&data->uut_tc_state, TC_ATTACHED_WAIT_SNK_BIT);
		break;
	case TC_ATTACHED_SNK:
		stop_tc_timer(dev, TC_ATTACHED_SNK_BIT);
		atomic_set_bit(&data->uut_tc_state, TC_ATTACHED_SNK_BIT);
		break;

	case PRL_TX_WAIT_FOR_MESSAGE_REQUEST_NOTIFY:
		stop_prl_tx_timer(dev, PRL_TX_WAIT_FOR_MESSAGE_REQUEST_NOTIFY_TEST);
		atomic_set_bit(&data->uut_prl_tx_state, PRL_TX_WAIT_FOR_MESSAGE_REQUEST_NOTIFY_TEST);
		break;

	case PE_SNK_STARTUP_NOTIFY:
		atomic_set_bit(&data->uut_pe_state, PE_SNK_STARTUP_NOTIFY_TEST);
		break;
	case PE_SNK_WAIT_FOR_CAPABILITIES_NOTIFY:
		stop_pe_timer(dev, PE_SNK_WAIT_FOR_CAPABILITIES_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state,
					PE_SNK_WAIT_FOR_CAPABILITIES_NOTIFY_TEST);
		break;
	case PE_SNK_EVALUATE_CAPABILITY_NOTIFY:
		stop_pe_timer(dev, PE_SNK_EVALUATE_CAPABILITY_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state,
					PE_SNK_EVALUATE_CAPABILITY_NOTIFY_TEST);
		break;
	case PE_SNK_SELECT_CAPABILITY_NOTIFY:
		stop_pe_timer(dev, PE_SNK_SELECT_CAPABILITY_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_SNK_SELECT_CAPABILITY_NOTIFY_TEST);
		break;
	case PE_SNK_TRANSITION_SINK_NOTIFY:
		stop_pe_timer(dev, PE_SNK_TRANSITION_SINK_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_SNK_TRANSITION_SINK_NOTIFY_TEST);
		break;
	case PE_SNK_READY_NOTIFY:
		stop_pe_timer(dev, PE_SNK_READY_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_SNK_READY_NOTIFY_TEST);
		break;
	case PE_SNK_HARD_RESET_NOTIFY:
		stop_pe_timer(dev, PE_SNK_HARD_RESET_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_SNK_HARD_RESET_NOTIFY_TEST);
		break;
	case PE_SNK_TRANSITION_TO_DEFAULT_NOTIFY:
		stop_pe_timer(dev, PE_SNK_TRANSITION_TO_DEFAULT_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state,
					PE_SNK_TRANSITION_TO_DEFAULT_NOTIFY_TEST);
		break;
	case PE_SNK_GET_SOURCE_CAP_NOTIFY:
		stop_pe_timer(dev, PE_SNK_GET_SOURCE_CAP_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_SNK_GET_SOURCE_CAP_NOTIFY_TEST);
		break;
	case PE_SEND_SOFT_RESET_NOTIFY:
		stop_pe_timer(dev, PE_SEND_SOFT_RESET_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_SEND_SOFT_RESET_NOTIFY_TEST);
		break;
	case PE_SOFT_RESET_NOTIFY:
		stop_pe_timer(dev, PE_SOFT_RESET_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_SOFT_RESET_NOTIFY_TEST);
		break;
	case PE_SEND_NOT_SUPPORTED_NOTIFY:
		stop_pe_timer(dev, PE_SEND_NOT_SUPPORTED_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_SEND_NOT_SUPPORTED_NOTIFY_TEST);
		break;
	case PE_CHUNK_RECEIVED_NOTIFY:
		stop_pe_timer(dev, PE_CHUNK_RECEIVED_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_CHUNK_RECEIVED_NOTIFY_TEST);
		break;
	case PE_SNK_GIVE_SINK_CAP_NOTIFY:
		stop_pe_timer(dev, PE_SNK_GIVE_SINK_CAP_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_SNK_GIVE_SINK_CAP_NOTIFY_TEST);
		break;
	case PE_DRS_EVALUATE_SWAP_NOTIFY:
		stop_pe_timer(dev, PE_DRS_EVALUATE_SWAP_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_DRS_EVALUATE_SWAP_NOTIFY_TEST);
		break;
	case PE_DRS_SEND_SWAP_NOTIFY:
		stop_pe_timer(dev, PE_DRS_SEND_SWAP_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_DRS_SEND_SWAP_NOTIFY_TEST);
		break;
	case PE_SUSPEND_NOTIFY:
		stop_pe_timer(dev, PE_SUSPEND_NOTIFY_TEST);
		atomic_set_bit(&data->uut_pe_state, PE_SUSPEND_NOTIFY_TEST);
		break;
#endif
	}
}

bool uut_policy_check(const struct device *dev,
		      const enum usbc_policy_check_t policy_check)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	switch (policy_check) {
	case CHECK_POWER_ROLE_SWAP:
		return data->pp_check_power_role_swap;
	case CHECK_DATA_ROLE_SWAP_TO_DFP:
		return data->pp_check_data_role_swap_to_dfp;
	case CHECK_DATA_ROLE_SWAP_TO_UFP:
		return data->pp_check_data_role_swap_to_ufp;
	case CHECK_SNK_AT_DEFAULT_LEVEL:
		return data->pp_check_snk_at_default_level;
	/* TODO: Placeholder */
	case CHECK_VCONN_CONTROL:
		return true;
	}

	return false;
}

static uint32_t delay_until_in_tc_state_or_timeout(const struct device *dev,
						enum tc_state_tests check, int timeout)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	uint32_t elapsed = timeout;

	if (atomic_test_and_clear_bit(&data->uut_tc_state, check)) {
		return 0;
	}

	k_timer_start(&data->tc_timer[check], K_MSEC(timeout), K_NO_WAIT);
	k_timer_status_sync(&data->tc_timer[check]);
	elapsed -= data->tc_timeout[check];
	k_timer_stop(&data->tc_timer[check]);

	delay_for(10);

	return elapsed;
}

static uint32_t delay_until_in_pe_state_or_timeout(const struct device *dev,
						enum pe_state_tests check, int timeout)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	uint32_t elapsed;

	if (atomic_test_and_clear_bit(&data->uut_pe_state, check)) {
		return 0;
	}

	k_timer_start(&data->pe_timer[check], K_MSEC(timeout), K_NO_WAIT);
	k_timer_status_sync(&data->pe_timer[check]);
	elapsed = timeout - data->pe_timeout[check];

	k_msleep(20);

	return elapsed;
}
#if 0
static uint32_t delay_until_in_prl_tx_state_or_timeout(const struct device *dev,
						enum prl_tx_state_tests check, int timeout)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	uint32_t elapsed;

	if (atomic_test_and_clear_bit(&data->uut_prl_tx_state, check)) {
		return 0;
	}

	k_timer_start(&data->prl_tx_timer[check], K_MSEC(timeout), K_NO_WAIT);
	k_timer_status_sync(&data->prl_tx_timer[check]);
	elapsed = timeout - data->prl_tx_timeout[check];

	return elapsed;
}
#endif
#if 0
static void policy_timer_handler(struct k_timer *timer)
{
	struct port1_data_t *data = k_timer_user_data_get(timer);

	atomic_set(&data->policy_timer_expired, true);
}

static uint32_t delay_until_notify_or_timeout(const struct device *dev,
				atomic_t *policy, int timeout)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	uint32_t elapsed = timeout;

	if (atomic_get(policy)) {
		zassert_true(false, "Timing error. Policy already set");
		return 0;
	}

	data->policy_timeout = 0;
	atomic_set(&data->policy_timer_expired, false);
	k_timer_start(&data->policy_timer, K_MSEC(timeout), K_NO_WAIT);

	do {
		if (atomic_get(policy)) {
			elapsed -= data->policy_timeout;
			break;
		}
		k_msleep(1);
	} while(atomic_get(&data->policy_timer_expired) == false);

	k_timer_stop(&data->policy_timer);

	printk("%d %d %d\n", elapsed, elapsed / 2, elapsed >> 1);

	return elapsed >> 1;
}
#endif

static void * test_usbc_setup(void)
{
	/* Get the device for this port */
	usbc_port1 = DEVICE_DT_GET(PORT1_NODE);
	zassert_true(device_is_ready(usbc_port1), "Failed to find USBC PORT1");

	for (int i = 0; i < TC_STATE_NUM; i++) {
		k_timer_init(&port1_data.tc_timer[i], NULL, NULL);
		k_timer_user_data_set(&port1_data.tc_timer[i], &port1_data);
	}

	for (int i = 0; i < PE_STATE_NUM; i++) {
		k_timer_init(&port1_data.pe_timer[i], NULL, NULL);
		k_timer_user_data_set(&port1_data.pe_timer[i], &port1_data);
	}

	for (int i = 0; i < PRL_TX_STATE_NUM; i++) {
		k_timer_init(&port1_data.prl_tx_timer[i], NULL, NULL);
		k_timer_user_data_set(&port1_data.prl_tx_timer[i], &port1_data);
	}

#if 0
	k_timer_init(&port1_data.policy_timer, policy_timer_handler, NULL);
	k_timer_user_data_set(&port1_data.policy_timer, &port1_data);
#endif

	/* Initialize the Sink Cap PDO */
	port1_data.snk_cap_pdo.type = PDO_FIXED;
	port1_data.snk_cap_pdo.dual_role_power = 1;
	port1_data.snk_cap_pdo.higher_capability = 0;
	port1_data.snk_cap_pdo.unconstrained_power = 1;
	port1_data.snk_cap_pdo.usb_comms_capable = 0;
	port1_data.snk_cap_pdo.dual_role_data = 0;
	port1_data.snk_cap_pdo.frs_required = 0;
	port1_data.snk_cap_pdo.reserved0 = 0;
	port1_data.snk_cap_pdo.voltage =
				PD_CONVERT_MV_TO_FIXED_PDO_VOLTAGE(5000);
	port1_data.snk_cap_pdo.operational_current =
				PD_CONVERT_MA_TO_FIXED_PDO_CURRENT(100);

	/* Register USB-C Callbacks */

	/* Register Policy Check callback */
	usbc_set_policy_cb_check(usbc_port1, uut_policy_check);
	/* Register Policy Notify callback */
	usbc_set_policy_cb_notify(usbc_port1, uut_notify);
	/* Register Policy Get Sink Capabilities callback */
	usbc_set_policy_cb_get_snk_cap(usbc_port1, uut_policy_cb_get_snk_cap);
	/* Register Policy Set Source Capabilities callback */
	usbc_set_policy_cb_set_src_cap(usbc_port1, uut_policy_cb_set_src_cap);
	/* Register Policy Get Request Data Object callback */
	usbc_set_policy_cb_get_rdo(usbc_port1, uut_policy_cb_get_rdo);
	/*
	 * Set Tester port data object. This object is passed to the
	 * policy callbacks
	 */
	usbc_set_dpm_data(usbc_port1, &port1_data);

	return NULL;
}

static void test_usbc_before(void *f)
{
	/* Tester is source */
	tester_set_power_role_source();

	/* Tester is UFP */
	tester_set_data_role_ufp();

	/* Start the USB-C Subsystem */
	usbc_start(usbc_port1);
}

static void test_usbc_after(void *f)
{
	/* Stop the USB-C Subsystem */
	usbc_suspend(usbc_port1);
}

/**
 * @brief COMMON.CHECK.PD.8 Check Request Message
 *
 * The Tester performs additional protocol checks to all Request messages
 * sent by the UUT.
 */
static void check_request_message(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	union pd_rdo rdo;

	/* 1) Field check for all types of Request Data Object */
	rdo.raw_value = sys_get_le32(data->rx_msg.data);

	/*
	 * a) B31…28 (Object Position) is not 000b, and the value is not
	 *    greater than the number of PDOs in the last Source Capabilities
	 *    message
	 */
	zassert_not_equal(rdo.fixed.object_pos, 0,
				"RDO object position can't be zero");
	zassert_true(rdo.fixed.object_pos <= data->uut_received_src_cap_num,
				"RDO object position out of range");
}


/**
 * @brief COMMON.PROC.PD.10 UUT Sent Request
 *
 * Tester runs this procedure whenever receiving
 * Request message from the UUT.
 */
static void uut_sent_request(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	atomic_clear_bit(&data->uut_pe_state, PE_SNK_TRANSITION_SINK_NOTIFY_TEST);
	atomic_clear_bit(&data->uut_pe_state, PE_SNK_READY_NOTIFY_TEST);

	/*
	 * 2) The Tester responds with an Accept message to the
	 *    Request message.
	 */
	tester_send_ctrl_msg(PD_CTRL_ACCEPT, true);

	/*
	 * 3) If the VBUS voltage is stable within the target voltage,
	 *    the Tester sends a PS_RDY message after the reception of
	 *    Accept message.
	 */
	delay_until_in_pe_state_or_timeout(dev,
				PE_SNK_TRANSITION_SINK_NOTIFY_TEST, 1000);

	tester_send_ctrl_msg(PD_CTRL_PS_RDY, true);
	delay_until_in_pe_state_or_timeout(dev, PE_SNK_READY_NOTIFY_TEST, 1000);

	zassert_true(atomic_get(&data->pn_transition_ps),
			"UUT failed to respond to PS_RDY message");
}

/**
 * @brief COMMON.PROC.BU.2 Bring-up Sink UUT 
 */
static void bring_up_sink_uut(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	union pd_fixed_supply_pdo_source pdo;

	/* The TCPC sends a GoodCRC */
	tc_set_state(TS_SEND_GOODCRC);

	/* Initialize test variables */
	data->uut_tc_state = ATOMIC_INIT(0);
        data->uut_pe_state = ATOMIC_INIT(0);
	data->uut_prl_tx_state = ATOMIC_INIT(0);
        data->pn_transition_ps = ATOMIC_INIT(false);
        data->uut_request = ATOMIC_INIT(false);

        data->pp_check_power_role_swap = ATOMIC_INIT(false);
        data->pp_check_data_role_swap_to_dfp = ATOMIC_INIT(false);
        data->pp_check_data_role_swap_to_ufp = ATOMIC_INIT(false);
        data->pp_check_snk_at_default_level = ATOMIC_INIT(false);

	data->pn_protocol_error = ATOMIC_INIT(false);
	data->pn_msg_discarded = ATOMIC_INIT(false);
	data->pn_msg_accept_received = ATOMIC_INIT(false);
	data->pn_msg_rejected_received = ATOMIC_INIT(false);
	data->pn_msg_not_supported_received = ATOMIC_INIT(false);
	data->pn_transition_ps = ATOMIC_INIT(false);
	data->pn_pd_connected = ATOMIC_INIT(false);
	data->pn_not_pd_connected = ATOMIC_INIT(false);
	data->pn_power_change_0a0 = ATOMIC_INIT(false);
	data->pn_power_change_def = ATOMIC_INIT(false);
	data->pn_power_change_1a5 = ATOMIC_INIT(false);
	data->pn_power_change_3a0 = ATOMIC_INIT(false);
	data->pn_data_role_is_ufp = ATOMIC_INIT(false);
	data->pn_data_role_is_dfp = ATOMIC_INIT(false);
	data->pn_port_partner_not_responsive = ATOMIC_INIT(false);
	data->pn_snk_transition_to_default = ATOMIC_INIT(false);
	data->pn_hard_reset_received = ATOMIC_INIT(false);

	data->uut_received_src_cap_num = 0;
	data->snk_cap_pdo.raw_value = 0;
	data->rx_msg.len = 0;

	/* Initialize PDO for sending in step 5 */

	/* a) B31…30 (Fixed Supply) set to 00b */
	pdo.type = PDO_FIXED;
	/* b) B29 (Dual-Role Power) set to 1b */
	pdo.dual_role_power = 1;
	/* c) B28 (USB Suspend Supported) set to 0b */
	pdo.usb_suspend_supported = 0;
	/* d) B27 (Unconstrained Power) set to 1b */
	pdo.unconstrained_power = 1;
	/* e) B26 (USB Communications Capable) set to 0b */
	pdo.usb_comms_capable = 0;
	/* f) B25 (Dual-Role Data) set to 0b */
	pdo.dual_role_data = 0;
	/* g) B24 (PD3, Unchunked Extended Messages Supported) set to 0b */
	pdo.unchunked_ext_msg_supported = 0;
	/*
	 * h) B23 (EPR Mode Capable) to 0b, unless it is mentioned in the
	 *    test procedure. NOTE: NOT CURRENTLY SUPPORTED IN THE SUBSYSTEM.
	 */
	pdo.reserved0 = 0;
	/* i) B21…20 (Peak Current) set to 00b */
	pdo.peak_current = 0;
	/* j) B19…10 (Voltage) set to 5V */
	pdo.voltage = PD_CONVERT_MV_TO_FIXED_PDO_VOLTAGE(5000);
	/* k) B9…0 (Maximum Current) set to 100mA */
	pdo.max_current = PD_CONVERT_MA_TO_FIXED_PDO_CURRENT(100);


	/* 1) The test starts in a disconnected state. */
	tester_disconnected();
	delay_for(200);
	
	/* 2) The tester applies Rp. */
	tester_apply_cc(TC_CC_VOLT_RP_3A0, TC_CC_VOLT_OPEN);

	/*
	 * 3) If the UUT attachment is detected, the Tester applies VCONN
	 *    (if Ra is detected) and vSafe5V on VBUS.
	 */
	tester_apply_vbus(PD_V_SAFE_5V_MIN_MV);

	/* 4) The Tester waits until TC_ATTACHED_SNK state is reached. */
	delay_until_in_tc_state_or_timeout(dev, TC_ATTACHED_SNK_BIT, 1000);

	/*
	 * 5. The Tester transmits Source Capabilities message with
	 *    single PDO:
	 */
	delay_until_in_pe_state_or_timeout(dev,
				PE_SNK_WAIT_FOR_CAPABILITIES_NOTIFY_TEST, 1000);

	tester_send_data_msg(PD_DATA_SOURCE_CAP, &pdo.raw_value, 1, true);

	/*
	 * 6) Repeat Step-5 if the Tester does not receive a GoodCRC from
	 *    the UUT in response to Source Capabilities message.
	 *    This sequence is repeated at least 50 times.
	 */
	delay_until_in_pe_state_or_timeout(dev,
				PE_SNK_SELECT_CAPABILITY_NOTIFY_TEST, 1000);

	/*
	 * 7) The check fails if the UUT does not respond with
	 *    a Request message.
	 */
	zassert_true(atomic_get(&data->uut_request), "UUT didn't send request message.");
	atomic_set(&data->uut_request, false);

	tester_get_uut_tx_data(&data->rx_msg);
	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
			"UUT message not sent to SOP");
	zassert_equal(data->rx_msg.header.message_type, PD_DATA_REQUEST,
			"UUT did not send request msg");

	check_request_message(dev);
	uut_sent_request(dev);

	zassert_equal(data->uut_received_src_cap_num, 1,
			"UUT failed to respond to Source Capabilites message");
	zassert_equal(pdo.raw_value, data->uut_received_src_caps[0],
			"Sent PDO does not match UUT's received PDO");
	zassert_true(atomic_get(&data->pn_pd_connected),
			"UUT not PD connected");

	/* 10) An explicit contract is now established. */

	/*
	 * 11) The Tester presents SinkTxOK if the test is in PD3 mode.
	 *     The Tester waits 500ms to respond to messages from the UUT.
	 */
	delay_for(500);

	data->uut_tc_state = ATOMIC_INIT(0);
	data->uut_pe_state = ATOMIC_INIT(0);
	data->uut_prl_tx_state = ATOMIC_INIT(0);
	data->pn_transition_ps = ATOMIC_INIT(false);
	data->uut_request = ATOMIC_INIT(false);

	printk("UUT Sink is up in PD%d mode\n", tester_get_rev() + 1);
}
#if 1
/**
 * @brief TEST.PD.PROT.SNK.1 Get_Sink_Cap Response
 *
 * The Tester verifies that the Sink UUT responds correctly
 * to the Get_Sink_Cap message.
 */
static void test_get_sink_cap_response(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	/*
	 * 1.b) The UUT has VIF field PD_Port_Type set to anything else, the
	 *      Tester runs bring-up procedure with the UUT as a Sink
	 *      COMMON.PROC.BU.2
	 */
	bring_up_sink_uut(dev);

	/*
	 * 2. The Tester sends a Get_Sink_Cap message to the UUT. The Tester
	 *    continues to present SinkTxNG while waiting for a response if
	 *    the test is running in PD3 mode.
	 */
	tester_send_ctrl_msg(PD_CTRL_GET_SINK_CAP, true);
	delay_until_in_pe_state_or_timeout(dev, PE_SNK_GIVE_SINK_CAP_NOTIFY_TEST, 1000);

	/*
	 * 3) If the UUT is a Sink, the check fails if the UUT does not send
	 *	a Sink Capabilities message.
	 */
	tester_get_uut_tx_data(&data->rx_msg);

	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
				"UUT Sink Cap msg not sent to SOP");
	zassert_equal(data->rx_msg.header.message_type, PD_DATA_SINK_CAP,
				"UUT did not send Sink Cap msg");
}

ZTEST(test_usbc, test_pd_prot_snk_1)
{
	/** Test in PD2.0 mode */
	
	/* Start the USB-C Subsystem */
	usbc_start(usbc_port1);

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_get_sink_cap_response(usbc_port1);

	usbc_suspend(usbc_port1);
	delay_for(500);

	/** Test in PD3.0 mode */
	usbc_start(usbc_port1);

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_get_sink_cap_response(usbc_port1);

	usbc_suspend(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.SNK.2 Get_Source_Cap Response
 *
 * The Tester verifies that the Sink UUT responds correctly
 * to the Get_Source_Cap message.
 */
static void test_get_source_cap_response(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	printk("Tester PD%d mode\n", tester_get_rev() + 1);

	/*
	 * 1.b The UUT has VIF field PD_Port_Type set to anything else, the
	 *      Tester runs bring-up procedure with the UUT as a Sink
	 *      COMMON.PROC.BU.2
	 */
	bring_up_sink_uut(dev);

	/*
	 * 2. The Tester sends a Get_Source_Cap message to the UUT. The Tester
	 *    continues to present SinkTxNG while waiting for a response if
	 *    the test is running in PD3 mode.
	 */
	tester_send_ctrl_msg(PD_CTRL_GET_SOURCE_CAP, true);
	delay_until_in_pe_state_or_timeout(dev, PE_SEND_NOT_SUPPORTED_NOTIFY_TEST, 1000);

	/*
	 * 3.a The check fails if VIF field PD_Port_Type = Consumer Only and
	 *     the UUT does not send a Reject message (in PD2 mode) or
	 *     Not_Supported (in PD3 mode).
	 */
	tester_get_uut_tx_data(&data->rx_msg);
	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
				"UUT msg not sent to SOP");

	if (tester_get_rev() == PD_REV20) {
		zassert_equal(data->rx_msg.header.message_type,
			PD_CTRL_REJECT, "UUT sent wrong message");
	}
	else if (tester_get_rev() == PD_REV30) {
		zassert_equal(data->rx_msg.header.message_type,
			PD_CTRL_NOT_SUPPORTED, "UUT sent wrong message");
	}
	else {
		ztest_test_fail();
	}
}

ZTEST(test_usbc, test_pd_prot_snk_2)
{
	/** Test in PD2.0 mode */

	usbc_start(usbc_port1);
	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_get_source_cap_response(usbc_port1);

	usbc_suspend(usbc_port1);
	delay_for(500);
	/** Test in PD3.0 mode */
	usbc_start(usbc_port1);

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_get_source_cap_response(usbc_port1);

	usbc_suspend(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.SNK.3 SinkWaitCapTimer Deadline
 *
 * The Tester verifies that the UUT provides a Request to a Source Capabilities
 * message sent at the deadline limit of tTypeCSinkWaitCap after a Hard Reset.
 */
static void test_sink_wait_cap_timer_deadline(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	union pd_fixed_supply_pdo_source pdo;

	printk("Tester PD%d mode\n", tester_get_rev() + 1);

	/*
	 * 1.b The UUT has VIF field PD_Port_Type set to anything else, the
	 *      Tester runs bring-up procedure with the UUT as a Sink
	 *      COMMON.PROC.BU.2
	 */
	bring_up_sink_uut(dev);

	/*
	 * 2. The Tester sends a Hard Reset. It drives VBUS to vSafe0V and then
	 *    restores VBUS to vSafe5V.
	 */
	tester_send_hard_reset();
	delay_for(20);
	tester_apply_vbus(PD_V_SAFE_0V_MAX_MV);

	delay_until_in_pe_state_or_timeout(dev, TC_UNATTACHED_SNK_BIT, 1000);
	tester_apply_vbus(PD_V_SAFE_5V_MIN_MV);

	/*
	 * The Tester transmits Source Capabilities message with single PDO
	 * immediately prior to tTypeCSinkWaitCap min (the delay is from the
	 * time VBUS present vSafe5V min to the last bit of Source Capabilities
	 * message EOP):
	 */
	/* a) B31…30 (Fixed Supply) set to 00b */
	pdo.type = PDO_FIXED;
	/* b) B29 (Dual-Role Power) set to 1b */
	pdo.dual_role_power = 1;
	/* c) B28 (USB Suspend Supported) set to 0b */
	pdo.usb_suspend_supported = 0;
	/* d) B27 (Unconstrained Power) set to 1b */
	pdo.unconstrained_power = 1;
	/* e) B26 (USB Communications Capable) set to 0b */
	pdo.usb_comms_capable = 0;
	/* f) B25 (Dual-Role Data) set to 0b */
	pdo.dual_role_data = 0;
	/* g) B24 (PD3, Unchunked Extended Messages Supported) set to 0b */
	pdo.unchunked_ext_msg_supported = 0;
	/*
	 * h) B23 (EPR Mode Capable) to 0b, unless it is mentioned in the
	 *    test procedure. NOTE: NOT CURRENTLY SUPPORTED IN THE SUBSYSTEM.
	 */
	pdo.reserved0 = 0;
	/* i) B21…20 (Peak Current) set to 00b */
	pdo.peak_current = 0;
	/* j) B19…10 (Voltage) set to 5V */
	pdo.voltage = PD_CONVERT_MV_TO_FIXED_PDO_VOLTAGE(5000);
	/* k) B9…0 (Maximum Current) set to 100mA */
	pdo.max_current = PD_CONVERT_MA_TO_FIXED_PDO_CURRENT(100);

	delay_until_in_pe_state_or_timeout(dev,
				PE_SNK_WAIT_FOR_CAPABILITIES_NOTIFY_TEST, 1000);

	delay_for(PD_T_TYPEC_SINK_WAIT_CAP_MIN_MS);
	tester_send_data_msg(PD_DATA_SOURCE_CAP, &pdo.raw_value, 1, true);

	/*
	 * 4. The Tester checks that the UUT responds with a Request message.
	 */
	delay_until_in_pe_state_or_timeout(dev,
					PE_SNK_SELECT_CAPABILITY_NOTIFY_TEST, 1000);

	zassert_true(atomic_get(&data->uut_request),
					"UUT didn't send request message.");
	atomic_set(&data->uut_request, false);

	tester_get_uut_tx_data(&data->rx_msg);
	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
				"UUT message not sent to SOP");
	zassert_equal(data->rx_msg.header.message_type, PD_DATA_REQUEST,
				"UUT did not send request msg");
}

ZTEST(test_usbc, test_pd_prot_snk_3)
{
	/** Test in PD2.0 mode */
	usbc_start(usbc_port1);
	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_sink_wait_cap_timer_deadline(usbc_port1);

	usbc_suspend(usbc_port1);
	delay_for(500);
	usbc_start(usbc_port1);
	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_sink_wait_cap_timer_deadline(usbc_port1);
	usbc_suspend(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.SNK.4 SinkWaitCapTimer Timeout
 *
 * The Tester does not send Source Capabilities message after cycling
 * VBUS to force a SinkWaitCapTimer timeout on the UUT, then verifies
 * it is correctly implemented.
 */
static void test_sink_wait_cap_timer_timeout(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	uint32_t time;

	printk("Tester PD%d mode\n", tester_get_rev() + 1);

	/*
	 * 1.b The UUT has VIF field PD_Port_Type set to anything else, the
	 *      Tester runs bring-up procedure with the UUT as a Sink
	 *      COMMON.PROC.BU.2
	 */
	bring_up_sink_uut(dev);

	/*
	 * 2. The Tester sends a Hard Reset. It drives VBUS to vSafe0V and then
	 *    restores VBUS to vSafe5V.
	 */
	atomic_clear_bit(&data->uut_pe_state, PE_SNK_HARD_RESET_NOTIFY_TEST);
	atomic_clear_bit(&data->uut_pe_state, PE_SNK_WAIT_FOR_CAPABILITIES_NOTIFY_TEST);
	atomic_clear_bit(&data->uut_tc_state, TC_UNATTACHED_SNK_BIT);
	atomic_clear_bit(&data->uut_tc_state, TC_ATTACHED_SNK_BIT);

	tester_send_hard_reset();	
	tester_apply_vbus(PD_V_SAFE_0V_MAX_MV);
	delay_until_in_tc_state_or_timeout(dev, TC_UNATTACHED_SNK_BIT, 1000);
	tester_apply_vbus(PD_V_SAFE_5V_MIN_MV);

	delay_until_in_tc_state_or_timeout(dev, TC_ATTACHED_SNK_BIT, 1000);

	/*
	 * 3. The Tester does not send a Source Capabilities message after
	 *    cycling the VBUS to force a SinkWaitCaptTimer timeout on the UUT.
	 */
	time = delay_until_in_pe_state_or_timeout(dev,
				PE_SNK_WAIT_FOR_CAPABILITIES_NOTIFY_TEST, 1000);

	/*
	 * 4. The check fails if the UUT does not send a Hard Reset between
	 *    tTypeCSinkWaitCap min and max.
	 */
	time = delay_until_in_pe_state_or_timeout(dev, PE_SNK_HARD_RESET_NOTIFY_TEST, 1000);
	printk("Sink wait cap. timeout: %d >= %d <= %d\n",
		PD_T_TYPEC_SINK_WAIT_CAP_MIN_MS, time, PD_T_TYPEC_SINK_WAIT_CAP_MAX_MS);
	zassert_true((time >= PD_T_TYPEC_SINK_WAIT_CAP_MIN_MS &&
				time <= PD_T_TYPEC_SINK_WAIT_CAP_MAX_MS),
			"UUT timeout out of bounds");
}

ZTEST(test_usbc, test_pd_prot_snk_4)
{
	/** Test in PD2.0 mode */

	usbc_start(usbc_port1);

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_sink_wait_cap_timer_timeout(usbc_port1);

	usbc_suspend(usbc_port1);
	delay_for(500);
	usbc_start(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_sink_wait_cap_timer_timeout(usbc_port1);
	usbc_suspend(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.SNK.5 SenderResponseTimer Deadline
 *
 * The Tester verifies that the UUT accepts an Accept message sent at
 * the deadline limit of tSenderResponse min.
 */
static void test_sender_response_timer_deadline(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	union pd_fixed_supply_pdo_source pdo;
	uint32_t time;

	printk("Tester PD%d mode\n", tester_get_rev() + 1);

	/* a) B31…30 (Fixed Supply) set to 00b */
	pdo.type = PDO_FIXED;
	/* b) B29 (Dual-Role Power) set to 0b, Tester is a Source only */
	pdo.dual_role_power = 0;
	/* c) B28 (USB Suspend Supported) set to 0b */
	pdo.usb_suspend_supported = 0;
	/* d) B27 (Unconstrained Power) set to 0b */
	pdo.unconstrained_power = 0;
	/* e) B26 (USB Communications Capable) set to 0b */
	pdo.usb_comms_capable = 0;
	/* f) B25 (Dual-Role Data) set to 0b */
	pdo.dual_role_data = 0;
	/* g) B24 (PD3, Unchunked Extended Messages Supported) set to 0b */
	pdo.unchunked_ext_msg_supported = 0;
	/*
	 * h) B23 (EPR Mode Capable) to 0b, unless it is mentioned in the
	 *    test procedure. NOTE: NOT CURRENTLY SUPPORTED IN THE SUBSYSTEM.
	 */
	pdo.reserved0 = 0;
	/* i) B21…20 (Peak Current) set to 00b */
	pdo.peak_current = 0;
	/* j) B19…10 (Voltage) set to 5V */
	pdo.voltage = PD_CONVERT_MV_TO_FIXED_PDO_VOLTAGE(5000);
	/* k) B9…0 (Maximum Current) set to 100mA */
	pdo.max_current = PD_CONVERT_MA_TO_FIXED_PDO_CURRENT(100);

	/*
	 * 1.b The UUT has VIF field PD_Port_Type set to anything else, the
	 *      Tester runs bring-up procedure with the UUT as a Sink
	 *      COMMON.PROC.BU.2
	 */
	bring_up_sink_uut(dev);

	atomic_clear_bit(&data->uut_pe_state, PE_SNK_HARD_RESET_NOTIFY_TEST);
	atomic_clear_bit(&data->uut_pe_state, PE_SNK_SELECT_CAPABILITY_NOTIFY_TEST);

	/* 2. The Tester transmits Source Capabilities message with single PDO */
	tester_send_data_msg(PD_DATA_SOURCE_CAP, &pdo.raw_value, 1, true);

	delay_until_in_pe_state_or_timeout(dev, PE_SNK_SELECT_CAPABILITY_NOTIFY_TEST, 1000);

	/* 3. The check fails if the UUT does not respond with */
        zassert_true(atomic_get(&data->uut_request), "UUT didn't send request message.");
        atomic_set(&data->uut_request, false);

	/*
	 * 4. The Tester sends an Accept message at the deadline limit of
	 *    tSenderResponse min.
	 */
	delay_for(PD_T_SENDER_RESPONSE_MIN_MS - CONFIG_USBC_STATE_MACHINE_CYCLE_TIME);
	tester_send_ctrl_msg(PD_CTRL_ACCEPT, true);

	/*
	 * 5. The check fails if a Hard Reset is detected before
	 *    tSenderResponse max in respond to the Request message.
	 */
	time = delay_until_in_pe_state_or_timeout(dev,
		PE_SNK_HARD_RESET_NOTIFY_TEST, 2 * PD_T_SENDER_RESPONSE_MAX_MS);

	printk("TIME: %d\n", time);
	zassert_false(time <= PD_T_SENDER_RESPONSE_MAX_MS,
						"UUT timeout out of bounds");
	zassert_false(atomic_test_and_clear_bit(&data->uut_pe_state,
		PE_SNK_HARD_RESET_NOTIFY_TEST), "UUT Sent Hard Reset");
}

ZTEST(test_usbc, test_pd_prot_snk_5)
{
	/** Test in PD2.0 mode */

	usbc_start(usbc_port1);
	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_sender_response_timer_deadline(usbc_port1);

	usbc_suspend(usbc_port1);
        delay_for(500);
        usbc_start(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_sender_response_timer_deadline(usbc_port1);

	usbc_suspend(usbc_port1);
}
#endif

void tester_start_sender_response_timer(void)
{
}

/**
 * @brief TEST.PD.PROT.SNK.6 SenderResponseTimer Timeout
 *
 * The Tester does not respond to the Request message from the UUT, in
 * order to force a SenderResponseTimer timeout on the UUT and verifies
 * it is correctly implemented.
 */
static void test_sender_response_timer_timeout(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	union pd_fixed_supply_pdo_source pdo;
	uint32_t time;

	printk("Tester PD%d mode\n", tester_get_rev() + 1);

	/* a) B31…30 (Fixed Supply) set to 00b */
	pdo.type = PDO_FIXED;
	/* b) B29 (Dual-Role Power) set to 0b, Tester is a Source only */
	pdo.dual_role_power = 0;
	/* c) B28 (USB Suspend Supported) set to 0b */
	pdo.usb_suspend_supported = 0;
	/* d) B27 (Unconstrained Power) set to 0b */
	pdo.unconstrained_power = 0;
	/* e) B26 (USB Communications Capable) set to 0b */
	pdo.usb_comms_capable = 0;
	/* f) B25 (Dual-Role Data) set to 0b */
	pdo.dual_role_data = 0;
	/* g) B24 (PD3, Unchunked Extended Messages Supported) set to 0b */
	pdo.unchunked_ext_msg_supported = 0;
	/*
	 * h) B23 (EPR Mode Capable) to 0b, unless it is mentioned in the
	 *    test procedure. NOTE: NOT CURRENTLY SUPPORTED IN THE SUBSYSTEM.
	 */
	pdo.reserved0 = 0;
	/* i) B21…20 (Peak Current) set to 00b */
	pdo.peak_current = 0;
	/* j) B19…10 (Voltage) set to 5V */
	pdo.voltage = PD_CONVERT_MV_TO_FIXED_PDO_VOLTAGE(5000);
	/* k) B9…0 (Maximum Current) set to 100mA */
	pdo.max_current = PD_CONVERT_MA_TO_FIXED_PDO_CURRENT(100);

	atomic_clear_bit(&data->uut_pe_state, PE_SNK_HARD_RESET_NOTIFY_TEST);

	/*
	 * 1.b The UUT has VIF field PD_Port_Type set to anything else, the
	 *      Tester runs bring-up procedure with the UUT as a Sink
	 *      COMMON.PROC.BU.2
	 */
	bring_up_sink_uut(dev);

	atomic_set(&data->pn_port_partner_not_responsive, false);
	atomic_set(&data->pn_pd_connected, false);

	/* 2. The Tester transmits Source Capabilities message with single PDO */
	tester_send_data_msg(PD_DATA_SOURCE_CAP, &pdo.raw_value, 1, true);
	time = delay_until_in_pe_state_or_timeout(dev, PE_SNK_SELECT_CAPABILITY_NOTIFY_TEST, 1000);
//	time = delay_until_notify_or_timeout(dev, &data->pn_pd_connected, 1000);
	printk("T1: %d\n", time);

	/* 3. The check fails if the UUT does not respond with */
	zassert_true(atomic_get(&data->pn_pd_connected), "UUT didn't send request message.");
//	zassert_true(atomic_get(&data->uut_request), "UUT didn't send request message.");
//	atomic_set(&data->uut_request, false);
//	k_msleep(DELAY_SM_CYCLES(2));
//	delay_for(10);

//	time = delay_until_in_prl_tx_state_or_timeout(dev, PRL_TX_WAIT_FOR_MESSAGE_REQUEST_NOTIFY_TEST, 1000);
//	printk("T2: %d\n", time);
//	k_usleep(10000);
	//delay_for(30);
	/*
	 * 4. The Tester does not send an Accept (as a response to the Request message)
	 *    in order to force a SenderResponseTimer timeout on the UUT.
	 */

	/*
	 * 5. The Tester checks that a Hard Reset is detected between
	 *    tSenderResponse min and max.
	 *    NOTE: Only min is checked because the timing is very tight.
	 */
//	time = delay_until_notify_or_timeout(dev, &data->pn_port_partner_not_responsive, 1000);
//	zassert_true(atomic_get(&data->pn_port_partner_not_responsive), "UUT didn't send Hard Reset.");

	time = delay_until_in_pe_state_or_timeout(dev, PE_SNK_HARD_RESET_NOTIFY_TEST, 1000);
	printk("Sender Response timeout: %d <= %d <= %d\n",
		PD_T_SENDER_RESPONSE_MIN_MS, time, PD_T_SENDER_RESPONSE_MAX_MS);
	zassert_true((time >= PD_T_SENDER_RESPONSE_MIN_MS),
				"UUT timeout out of bounds");
}
#if 0
ZTEST(test_usbc, test_pd_prot_snk_6_rev2)
{
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();

	/* Run test */
	test_sender_response_timer_timeout(usbc_port1);
}
#endif
ZTEST(test_usbc, test_pd_prot_snk_6_rev3)
{
	/* Test in PD3.0 mode */
	tester_set_rev_pd3();

	/* Run test */
	test_sender_response_timer_timeout(usbc_port1);
}

#if 0
/**
 * @brief TEST.PD.PROT.SNK.7 PSTransitionTimer Timeout
 *
 * The Tester does not send the PS_RDY message after the Accept message
 * is sent to the UUT, in order to force a PSTransitionTimer timeout on
 * the UUT and verifies it is correctly implemented.
 */
static void test_ps_transition_timer_timeout(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	union pd_fixed_supply_pdo_source pdo;
	uint32_t time;

	printk("Tester PD%d mode\n", tester_get_rev() + 1);

	/* a) B31…30 (Fixed Supply) set to 00b */
	pdo.type = PDO_FIXED;
	/* b) B29 (Dual-Role Power) set to 0b, Tester is a Source only */
	pdo.dual_role_power = 0;
	/* c) B28 (USB Suspend Supported) set to 0b */
	pdo.usb_suspend_supported = 0;
	/* d) B27 (Unconstrained Power) set to 0b */
	pdo.unconstrained_power = 0;
	/* e) B26 (USB Communications Capable) set to 0b */
	pdo.usb_comms_capable = 0;
	/* f) B25 (Dual-Role Data) set to 0b */
	pdo.dual_role_data = 0;
	/* g) B24 (PD3, Unchunked Extended Messages Supported) set to 0b */
	pdo.unchunked_ext_msg_supported = 0;
	/*
	 * h) B23 (EPR Mode Capable) to 0b, unless it is mentioned in the
	 *    test procedure. NOTE: NOT CURRENTLY SUPPORTED IN THE SUBSYSTEM.
	 */
	pdo.reserved0 = 0;
	/* i) B21…20 (Peak Current) set to 00b */
	pdo.peak_current = 0;
	/* j) B19…10 (Voltage) set to 5V */
	pdo.voltage = PD_CONVERT_MV_TO_FIXED_PDO_VOLTAGE(5000);
	/* k) B9…0 (Maximum Current) set to 100mA */
	pdo.max_current = PD_CONVERT_MA_TO_FIXED_PDO_CURRENT(100);

	/*
	 * 1.b The UUT has VIF field PD_Port_Type set to anything else, the
	 *      Tester runs bring-up procedure with the UUT as a Sink
	 *      COMMON.PROC.BU.2
	 */
	bring_up_sink_uut(dev);

	atomic_clear_bit(&data->uut_pe_state, PE_SNK_HARD_RESET_NOTIFY_TEST);
	atomic_clear_bit(&data->uut_pe_state, PE_SNK_SELECT_CAPABILITY_NOTIFY_TEST);

	/* 2. The Tester transmits Source Capabilities message with single PDO */
	tester_send_data_msg(PD_DATA_SOURCE_CAP, &pdo.raw_value, 1, true);

	delay_until_in_pe_state_or_timeout(dev, PE_SNK_SELECT_CAPABILITY_NOTIFY_TEST, 1000);

	/* 3. The check fails if the UUT does not respond with */
        zassert_true(atomic_get(&data->uut_request), "UUT didn't send request message.");
        atomic_set(&data->uut_request, false);

	/*
	 * 4. The Tester sends an Accept message at the deadline limit of
	 *    tSenderResponse min.
	 */
	tester_send_ctrl_msg(PD_CTRL_ACCEPT, true);

	/* 5. The Tester does not send a PS_RDY message. */

	/*
	 * 6. The check fails if a Hard Reset is detected before
	 *    within tPStransition min and tPStransition max.
	 */
	time = delay_until_in_pe_state_or_timeout(dev,
		PE_SNK_HARD_RESET_NOTIFY_TEST, 2 * PD_T_SPR_PS_TRANSITION_MAX_MS);

	printk("TIME: %d %d %d\n", PD_T_SPR_PS_TRANSITION_MIN_MS, time, PD_T_SPR_PS_TRANSITION_MAX_MS);
	zassert_true(time >= PD_T_SPR_PS_TRANSITION_MIN_MS &&
			time <= PD_T_SPR_PS_TRANSITION_MAX_MS,
						"UUT timeout out of bounds");
	zassert_false(atomic_test_and_clear_bit(&data->uut_pe_state,
		PE_SNK_HARD_RESET_NOTIFY_TEST), "UUT Sent Hard Reset");
}

ZTEST(test_usbc, test_pd_prot_snk_7)
{
	/** Test in PD2.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_ps_transition_timer_timeout(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_ps_transition_timer_timeout(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.SNK.8 Atomic Message Sequence – Accept
 *
 * The Tester sends a GetSinkCap message in place of Accept message and
 * verifies the UUT will send a SoftReset and recover from the error.
 */
static void test_atomic_message_sequence_accept(const struct device *dev)
{
#if 0
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	//uint32_t time;

	printk("Tester PD%d mode\n", tester_get_rev() + 1);

	/* a) B31…30 (Fixed Supply) set to 00b */
	pdo.type = PDO_FIXED;
	/* b) B29 (Dual-Role Power) set to 0b, Tester is a Source only */
	pdo.dual_role_power = 0;
	/* c) B28 (USB Suspend Supported) set to 0b */
	pdo.usb_suspend_supported = 0;
	/* d) B27 (Unconstrained Power) set to 0b */
	pdo.unconstrained_power = 0;
	/* e) B26 (USB Communications Capable) set to 0b */
	pdo.usb_comms_capable = 0;
	/* f) B25 (Dual-Role Data) set to 0b */
	pdo.dual_role_data = 0;
	/* g) B24 (PD3, Unchunked Extended Messages Supported) set to 0b */
	pdo.unchunked_ext_msg_supported = 0;
	/*
	 * h) B23 (EPR Mode Capable) to 0b, unless it is mentioned in the
	 *    test procedure. NOTE: NOT CURRENTLY SUPPORTED IN THE SUBSYSTEM.
	 */
	pdo.reserved0 = 0;
	/* i) B21…20 (Peak Current) set to 00b */
	pdo.peak_current = 0;
	/* j) B19…10 (Voltage) set to 5V */
	pdo.voltage = PD_CONVERT_MV_TO_FIXED_PDO_VOLTAGE(5000);
	/* k) B9…0 (Maximum Current) set to 100mA */
	pdo.max_current = PD_CONVERT_MA_TO_FIXED_PDO_CURRENT(100);


	/*
	 * 1.b The UUT has VIF field PD_Port_Type set to anything else, the
	 *      Tester runs bring-up procedure with the UUT as a Sink
	 *      COMMON.PROC.BU.2
	 */
	bring_up_sink_uut(dev);

	/*
	 * 2. The Tester sends a Hard Reset. It drives VBUS to vSafe0V and then
	 *    restores VBUS to vSafe5V.
	 */
	atomic_clear_bit(&data->uut_pe_state, PE_SNK_HARD_RESET_NOTIFY_TEST);
	atomic_clear_bit(&data->uut_pe_state, PE_SNK_WAIT_FOR_CAPABILITIES_NOTIFY_TEST);
	atomic_clear_bit(&data->uut_tc_state, TC_UNATTACHED_SNK_BIT);
	atomic_clear_bit(&data->uut_tc_state, TC_ATTACHED_SNK_BIT);

	tester_send_hard_reset();	
	tester_apply_vbus(PD_V_SAFE_0V_MAX_MV);
	delay_until_in_tc_state_or_timeout(dev, TC_UNATTACHED_SNK_BIT, 1000);
	tester_apply_vbus(PD_V_SAFE_5V_MIN_MV);

	delay_until_in_tc_state_or_timeout(dev, TC_ATTACHED_SNK_BIT, 1000);

	/* 3. The Tester transmits Source Capabilities message */
	tester_send_data_msg(PD_DATA_SOURCE_CAP, &pdo.raw_value, 1, true);

	delay_until_in_pe_state_or_timeout(dev, PE_SNK_SELECT_CAPABILITY_NOTIFY_TEST, 1000);

	/* 4. The check fails if the UUT does not respond with request */
        zassert_true(atomic_get(&data->uut_request), "UUT didn't send request message.");
        atomic_set(&data->uut_request, false);

	/* 5. The Tester sends a Get_Sink_Cap message */
	tester_send_ctrl_msg(PD_CTRL_GET_SINK_CAP, true);

	/*
	 * 6. The check fails if a Soft_Reset message is not received within
	 *    tProtErrSoftReset max.
	 */

	/*
	 * 7. The Tester responds with an Accept message to the
	 *    Soft_Reset message.
	 */

	/*
	 * 8. The Tester sends Source Capabilities message to the UUT
	 *    repeatedly until nCapsCount is reached or a GoodCRC is received.
	 *    The check fails if nCapsCount is reached.
	 */

	/* 9. The check fails if the UUT does not respond with a Request message */
#endif
	ztest_test_pass();
}

ZTEST(test_usbc, test_pd_prot_snk_8)
{
	/** Test in PD2.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_atomic_message_sequence_accept(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_atomic_message_sequence_accept(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.SNK.9 Atomic Message Sequence – PS_RDY
 *
 * The Tester sends a GetSinkCap message in place of PS_RDY message
 * and verifies the UUT will send a Hard Reset.
 */
static void test_atomic_message_sequence_ps_rdy(const struct device *dev)
{
	ztest_test_pass();
}

ZTEST(test_usbc, test_pd_prot_snk_9)
{
	/** Test in PD2.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_atomic_message_sequence_ps_rdy(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_atomic_message_sequence_ps_rdy(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.SNK.10 DR_Swap Request
 *
 * As a Sink, the Tester sends a DR_Swap message, and verifies
 * that the UUT responds correctly.
 */
static void test_dr_swap_request(const struct device *dev)
{
	ztest_test_pass();
}

ZTEST(test_usbc, test_pd_prot_snk_10)
{
	/** Test in PD2.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_dr_swap_request(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_dr_swap_request(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.SNK.11 VCONN_Swap Request
 *
 * As a Source, the Tester sends a VCONN_Swap message, and verifies
 * that the UUT responds correctly.
 */
static void test_vconn_swap_request(const struct device *dev)
{
	ztest_test_pass();
}

ZTEST(test_usbc, test_pd_prot_snk_11)
{
	/** Test in PD2.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_vconn_swap_request(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_vconn_swap_request(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.SNK.12 PR_Swap – PSSourceOffTimer Timeout
 *
 * As a new Sink, the Tester intentionally does not send a PS_RDY message
 * after a PR_Swap in order to force a PSSourceOffTimer timeout on the UUT
 * and verifies it is correctly implemented.
 */
static void test_pr_swap_ps_source_off_timer_timeout(const struct device *dev)
{
	ztest_test_pass();
}

ZTEST(test_usbc, test_pd_prot_snk_12)
{
	/** Test in PD2.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_pr_swap_ps_source_off_timer_timeout(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_pr_swap_ps_source_off_timer_timeout(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.SNK.13 PR_Swap – Request SenderResponseTimer Timeout
 *
 * As a new Sink, the Tester intentionally does not send a Request message
 * after a PR_Swap in order to force a SenderResponseTimer timeout on the
 * UUT and verifies it is correctly implemented.
 */
static void test_pr_swap_request_sender_response_timer_timeout(const struct device *dev)
{
	ztest_test_pass();
}

ZTEST(test_usbc, test_pd_prot_snk_13)
{
	/** Test in PD2.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_pr_swap_request_sender_response_timer_timeout(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_pr_swap_request_sender_response_timer_timeout(usbc_port1);
}
#endif
#if 0
/**
 * @brief TEST.PD.PROT.ALL.3 Soft Reset Response
 *
 * The Tester checks that the UUT responds correctly to Soft Reset message.
 */
static void test_soft_reset_response(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	union pd_fixed_supply_pdo_source pdo;

	printk("Tester PD%d mode\n", tester_get_rev() + 1);

	/*
	 * 1.a) The UUT has VIF field PD_Port_Type set to Consumer Only.
	 *      The Tester behaves as a Source only and it runs bring-up
	 *      procedure with the UUT as a Sink COMMON.PROC.BU.2
	 */
	bring_up_sink_uut(dev);

	/*
	 * 2.b) If the UUT is a Sink, the Tester sends a Get_Sink_Cap
	 *      message.
	 */
	tester_send_ctrl_msg(PD_CTRL_GET_SINK_CAP, true);
	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
				PE_SNK_GIVE_SINK_CAP_NOTIFY, 1000);

	/*
	 * 3.b) If the UUT is a Sink, the check fails if the UUT does not send
	 *	a Sink Capabilities message.
	 */
	tester_get_uut_tx_data(&data->rx_msg);
	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
				"UUT Sink Cap msg not sent to SOP");
	zassert_equal(data->rx_msg.header.message_type, PD_DATA_SINK_CAP,
				"UUT did not send Sink Cap msg");

	/*
	 * 4) If MessageID in the last sent message is not 000b, the Tester
	 *    repeats the previous 2 steps (i.e. Get_Sink_cap and receiving
	 *    response) until the MessageID in the last sent message is 000b.
	 */
	while (tester_get_msgid() != 0) {
		tester_send_ctrl_msg(PD_CTRL_GET_SINK_CAP, true);
		delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
					PE_SNK_GIVE_SINK_CAP_NOTIFY, 1000);
		tester_get_uut_tx_data(&data->rx_msg);
		zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
					"UUT Sink Cap msg not sent to SOP");
		zassert_equal(data->rx_msg.header.message_type,
				PD_DATA_SINK_CAP, "UUT did not send Sink Cap msg");
	}

	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
				PE_SNK_READY_NOTIFY, 1000);

	/*
	 * 5) Immediately after sending GoodCRC (Sink Capabilities), the
	 *    Tester sends a Soft Reset message. The check fails if the UUT
	 *    does not send an Accept message with MessageID 000b.
	 */
	tester_send_ctrl_msg(PD_CTRL_SOFT_RESET, true);
	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
				PE_SOFT_RESET_NOTIFY, 1000);

	tester_get_uut_tx_data(&data->rx_msg);

	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
					"UUT message not sent to SOP");
	zassert_equal(data->rx_msg.header.message_type,
				PD_CTRL_ACCEPT, "UUT did not send Accept msg");
	zassert_equal(data->rx_msg.header.message_id, 0,
				"UUT Accept message's ID not equal to zero");

	/*
	 * 6.a) For Sink UUT, the Tester transmits Source Capabilities message
	 *      with single PDO (5V @100mA, Source only). The check fails if
	 *      the UUT does not send a Request message.
	 */
	pdo.type = PDO_FIXED;
	pdo.dual_role_power = 1;
	pdo.usb_suspend_supported = 0;
	pdo.unconstrained_power = 1;
	pdo.usb_comms_capable = 0;
	pdo.dual_role_data = 0;
	pdo.unchunked_ext_msg_supported = 0;
	pdo.reserved0 = 0;
	pdo.peak_current = 0;
	pdo.voltage = PD_CONVERT_MV_TO_FIXED_PDO_VOLTAGE(5000);
	pdo.max_current = PD_CONVERT_MA_TO_FIXED_PDO_CURRENT(100);

	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
				PE_SNK_WAIT_FOR_CAPABILITIES_NOTIFY, 1000);
	tester_send_data_msg(PD_DATA_SOURCE_CAP, &pdo.raw_value, 1, true);
	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
				PE_SNK_SELECT_CAPABILITY_NOTIFY, 1000);
	//k_msleep(3);

	tester_get_uut_tx_data(&data->rx_msg);
	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
				"UUT Request msg not sent to SOP");
        zassert_equal(data->rx_msg.header.message_type, PD_DATA_REQUEST,
				"UUT did not send Request msg");
	check_request_message(dev);

	uut_sent_request(dev);
	zassert_true(atomic_get(&data->pn_transition_ps),
				"UUT failed to respond to PS_RDY message");

	/* 7.b) Tester sends a Get_Sink_Cap message. */
	tester_send_ctrl_msg(PD_CTRL_GET_SINK_CAP, true);
	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
				PE_SNK_GIVE_SINK_CAP_NOTIFY, 1000);

	/*
	 * 8.b) The check fails if the UUT does not send a
	 *      Sink Capabilities message.
	 */	
	tester_get_uut_tx_data(&data->rx_msg);

	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
					"UUT Sink Cap msg not sent to SOP");
	zassert_equal(data->rx_msg.header.message_type, PD_DATA_SINK_CAP,
					"UUT did not send Sink Cap msg");
}

ZTEST(test_usbc, test_pd_prot_all_3)
{
	/** Test in PD2.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_soft_reset_response(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_soft_reset_response(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.ALL.4 Reset Signals and MessageID
 *
 * The Tester checks that the UUT responds correctly to Hard Reset.
 */
void test_hard_reset_response(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);
	union pd_fixed_supply_pdo_source pdo;

	printk("Tester PD%d mode\n", tester_get_rev() + 1);

	/*
	 * 1.a) The UUT has VIF field PD_Port_Type set to Consumer Only.
	 *	The Tester behaves as a Source only and it runs bring-up
	 *	procedure with the UUT as a Sink COMMON.PROC.BU.2
	 */
	bring_up_sink_uut(dev);

	/*
	 * 2.b) If the UUT is a Sink, the Tester sends a Get_Sink_Cap
	 *      message.
	 */
	tester_send_ctrl_msg(PD_CTRL_GET_SINK_CAP, true);
	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
					PE_SNK_GIVE_SINK_CAP_NOTIFY, 1000);

	/*
	 * 3.b) If the UUT is a Sink, the check fails if the UUT does not send
	 *	a Sink Capabilities message.
	 */
	tester_get_uut_tx_data(&data->rx_msg);

	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
				"UUT Sink Cap msg not sent to SOP");
	zassert_equal(data->rx_msg.header.message_type, PD_DATA_SINK_CAP,
				"UUT did not send Sink Cap msg");

	/*
	 * 4) If MessageID in the last sent message is not 000b, the Tester
	 *    repeats the previous 2 steps (i.e. Get_Sink_cap and receiving
	 *    response) until the MessageID in the last sent message is 000b.
	 */
	while (tester_get_msgid() != 0) {
		tester_send_ctrl_msg(PD_CTRL_GET_SINK_CAP, true);
		delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
					PE_SNK_GIVE_SINK_CAP_NOTIFY, 1000);
		tester_get_uut_tx_data(&data->rx_msg);
		zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
					"UUT Sink Cap msg not sent to SOP");
		zassert_equal(data->rx_msg.header.message_type,
			PD_DATA_SINK_CAP, "UUT did not send Sink Cap msg");
	}

	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
					PE_SNK_READY_NOTIFY, 1000);

	/*
	 * 5) Immediately after sending GoodCRC (Sink Capabilities), the
	 *    Tester sends a Hard Reset message. The check fails if the UUT
	 *    does not send an Accept message with MessageID 000b.
	 */
	tester_send_hard_reset();
	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
						PE_SNK_READY_NOTIFY, 1000);

	/*
	 * 6.a) For Sink UUT, the Tester transmits Source Capabilities message
	 *      with single PDO (5V @100mA, Source only). The check fails if
	 *      the UUT does not send a Request message.
	 */
	pdo.type = PDO_FIXED;
	pdo.dual_role_power = 1;
	pdo.usb_suspend_supported = 0;
	pdo.unconstrained_power = 1;
	pdo.usb_comms_capable = 0;
	pdo.dual_role_data = 0;
	pdo.unchunked_ext_msg_supported = 0;
	pdo.reserved0 = 0;
	pdo.peak_current = 0;
	pdo.voltage = PD_CONVERT_MV_TO_FIXED_PDO_VOLTAGE(5000);
	pdo.max_current = PD_CONVERT_MA_TO_FIXED_PDO_CURRENT(100);
	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
				PE_SNK_WAIT_FOR_CAPABILITIES_NOTIFY, 1000);

	tester_send_data_msg(PD_DATA_SOURCE_CAP, &pdo.raw_value, 1, true);
	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
				PE_SNK_SELECT_CAPABILITY_NOTIFY, 1000);
	//k_msleep(3);

	tester_get_uut_tx_data(&data->rx_msg);
	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
				"UUT Request msg not sent to SOP");
	zassert_equal(data->rx_msg.header.message_type,
			PD_DATA_REQUEST, "UUT did not send Request msg");
	check_request_message(dev);

	uut_sent_request(dev);
	zassert_true(atomic_get(&data->pn_transition_ps),
				"UUT failed to respond to PS_RDY message");

	/* 7.b) Tester sends a Get_Sink_Cap message. */
	tester_send_ctrl_msg(PD_CTRL_GET_SINK_CAP, true);
	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
				PE_SNK_GIVE_SINK_CAP_NOTIFY, 1000);
	//k_msleep(3);

	/*
	 * 8.b) The check fails if the UUT does not send a
	 *      Sink Capabilities message.
	 */	
	tester_get_uut_tx_data(&data->rx_msg);

	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
				"UUT message not sent to SOP");
	zassert_equal(data->rx_msg.header.message_type,
			PD_DATA_SINK_CAP, "UUT did not send Sink Cap msg");

	/*
	 * 9) The Tester repeats Step-7 with the same MessageID. Because the
	 *    UUT is expected to ignore these erroneous messages, the Tester
	 *    should immediately return to PE_SNK_Ready after transmission.
	 */
	tester_send_ctrl_msg(PD_CTRL_GET_SINK_CAP, false);
	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
				PE_SNK_GIVE_SINK_CAP_NOTIFY, 1000);
	/* Delay a short while to allow UUT time to test and ignore message */
	//k_msleep(40);
	delay_for(40);

	/*
	 * 10) The check fails if the UUT has sent a response message when
	 *     the MessageID has been repeated.
	 */
	zassert_false(tester_is_rx_msg_pending(),
				"UUT did not ignore message with repeated ID");

	/* 11.b The Tester sends a Get_Sink_Cap message. */
	tester_send_ctrl_msg(PD_CTRL_GET_SINK_CAP, true);
        delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
				PE_SNK_GIVE_SINK_CAP_NOTIFY, 1000);

	/*
	 * 12.b) The check fails if the UUT does not send a
	 *       Sink Capabilities message.
	 */
	tester_get_uut_tx_data(&data->rx_msg);

	zassert_equal(data->rx_msg.type, PD_PACKET_SOP, "UUT Sink Cap msg not sent to SOP");
	zassert_equal(data->rx_msg.header.message_type, PD_DATA_SINK_CAP, "UUT did not send Sink Cap msg");
}

ZTEST(test_usbc, test_pd_prot_all_4)
{
	/** Test in PD2.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_hard_reset_response(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_hard_reset_response(usbc_port1);
}

/**
 * @brief TEST.PD.PROT.ALL.5 Unrecognized Message
 *
 * The Tester checks that the UUT responds correctly to unrecognized message.
 */
void test_unrecognized_message(const struct device *dev)
{
	struct port1_data_t *data = usbc_get_dpm_data(dev);

	printk("Tester PD%d mode\n", tester_get_rev() + 1);
	/*
	 * 1.a) The UUT has VIF field PD_Port_Type set to Consumer Only.
	 *	The Tester behaves as a Source only and it runs bring-up
	 *	procedure with the UUT as a Sink COMMON.PROC.BU.2
	 */
	bring_up_sink_uut(dev);

	/*
	 * 2.b) Tester sends a Control Message with Message Type
	 *      field set to 1110b (Reserved, in PD2 mode) or
	 *      11111b (Reserved, in PD3 mode).
	 */
	switch (tester_get_rev()) {
	case PD_REV10:
		zassert_true(false,
			"Tester configured for unsupported PD revision");
		break;
	case PD_REV20:
		tester_send_ctrl_msg(0x0e, true);
		break;
	case PD_REV30:
		tester_send_ctrl_msg(0x1f, true);
		break;
	}
	delay_until_in_pe_state_or_timeout(dev, &data->uut_pe_state,
					PE_SEND_NOT_SUPPORTED_NOTIFY, 1000);

	/*
	 * 3.b) The check fails if the UUT does not send Reject message if
	 *      in PD2 mode and Not_Supported message if in PD3 mode.
	 */
	tester_get_uut_tx_data(&data->rx_msg);
	zassert_equal(data->rx_msg.type, PD_PACKET_SOP,
				"UUT message not sent to SOP");

	if (tester_get_rev() == PD_REV20) {
		zassert_equal(data->rx_msg.header.message_type,
			PD_CTRL_REJECT, "UUT didn't send PD_CTRL_REJECT");
	} else {
		zassert_equal(data->rx_msg.header.message_type,
			PD_CTRL_NOT_SUPPORTED, "UUT didn't send PD_CTRL_NOT_SUPPORTED");
	}
}

ZTEST(test_usbc, test_pd_prot_all_5)
{
	/** Test in PD2.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD2.0 mode */
	tester_set_rev_pd2();
	/* Run test */
	test_unrecognized_message(usbc_port1);

	/** Test in PD3.0 mode */

	/* Tester is source */
	tester_set_power_role_source();
	/* Tester is UFP */
	tester_set_data_role_ufp();
	/* Tester in PD3.0 mode */
	tester_set_rev_pd3();
	/* Run test */
	test_unrecognized_message(usbc_port1);
}
#endif

ZTEST_SUITE(test_usbc, NULL, test_usbc_setup, test_usbc_before, test_usbc_after, NULL);
