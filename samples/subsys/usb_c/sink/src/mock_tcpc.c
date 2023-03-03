/*
 * Copyright 2022 The Chromium OS Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mock_tcpc

#include <zephyr/logging/log.h>

#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <stddef.h>

#include "mock_tcpc.h"
#include "mock_tester.h"

enum tcpc_state_t test_state;

/**
 * @brief Get the state of the CC1 and CC2 lines
 *
 * @retval 0 on success
 * @retval -EIO on failure
 */
static int mt_get_cc(const struct device *dev,
		       enum tc_cc_voltage_state *cc1,
		       enum tc_cc_voltage_state *cc2)
{
	*cc1 = tester_get_cc1();
	*cc2 = tester_get_cc2();

	return 0;
}

/**
 * @brief Get the CC enable mask. The mask indicates which CC line
 *        is enabled.
 *
 * @retval CC Enable mask (bit 0: CC1, bit 1: CC2)
 */

/**
 * @brief Enable or Disable VCONN
 *
 * @retval 0 on success
 * @retval -EIO on failure
 * @retval -ENOTSUP if not supported
 */
static int mt_set_vconn(const struct device *dev, bool enable)
{
//	const struct mock_tcpc_config *const config = dev->config;
//	struct mock_tcpc_data *data = dev->data;

	return 0; //ret;
}

/**
 * @brief Sets the value of the CC pull up resistor used when operating as a Source
 *
 * @retval 0 on success
 */
static int mt_select_rp_value(const struct device *dev, enum tc_rp_value rp)
{
//	const struct mock_tcpc_config *const config = dev->config;
//	struct mock_tcpc_data *data = dev->data;

//	data->rp = rp;

	return 0;
}

/**
 * @brief Gets the value of the CC pull up resistor used when operating as a Source
 *
 * @retval 0 on success
 */
static int mt_get_rp_value(const struct device *dev, enum tc_rp_value *rp)
{
//	struct tcpc_data *data = dev->data;

//	*rp = data->rp;

	return 0;
}

/**
 * @brief Set the CC pull up or pull down resistors
 *
 * @retval 0 on success
 * @retval -EIO on failure
 */
static int mt_set_cc(const struct device *dev,
		       enum tc_cc_pull cc_pull)
{
//	const struct mock_tcpc_config *const config = dev->config;
//	struct mock_tcpc_data *data = dev->data;
#if 0
	const struct tcpc_config *const config = dev->config;
	struct tcpc_data *data = dev->data;
	uint32_t cr;

	cr = LL_UCPD_ReadReg(config->ucpd_port, CR);

	/*
	 * Always set ANASUBMODE to match desired Rp. TCPM layer has a valid
	 * range of 0, 1, or 2. This range maps to 1, 2, or 3 in ucpd for
	 * ANASUBMODE.
	 */
	cr &= ~UCPD_CR_ANASUBMODE_Msk;
	cr |= STM32_UCPD_CR_ANASUBMODE_VAL(UCPD_RP_TO_ANASUB(data->rp));

	/* Disconnect both pull from both CC lines for R_open case */
	cr &= ~UCPD_CR_CCENABLE_Msk;
	/* Set ANAMODE if cc_pull is Rd */
	if (cc_pull == TC_CC_RD) {
		cr |= (UCPD_CR_ANAMODE | UCPD_CR_CCENABLE_Msk);
		/* Clear ANAMODE if cc_pull is Rp */
	} else if (cc_pull == TC_CC_RP) {
		cr &= ~(UCPD_CR_ANAMODE);
		cr |= ucpd_get_cc_enable_mask(dev);
	}

	/* Update pull values */
	LL_UCPD_WriteReg(config->ucpd_port, CR, cr);

#ifdef CONFIG_SOC_SERIES_STM32G0X
	update_stm32g0x_cc_line(config->ucpd_port);
#endif
#endif
	return 0;
}

/**
 * @brief Set the polarity of the CC line, which is the active CC line
 *        used for power delivery.
 *
 * @retval 0 on success
 * @retval -EIO on failure
 * @retval -ENOTSUP if polarity is not supported
 */
