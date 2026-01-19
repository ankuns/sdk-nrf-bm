/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#include <nfc_t4t_lib.h>
#include <nfc/t4t/ndef_file.h>
#include <nfc/ndef/msg.h>
#include <nfc/ndef/text_rec.h>

#include <bm/softdevice_handler/nrf_sdh.h>
#include <bm/softdevice_handler/nrf_sdh_ble.h>

#include <bm/bluetooth/ble_adv.h>

#include <bm/bluetooth/peer_manager/nrf_ble_lesc.h>
#include <bm/bluetooth/peer_manager/peer_manager.h>
#include <bm/bluetooth/peer_manager/peer_manager_handler.h>


#include <hal/nrf_gpio.h>

#include <board-config.h>

LOG_MODULE_REGISTER(app, CONFIG_APP_BLE_PERIPHERAL_NFC_PAIRING_LOG_LEVEL);

#define MAX_REC_COUNT		3
#define NDEF_MSG_BUF_SIZE	128

#define NFC_FIELD_LED		BOARD_PIN_LED_0


/* Perform bonding. */
#define SEC_PARAM_BOND 1
/* Man In The Middle protection not required. */
#define SEC_PARAM_MITM 0
/* LE Secure Connections enabled. */
#define SEC_PARAM_LESC 1
/* Keypress notifications not enabled. */
#define SEC_PARAM_KEYPRESS 1
/* No I/O capabilities. */
#define SEC_PARAM_IO_CAPABILITIES BLE_GAP_IO_CAPS_DISPLAY_YESNO
/* Out Of Band data not available. */
#define SEC_PARAM_OOB 0
/* Minimum encryption key size. */
#define SEC_PARAM_MIN_KEY_SIZE 7
/* Maximum encryption key size. */
#define SEC_PARAM_MAX_KEY_SIZE 16

/* BLE Advertising library instance */
BLE_ADV_DEF(ble_adv);

/* Peer ID */
static uint16_t peer_id;


/* Text message in English with its language code. */
static const uint8_t en_payload[] = {
	'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!'
};
static const uint8_t en_code[] = {'e', 'n'};

/* Text message in Norwegian with its language code. */
static const uint8_t no_payload[] = {
	'H', 'a', 'l', 'l', 'o', ' ', 'V', 'e', 'r', 'd', 'e', 'n', '!'
};
static const uint8_t no_code[] = {'N', 'O'};

/* Text message in Polish with its language code. */
static const uint8_t pl_payload[] = {
	'W', 'i', 't', 'a', 'j', ' ', 0xc5, 0x9a, 'w', 'i', 'e', 'c', 'i',
	'e', '!'
};
static const uint8_t pl_code[] = {'P', 'L'};

/* Buffer used to hold an NFC NDEF message. */
static uint8_t ndef_msg_buf[NDEF_MSG_BUF_SIZE];

static void led_init(void)
{
	nrf_gpio_cfg_output(NFC_FIELD_LED);
}

static void nfc_field_led_on(void)
{
	nrf_gpio_pin_write(NFC_FIELD_LED, BOARD_LED_ACTIVE_STATE);
}

static void nfc_field_led_off(void)
{
	nrf_gpio_pin_write(NFC_FIELD_LED, !BOARD_LED_ACTIVE_STATE);
}

static void nfc_callback(void *context,
			 nfc_t4t_event_t event,
			 const uint8_t *data,
			 size_t data_length,
			 uint32_t flags)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(data_length);
	ARG_UNUSED(flags);

	switch (event) {
	case NFC_T4T_EVENT_FIELD_ON:
		nfc_field_led_on();
		break;
	case NFC_T4T_EVENT_FIELD_OFF:
		nfc_field_led_off();
		break;
	default:
		break;
	}
}

/**
 * @brief Function for encoding the NDEF file with text messages.
 */
