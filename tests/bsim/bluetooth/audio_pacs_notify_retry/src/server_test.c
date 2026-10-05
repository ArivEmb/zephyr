/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/lc3.h>
#include <zephyr/bluetooth/audio/pacs.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include "babblekit/flags.h"
#include "babblekit/testcase.h"
#include "bstests.h"

#define DEFAULT_ATT_MTU 23U

DEFINE_FLAG_STATIC(flag_connected);

static struct bt_conn *g_conn;

static struct bt_audio_codec_cap lc3_codec_1 = BT_AUDIO_CODEC_CAP_LC3(
	BT_AUDIO_CODEC_CAP_FREQ_16KHZ, BT_AUDIO_CODEC_CAP_DURATION_10,
	BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1), 40U, 60U, 1U, BT_AUDIO_CONTEXT_TYPE_ANY);
static struct bt_audio_codec_cap lc3_codec_2 = BT_AUDIO_CODEC_CAP_LC3(
	BT_AUDIO_CODEC_CAP_FREQ_24KHZ, BT_AUDIO_CODEC_CAP_DURATION_10,
	BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1), 40U, 60U, 1U, BT_AUDIO_CONTEXT_TYPE_ANY);
static struct bt_pacs_cap cap_1 = {.codec_cap = &lc3_codec_1};
static struct bt_pacs_cap cap_2 = {.codec_cap = &lc3_codec_2};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0U) {
		TEST_FAIL("Failed to connect (err %u)", err);
		return;
	}

	g_conn = bt_conn_ref(conn);
	SET_FLAG(flag_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn != g_conn) {
		return;
	}

	bt_conn_unref(g_conn);
	g_conn = NULL;
	UNSET_FLAG(flag_connected);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void test_main(void)
{
	const struct bt_data ad[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	};
	const struct bt_pacs_register_param pacs_param = {.snk_pac = true, .snk_loc = true};
	const struct bt_gatt_attr *snk_pac_attr;
	int err;

	TEST_START("PACS notify retry server");

	err = bt_enable(NULL);
	TEST_ASSERT(err == 0, "Bluetooth enable failed (err %d)", err);

	err = bt_pacs_register(&pacs_param);
	TEST_ASSERT(err == 0, "Could not register PACS (err %d)", err);

	err = bt_pacs_cap_register(BT_AUDIO_DIR_SINK, &cap_1);
	TEST_ASSERT(err == 0, "Failed to register Sink PAC (err %d)", err);

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	TEST_ASSERT(err == 0, "Advertising failed to start (err %d)", err);

	WAIT_FOR_FLAG(flag_connected);

	snk_pac_attr = bt_gatt_find_by_uuid(NULL, 0, BT_UUID_PACS_SNK);
	TEST_ASSERT(snk_pac_attr != NULL, "No Sink PAC attribute");

	while (!bt_gatt_is_subscribed(g_conn, snk_pac_attr, BT_GATT_CCC_NOTIFY)) {
		k_sleep(K_MSEC(10));
	}

	/* The Sink PAC does not fit the default ATT MTU, so the notification below fails */
	TEST_ASSERT(bt_gatt_get_mtu(g_conn) == DEFAULT_ATT_MTU, "Unexpected ATT MTU %u",
		    bt_gatt_get_mtu(g_conn));

	err = bt_pacs_cap_register(BT_AUDIO_DIR_SINK, &cap_2);
	TEST_ASSERT(err == 0, "Failed to register Sink PAC (err %d)", err);

	WAIT_FOR_FLAG_UNSET(flag_connected);

	TEST_PASS("PACS notify retry server passed");
}

static void test_tick(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		TEST_FAIL("Test did not pass in time");
	}
}

static void test_init(void)
{
	bst_ticker_set_next_tick_absolute(20 * USEC_PER_SEC);
	bst_result = In_progress;
}

static const struct bst_test_instance test_server[] = {
	{
		.test_id = "server",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_main,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_server_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_server);
}