static int mt_cc_set_polarity(const struct device *dev,
				enum tc_cc_polarity polarity)
{
//	const struct mock_tcpc_config *const config = dev->config;
//	struct mock_tcpc_data *data = dev->data;
#if 0
	const struct tcpc_config *const config = dev->config;
	uint32_t cr;

	cr = LL_UCPD_ReadReg(config->ucpd_port, CR);

	/*
	 * Polarity impacts the PHYCCSEL, CCENABLE, and CCxTCDIS fields. This
	 * function is called when polarity is updated at TCPM layer. STM32Gx
	 * only supports POLARITY_CC1 or POLARITY_CC2 and this is stored in the
	 * PHYCCSEL bit in the CR register.
	 */

	if (polarity == TC_POLARITY_CC1) {
		cr &= ~UCPD_CR_PHYCCSEL;
	} else if (polarity == TC_POLARITY_CC2) {
		cr |= UCPD_CR_PHYCCSEL;
	} else {
		return -ENOTSUP;
	}

	/* Update polarity */
	LL_UCPD_WriteReg(config->ucpd_port, CR, cr);
#endif
	return 0;
}

/**
 * @brief Enable or Disable Power Delivery
 *
 * @retval 0 on success
 * @retval -EIO on failure
 */
static int mt_set_rx_enable(const struct device *dev, bool enable)
{
	struct mock_tcpc_data *data = dev->data;

	data->rx_enable = enable;

	return 0;
}

/**
 * @brief Set the Power and Data role used when sending goodCRC messages
 *
 * @retval 0 on success
 * @retval -EIO on failure
 */
static int mt_set_roles(const struct device *dev,
			  enum tc_power_role power_role,
			  enum tc_data_role data_role)
{
	struct mock_tcpc_data *data = dev->data;

	data->msg_header.pr = power_role;
	data->msg_header.dr = data_role;

	return 0;
}

/**
 * @brief Enable the reception of SOP Prime messages
 *
 * @retval 0 on success
 * @retval -EIO on failure
 */
static int mt_sop_prime_enable(const struct device *dev, bool enable)
{
//	const struct mock_tcpc_config *const config = dev->config;
//	struct mock_tcpc_data *data = dev->data;

	/* Update static variable used to filter SOP//SOP'' messages */
//	data->ucpd_rx_sop_prime_enabled = enable;

	return 0;
}

/**
 * @brief Transmit a power delivery message
 *
 * @retval 0 on success
 * @retval -EFAULT on failure
 */
static int mt_transmit_data(const struct device *dev, struct pd_msg *msg)
{
	struct mock_tcpc_data *data = dev->data;

	switch (test_state) {
	case TS_SEND_GOODCRC:
		memcpy(&data->tx_msg, msg, sizeof(struct pd_msg));
		data->alert_handler(dev, data->alert_data,
					TCPC_ALERT_TRANSMIT_MSG_SUCCESS);
		tester_start_sender_response_timer();
		data->pending_tx_msg = true;
		break;
	case TS_DONT_SEND_GOODCRC:
		break;
	}

	return 0;
}

static bool mt_is_rx_pending_msg(const struct device *dev,
                                   enum pd_packet_type *type)
{
	struct mock_tcpc_data *data = dev->data;

	if (data->pending_rx_msg == false) {
		return false;
	}

	if (type != NULL) {
		*type = data->rx_msg.type;
	}

	return true;
}

/**
 * @brief Retrieves the Power Delivery message from the TCPC
 *	  The UUT calls this function in the Protocol Layer
 *	  to receive the message from the TCPC.
 *
 * @retval number of bytes received
 * @retval -EIO on no message to retrieve
 * @retval -EFAULT on buf being NULL
 */