static int welcome_msg_encode(uint8_t *buffer, uint32_t *len)
{
	int err;
	uint32_t ndef_len = nfc_t4t_ndef_file_msg_size_get(*len);

	/* Create NFC NDEF text record description in English */
	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_en_text_rec,
				      UTF_8,
				      en_code,
				      sizeof(en_code),
				      en_payload,
				      sizeof(en_payload));

	/* Create NFC NDEF text record description in Norwegian */
	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_no_text_rec,
				      UTF_8,
				      no_code,
				      sizeof(no_code),
				      no_payload,
				      sizeof(no_payload));

	/* Create NFC NDEF text record description in Polish */
	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_pl_text_rec,
				      UTF_8,
				      pl_code,
				      sizeof(pl_code),
				      pl_payload,
				      sizeof(pl_payload));

	/* Create NFC NDEF message description, capacity - MAX_REC_COUNT
	 * records
	 */
	NFC_NDEF_MSG_DEF(nfc_text_msg, MAX_REC_COUNT);

	/* Add text records to NDEF text message */
	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
				   &NFC_NDEF_TEXT_RECORD_DESC(nfc_en_text_rec));
	if (err < 0) {
		LOG_ERR("Cannot add first record!");
		return err;
	}
	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
				   &NFC_NDEF_TEXT_RECORD_DESC(nfc_no_text_rec));
	if (err < 0) {
		LOG_ERR("Cannot add second record!");
		return err;
	}
	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
				   &NFC_NDEF_TEXT_RECORD_DESC(nfc_pl_text_rec));
	if (err < 0) {
		LOG_ERR("Cannot add third record!");
		return err;
	}

	err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(nfc_text_msg),
				  nfc_t4t_ndef_file_msg_get(buffer),
				  &ndef_len);
	if (err < 0) {
		LOG_ERR("Cannot encode message!");
		return err;
	}

	err = nfc_t4t_ndef_file_encode(buffer, &ndef_len);
	if (err) {
		LOG_ERR("Cannot encode NDEF file!");
		return err;
	}
	*len = ndef_len;

	return err;
}

