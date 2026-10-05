/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/audio/pacs.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include "babblekit/flags.h"
#include "babblekit/testcase.h"
#include "bstests.h"

DEFINE_FLAG_STATIC(flag_connected);
DEFINE_FLAG_STATIC(flag_encrypted);
DEFINE_FLAG_STATIC(flag_discovered);
DEFINE_FLAG_STATIC(flag_subscribed);
DEFINE_FLAG_STATIC(flag_mtu_exchanged);
DEFINE_FLAG_STATIC(flag_notified);

static struct bt_conn *g_conn;
static uint16_t snk_pac_handle;
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;
static struct bt_gatt_exchange_params exchange_params;

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0U) {
		TEST_FAIL("Failed to connect (err %u)", err);
		return;
	}

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

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err security_err)
{
	if (security_err == BT_SECURITY_ERR_SUCCESS && level > BT_SECURITY_L1) {
		SET_FLAG(flag_encrypted);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	int err;

	if (g_conn != NULL || (type != BT_HCI_ADV_IND && type != BT_HCI_ADV_DIRECT_IND)) {
		return;
	}

	err = bt_le_scan_stop();
	TEST_ASSERT(err == 0, "Could not stop scan (err %d)", err);

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &g_conn);
	TEST_ASSERT(err == 0, "Could not connect to peer (err %d)", err);
}

static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	if (attr != NULL) {
		snk_pac_handle = bt_gatt_attr_value_handle(attr);
	}

	SET_FLAG(flag_discovered);

	return BT_GATT_ITER_STOP;
}

static uint8_t notify_func(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	if (data != NULL) {
		SET_FLAG(flag_notified);
	}

	return BT_GATT_ITER_CONTINUE;
}

static void subscribe_func(struct bt_conn *conn, uint8_t err,
			   struct bt_gatt_subscribe_params *params)
{
	TEST_ASSERT(err == 0U, "Subscribe failed (err 0x%02x)", err);
	SET_FLAG(flag_subscribed);
}

static void exchange_func(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params)
{
	TEST_ASSERT(err == 0U, "MTU exchange failed (err 0x%02x)", err);
	SET_FLAG(flag_mtu_exchanged);
}

static void test_main(void)
{
	static struct bt_gatt_discover_params disc_params;
	int err;

	TEST_START("PACS notify retry client");

	err = bt_enable(NULL);
	TEST_ASSERT(err == 0, "Bluetooth enable failed (err %d)", err);

	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);
	TEST_ASSERT(err == 0, "Scanning failed to start (err %d)", err);

	WAIT_FOR_FLAG(flag_connected);

	err = bt_conn_set_security(g_conn, BT_SECURITY_L2);
	TEST_ASSERT(err == 0, "Failed to start encryption (err %d)", err);
	WAIT_FOR_FLAG(flag_encrypted);

	discover_params.uuid = BT_UUID_PACS_SNK;
	discover_params.func = discover_func;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
	err = bt_gatt_discover(g_conn, &discover_params);
	TEST_ASSERT(err == 0, "Discovery failed (err %d)", err);
	WAIT_FOR_FLAG(flag_discovered);
	TEST_ASSERT(snk_pac_handle != 0U, "Sink PAC not found");

	subscribe_params.value_handle = snk_pac_handle;
	subscribe_params.ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
	subscribe_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	subscribe_params.disc_params = &disc_params;
	subscribe_params.value = BT_GATT_CCC_NOTIFY;
	subscribe_params.notify = notify_func;
	subscribe_params.subscribe = subscribe_func;
	err = bt_gatt_subscribe(g_conn, &subscribe_params);
	TEST_ASSERT(err == 0, "Subscribe failed (err %d)", err);
	WAIT_FOR_FLAG(flag_subscribed);

	k_sleep(K_SECONDS(1));
	TEST_ASSERT(!IS_FLAG_SET(flag_notified), "Notified before the MTU exchange");

	exchange_params.func = exchange_func;
	err = bt_gatt_exchange_mtu(g_conn, &exchange_params);
	TEST_ASSERT(err == 0, "MTU exchange failed to start (err %d)", err);
	WAIT_FOR_FLAG(flag_mtu_exchanged);

	WAIT_FOR_FLAG(flag_notified);

	err = bt_conn_disconnect(g_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	TEST_ASSERT(err == 0, "Disconnect failed (err %d)", err);
	WAIT_FOR_FLAG_UNSET(flag_connected);

	TEST_PASS("PACS notify retry client passed");
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

static const struct bst_test_instance test_client[] = {
	{
		.test_id = "client",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_main,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_client_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_client);
}