static int mt_receive_data(const struct device *dev, struct pd_msg *msg)
{
	struct mock_tcpc_data *data = dev->data;

	if (msg == NULL) {
		return -EFAULT;
	}

	/* Make sure we have a message to retrieve */
	if (mt_is_rx_pending_msg(dev, NULL) == false) {
		return -EIO;
	}

	data->pending_rx_msg = false;

	if (data->rx_msg.type == PD_PACKET_TX_HARD_RESET) {
		data->alert_handler(dev, data->alert_data,
			TCPC_ALERT_HARD_RESET_RECEIVED);
		return -EIO;
	}

	memcpy(msg, &data->rx_msg, sizeof(struct pd_msg));

	return msg->len + MSG_HEADER_SIZE;
}

/**
 * @brief Enable or Disable BIST Test mode
 *
 * return 0 on success
 * return -EIO on failure
 */
static int mt_set_bist_test_mode(const struct device *dev,
				   bool enable)
{
//	const struct mock_tcpc_config *const config = dev->config;
//	struct mock_tcpc_data *data = dev->data;

	return 0;
}

/**
 * @brief Dump a set of TCPC registers
 *
 * @retval 0 on success
 * @retval -EIO on failure
 */
static int mt_dump_std_reg(const struct device *dev)
{

	return 0;
}

/**
 * @brief Sets the alert function that's called when an interrupt is triggered
 *        due to a TCPC alert
 *
 * @retval 0 on success
 * @retval -EINVAL on failure
 */
static int mt_set_alert_handler_cb(const struct device *dev,
				     tcpc_alert_handler_cb_t handler, void *alert_data)
{
	struct mock_tcpc_data *data = dev->data;

	data->alert_handler = handler;
	data->alert_data = alert_data;

	return 0;
}

/**
 * @brief Sets a callback that can enable or disable VCONN if the TCPC is
 *        unable to or the system is configured in a way that does not use
 *        the VCONN control capabilities of the TCPC
 *
 */
static void mt_set_vconn_cb(const struct device *dev,
			      tcpc_vconn_control_cb_t vconn_cb)
{
//	const struct mock_tcpc_config *const config = dev->config;
//	struct mock_tcpc_data *data = dev->data;
}

/**
 * @brief Initializes the MOCK TCPC
 *
 * @retval 0 on success
 * @retval -EIO on failure
 */
static int mt_init(const struct device *dev)
{
	struct mock_tcpc_data *data = dev->data;

	test_state = TS_SEND_GOODCRC;

	/* Give tester access to the mock tcpc device */
	tester_set_tcpc_device(data);
	data->pending_rx_msg = false;
	data->pending_tx_msg = false;
	return 0;
}

void tc_set_state(enum tcpc_state_t ts)
{
	test_state = ts;	
}


static const struct tcpc_driver_api driver_api = {
	.init =			mt_init,
	.set_alert_handler_cb =	mt_set_alert_handler_cb,
	.get_cc =		mt_get_cc,
	.set_rx_enable =	mt_set_rx_enable,
	.is_rx_pending_msg =	mt_is_rx_pending_msg,
	.receive_data =		mt_receive_data,
	.transmit_data =	mt_transmit_data,
	.select_rp_value =	mt_select_rp_value,
	.get_rp_value =		mt_get_rp_value,
	.set_cc =		mt_set_cc,
	.set_roles =		mt_set_roles,
	.set_vconn_cb =		mt_set_vconn_cb,
	.set_vconn =		mt_set_vconn,
	.set_cc_polarity =	mt_cc_set_polarity,
	.dump_std_reg =		mt_dump_std_reg,
	.set_bist_test_mode =	mt_set_bist_test_mode,
	.sop_prime_enable =	mt_sop_prime_enable,
};

#define TCPC_DRIVER_INIT(inst)						\
	static struct mock_tcpc_data drv_data_##inst;			\
	static const struct mock_tcpc_config drv_config_##inst;		\
	DEVICE_DT_INST_DEFINE(inst,					\
			      &mt_init,					\
			      NULL,					\
			      &drv_data_##inst,				\
			      &drv_config_##inst,			\
			      POST_KERNEL,				\
			      CONFIG_USBC_INIT_PRIORITY,		\
			      &driver_api);

DT_INST_FOREACH_STATUS_OKAY(TCPC_DRIVER_INIT)