static void allow_list_set(enum pm_peer_id_list_skip skip)
{
	uint32_t nrf_err;
	uint16_t peer_ids[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
	uint32_t peer_id_count = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;

	nrf_err = pm_peer_id_list(peer_ids, &peer_id_count, PM_PEER_ID_INVALID, skip);
	if (nrf_err) {
		LOG_ERR("Failed to get peer id list, nrf_error %#x", nrf_err);
	}

	LOG_INF("Number of peers added to the allow list: %d, max %d",
		peer_id_count, BLE_GAP_WHITELIST_ADDR_MAX_COUNT);

	nrf_err = pm_allow_list_set(peer_ids, peer_id_count);
	if (nrf_err) {
		LOG_ERR("Failed to set allow list, nrf_error %#x", nrf_err);
	}
}

static void identities_set(enum pm_peer_id_list_skip skip)
{
	uint32_t nrf_err;
	uint16_t peer_ids[BLE_GAP_DEVICE_IDENTITIES_MAX_COUNT];
	uint32_t peer_id_count = BLE_GAP_DEVICE_IDENTITIES_MAX_COUNT;

	nrf_err = pm_peer_id_list(peer_ids, &peer_id_count, PM_PEER_ID_INVALID, skip);
	if (nrf_err) {
		LOG_ERR("Failed to get peer id list, nrf_error %#x", nrf_err);
	}

	nrf_err = pm_device_identities_list_set(peer_ids, peer_id_count);
	if (nrf_err) {
		LOG_ERR("Failed to set peer manager identity list, nrf_error %#x", nrf_err);
	}
}

static void delete_bonds(void)
{
	uint32_t nrf_err;

	LOG_INF("Erasing bonds");

	nrf_err = pm_peers_delete();
	if (nrf_err) {
		LOG_ERR("Failed to delete peers, nrf_error %#x", nrf_err);
	}
}

static uint32_t advertising_start(bool erase_bonds)
{
	uint32_t nrf_err = NRF_SUCCESS;

	if (erase_bonds) {
		delete_bonds();
	} else {
		allow_list_set(PM_PEER_ID_LIST_SKIP_NO_ID_ADDR);

		nrf_err = ble_adv_start(&ble_adv, BLE_ADV_MODE_FAST);
		if (nrf_err) {
			LOG_ERR("Failed to start advertising, nrf_error %#x", nrf_err);
		}
	}

	return nrf_err;
}


static void pm_evt_handler(const struct pm_evt *evt)
{
	pm_handler_on_pm_evt(evt);
	pm_handler_disconnect_on_sec_failure(evt);
	pm_handler_flash_clean(evt);

	switch (evt->evt_id) {
	case PM_EVT_CONN_SEC_SUCCEEDED:
		peer_id = evt->peer_id;
		break;

	case PM_EVT_PEERS_DELETE_SUCCEEDED:
		advertising_start(false);
		break;

	case PM_EVT_PEER_DATA_UPDATE_SUCCEEDED:
		if (evt->params.peer_data_update_succeeded.flash_changed &&
		    (evt->params.peer_data_update_succeeded.data_id == PM_PEER_DATA_ID_BONDING)) {
			LOG_INF("New bond, add the peer to the allow list if possible");
			/* Note: You should check on what kind of allow list policy your
			 * application should use.
			 */

			allow_list_set(PM_PEER_ID_LIST_SKIP_NO_ID_ADDR);
		}
		break;

	default:
		break;
	}
}

static uint32_t peer_manager_init(void)
{
	ble_gap_sec_params_t sec_param;
	uint32_t nrf_err;

	nrf_err = pm_init();
	if (nrf_err) {
		return nrf_err;
	}

	memset(&sec_param, 0, sizeof(ble_gap_sec_params_t));

	/* Security parameters to be used for all security procedures. */
	sec_param = (ble_gap_sec_params_t) {
		.bond = SEC_PARAM_BOND,
		.mitm = SEC_PARAM_MITM,
		.lesc = SEC_PARAM_LESC,
		.keypress = SEC_PARAM_KEYPRESS,
		.io_caps = SEC_PARAM_IO_CAPABILITIES,
		.oob = SEC_PARAM_OOB,
		.min_key_size = SEC_PARAM_MIN_KEY_SIZE,
		.max_key_size = SEC_PARAM_MAX_KEY_SIZE,
		.kdist_own.enc = 1,
		.kdist_own.id = 1,
		.kdist_peer.enc = 1,
		.kdist_peer.id = 1,
	};

	nrf_err = pm_sec_params_set(&sec_param);
	if (nrf_err) {
		LOG_ERR("pm_sec_params_set() failed, nrf_error %#x", nrf_err);
		return nrf_err;
	}

	nrf_err = pm_register(pm_evt_handler);
	if (nrf_err) {
		LOG_ERR("pm_register() failed, nrf_error %#x", nrf_err);
		return nrf_err;
	}

	return NRF_SUCCESS;
}

static void ble_adv_evt_handler(struct ble_adv *ble_adv, const struct ble_adv_evt *evt)
{
	uint32_t nrf_err;
	ble_gap_addr_t *peer_addr;
	ble_gap_addr_t allow_list_addrs[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
	ble_gap_irk_t allow_list_irks[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
	uint32_t addr_cnt = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;
	uint32_t irk_cnt = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;

	switch (evt->evt_type) {
	case BLE_ADV_EVT_ERROR:
		LOG_ERR("Advertising error %#x", evt->error.reason);
		break;
	case BLE_ADV_EVT_DIRECTED_HIGH_DUTY:
	case BLE_ADV_EVT_DIRECTED:
	case BLE_ADV_EVT_FAST:
	case BLE_ADV_EVT_SLOW:
	case BLE_ADV_EVT_FAST_ALLOW_LIST:
	case BLE_ADV_EVT_SLOW_ALLOW_LIST:
		// nrf_gpio_pin_write(BOARD_PIN_LED_0, BOARD_LED_ACTIVE_STATE);
		break;
	case BLE_ADV_EVT_IDLE:
		// nrf_gpio_pin_write(BOARD_PIN_LED_0, !BOARD_LED_ACTIVE_STATE);
		break;
	case BLE_ADV_EVT_ALLOW_LIST_REQUEST:
		nrf_err = pm_allow_list_get(allow_list_addrs, &addr_cnt, allow_list_irks, &irk_cnt);
		if (nrf_err) {
			LOG_ERR("Failed to get allow list, nrf_error %#x", nrf_err);
		}
		LOG_DBG("pm_allow_list_get returns %d addr in allow list and %d irk allow list",
				addr_cnt, irk_cnt);

		/* Set the correct identities list
		 * (no excluding peers with no Central Address Resolution).
		 */
		identities_set(PM_PEER_ID_LIST_SKIP_NO_IRK);

		nrf_err = ble_adv_allow_list_reply(ble_adv, allow_list_addrs, addr_cnt,
						   allow_list_irks, irk_cnt);
		if (nrf_err) {
			LOG_ERR("Failed to set allow list, nrf_error %#x", nrf_err);
		}
		break;

	case BLE_ADV_EVT_PEER_ADDR_REQUEST:
		struct pm_peer_data_bonding peer_bonding_data;

		/* Only Give peer address if we have a handle to the bonded peer. */
		if (peer_id != PM_PEER_ID_INVALID) {
			nrf_err = pm_peer_data_bonding_load(peer_id, &peer_bonding_data);
			if (nrf_err != NRF_ERROR_NOT_FOUND) {
				if (nrf_err) {
					LOG_ERR("Failed to load bonding data, nrf_error %#x",
						nrf_err);
				}

				/* Manipulate identities to exclude peers with no
				 * Central Address Resolution.
				 */
				identities_set(PM_PEER_ID_LIST_SKIP_ALL);

				peer_addr = &(peer_bonding_data.peer_ble_id.id_addr_info);
				nrf_err = ble_adv_peer_addr_reply(ble_adv, peer_addr);
				if (nrf_err) {
					LOG_ERR("Failed to reply peer address, nrf_error %#x",
						nrf_err);
				}
			}
		}
		break;
	default:
		break;
	}
}


int main(void)
{
	uint32_t len = sizeof(ndef_msg_buf);

	LOG_INF("Starting Peripheral NFC Pairing sample");

	/* Configure LED-pins as outputs */
	led_init();

	int err = nrf_sdh_enable_request();
	if (err) {
		LOG_ERR("Failed to enable SoftDevice, err %d", err);
		goto fail;
	}

	LOG_INF("SoftDevice enabled");

	err = nrf_sdh_ble_enable(CONFIG_NRF_SDH_BLE_CONN_TAG);
	if (err) {
		LOG_ERR("Failed to enable BLE, err %d", err);
		goto fail;
	}

	LOG_INF("Bluetooth is enabled!");

	uint32_t nrf_err = peer_manager_init();
	if (nrf_err) {
		LOG_ERR("Failed to initialize Peer Manager, nrf_error %x", nrf_err);
		goto fail;
	}

	// ble_uuid_t adv_uuid_list[] = {
	// 	{ .uuid = BLE_UUID_HUMAN_INTERFACE_DEVICE_SERVICE, .type = BLE_UUID_TYPE_BLE },
	// };

	struct ble_adv_config ble_adv_cfg = {
		.conn_cfg_tag = CONFIG_NRF_SDH_BLE_CONN_TAG,
		.evt_handler = ble_adv_evt_handler,
		.adv_data = {
			.name_type = BLE_ADV_DATA_FULL_NAME,
			.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE,

		},
		// .sr_data.uuid_lists.complete = {
		// 	.uuid = &adv_uuid_list[0],
		// 	.len = ARRAY_SIZE(adv_uuid_list),
		// }
	};

	nrf_err = ble_adv_init(&ble_adv, &ble_adv_cfg);
	if (nrf_err) {
		LOG_ERR("Failed to initialize BLE advertising, nrf_error %#x", nrf_err);
		goto fail;
	}



	/* Set up NFC */
	if (nfc_t4t_setup(nfc_callback, NULL) < 0) {
		LOG_ERR("Cannot setup NFC T4T library!");
		goto fail;
	}


	/* Encode welcome message */
	if (welcome_msg_encode(ndef_msg_buf, &len) < 0) {
		LOG_ERR("Cannot encode message!");
		goto fail;
	}

	/* Set created message as the NFC payload */
	if (nfc_t4t_ndef_staticpayload_set(ndef_msg_buf, len) < 0) {
		LOG_ERR("Cannot set payload!");
		goto fail;
	}

	/* Start sensing NFC field */
	if (nfc_t4t_emulation_start() < 0) {
		LOG_ERR("Cannot start emulation!");
		goto fail;
	}
	LOG_INF("NFC configuration done");

fail:
	/* Main loop */
	while (true) {
		while (LOG_PROCESS()) {
		}

		/* Wait for an event. */
		__WFE();

		/* Clear Event Register */
		__SEV();
		__WFE();
	}

	return 0;
}
