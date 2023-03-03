#include <zephyr/kernel.h>
#include <string.h>
#include <stddef.h>
#include <zephyr/drivers/usb_c/usbc_pd.h>

#include "mock_tester.h"
#include "mock_tcpc.h"

struct mock_tester_t {
	struct mock_tcpc_data *tcpc;
	enum tc_cc_voltage_state cc1;
	enum tc_cc_voltage_state cc2;
	int vbus;
	struct pd_msg tx_msg;
	struct pd_msg rx_msg;

	int msg_id;
	enum pd_rev_type rev;
	enum tc_power_role power_role;
	enum tc_data_role data_role;
};

static struct mock_tester_t tester;

void tester_msgid_set(int id)
{
	tester.tx_msg.header.message_id = id & 7;
}

void tester_msgid_inc(void)
{
	tester.tx_msg.header.message_id++;
	tester.tx_msg.header.message_id &= 7;
}

int tester_get_msgid(void)
{
	return tester.tx_msg.header.message_id;
}

void tester_transmit_data(struct pd_msg *msg)
{
	if (tester.tcpc->rx_enable) {
		memcpy(&tester.tcpc->rx_msg, msg, sizeof(struct pd_msg));
		tester.tcpc->pending_rx_msg = true;
	}
}

void tester_send_hard_reset(void)
{
	memset(&tester.tcpc->rx_msg, 0, sizeof(struct pd_msg));
	tester.tcpc->rx_msg.type = PD_PACKET_TX_HARD_RESET;
	tester.tcpc->pending_rx_msg = true;
}

void tester_send_ctrl_msg(enum pd_ctrl_msg_type type, bool inc_msgid)
{
	tester.tx_msg.type = PD_PACKET_SOP;
	tester.tx_msg.header.message_type = type;
	tester.tx_msg.header.port_data_role = tester.data_role;
	tester.tx_msg.header.specification_revision = tester.rev;
	tester.tx_msg.header.port_power_role = tester.power_role;
	if (inc_msgid) {
		tester_msgid_inc();
	}
	tester.tx_msg.header.number_of_data_objects = 0;
	tester.tx_msg.header.extended = 0;
	tester.tx_msg.len = 0;

	tester_transmit_data(&tester.tx_msg);
}

void tester_send_data_msg(enum pd_data_msg_type type, uint32_t *data, uint32_t len, bool inc_msgid)
{
	tester.tx_msg.type = PD_PACKET_SOP;
	tester.tx_msg.header.message_type = type;
	tester.tx_msg.header.port_data_role = tester.data_role;
	tester.tx_msg.header.specification_revision = tester.rev;
	tester.tx_msg.header.port_power_role = tester.power_role;
	if (inc_msgid) {
		tester_msgid_inc();
	}
	tester.tx_msg.header.number_of_data_objects = len;
	tester.tx_msg.header.extended = 0;
	tester.tx_msg.len = len * 4;
	memcpy(tester.tx_msg.data, (uint8_t *)data, len * 4);

	tester_transmit_data(&tester.tx_msg);
}

enum pd_rev_type tester_get_rev(void)
{
	return tester.rev;
}

void tester_set_rev_pd2(void)
{
	tester.rev = PD_REV20;
}

void tester_set_rev_pd3(void)
{
        tester.rev = PD_REV30;
}

void tester_set_power_role_source(void)
{
	tester.power_role = TC_ROLE_SOURCE;
}

void tester_set_data_role_ufp(void)
{
	tester.data_role = TC_ROLE_UFP;
}

void tester_set_data_role_dfp(void)
{
	tester.data_role = TC_ROLE_DFP;
}

void tester_set_tcpc_device(struct mock_tcpc_data *tcpc)
{
	tester.tcpc = tcpc;
}

void tester_disconnected(void)
{
	tester_apply_vbus(0);
	tester_apply_cc(TC_CC_VOLT_OPEN, TC_CC_VOLT_OPEN);
	tester_msgid_set(0);
}

void tester_apply_cc(enum tc_cc_voltage_state cc1,
		     enum tc_cc_voltage_state cc2)
{
        tester.cc1 = cc1;
        tester.cc2 = cc2;
}

void tester_apply_rp(enum tc_rp_value cc1, enum tc_rp_value cc2)
{
	switch (cc1) {
	case TC_RP_USB:
		tester.cc1 = TC_CC_VOLT_RP_DEF;
		break;
	case TC_RP_1A5:
		tester.cc1 = TC_CC_VOLT_RP_1A5;
		break;
	case TC_RP_3A0:
		tester.cc1 = TC_CC_VOLT_RP_3A0;
		break;
	case TC_RP_RESERVED:
		tester.cc1 = TC_CC_VOLT_OPEN;
		break;
	}

	switch (cc2) {
	case TC_RP_USB:
		tester.cc2 = TC_CC_VOLT_RP_DEF;
		break;
	case TC_RP_1A5:
		tester.cc2 = TC_CC_VOLT_RP_1A5;
		break;
	case TC_RP_3A0:
		tester.cc2 = TC_CC_VOLT_RP_3A0;
		break;
	case TC_RP_RESERVED:
		tester.cc2 = TC_CC_VOLT_OPEN;
		break;
	}
}

enum tc_cc_voltage_state tester_get_cc1(void)
{
	return tester.cc1;
}

enum tc_cc_voltage_state tester_get_cc2(void)
{
	return tester.cc2;
}

void tester_apply_vbus_level(enum tc_vbus_level level)
{
	switch (level) {
	case TC_VBUS_SAFE0V:
		tester.vbus = PD_V_SAFE_0V_MAX_MV - 1;
		break;
	case TC_VBUS_PRESENT:
		tester.vbus = PD_V_SAFE_5V_MIN_MV;
		break;
	case TC_VBUS_REMOVED:
		tester.vbus = TC_V_SINK_DISCONNECT_MIN_MV - 1;
		break;
	}
}

void tester_apply_vbus(int mv)
{
	tester.vbus = mv;
}

int tester_get_vbus(void)
{
	return tester.vbus;
}

void tester_get_uut_tx_data(struct pd_msg *msg)
{
	int loop = 0;

	do {
		loop++;
		k_msleep(1);
	} while ((loop < 500) && tester.tcpc->pending_tx_msg == false);

	tester.tcpc->pending_tx_msg = false;
	memcpy(msg, &tester.tcpc->tx_msg, sizeof(struct pd_msg));
}

bool tester_is_rx_msg_pending(void)
{
	return tester.tcpc->pending_rx_msg;
}
