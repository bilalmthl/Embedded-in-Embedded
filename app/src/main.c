/*
 * main.c
 */

/* IMPORTS -------------------------------------------------------------------------------------- */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

/* MACROS --------------------------------------------------------------------------------------- */

#define BLE_CUSTOM_SERVICE_UUID \
  BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define BLE_CUSTOM_CHARACTERISTIC_UUID \
  BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)

#define BLE_CUSTOM_CHARACTERISTIC_MAX_DATA_LENGTH 20
#define LED0_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

/* PROTOTYPES ----------------------------------------------------------------------------------- */

static ssize_t ble_custom_service_read(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                       void* buf, uint16_t len, uint16_t offset);
static ssize_t ble_custom_service_write(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                        const void* buf, uint16_t len, uint16_t offset,
                                        uint8_t flags);

static void ble_custom_service_notify(void);

/* VARIABLES ------------------------------------------------------------------------------------ */

static const struct bt_uuid_128 ble_custom_service_uuid = BT_UUID_INIT_128(BLE_CUSTOM_SERVICE_UUID);
static const struct bt_uuid_128 ble_custom_characteristic_uuid =
    BT_UUID_INIT_128(BLE_CUSTOM_CHARACTERISTIC_UUID);

static const struct bt_data ble_advertising_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BLE_CUSTOM_SERVICE_UUID),
};

static const struct bt_data ble_scan_response_data[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static uint8_t ble_custom_characteristic_user_data[BLE_CUSTOM_CHARACTERISTIC_MAX_DATA_LENGTH + 1] =
    {'E', 'i', 'E'};

// LED 1 GPIO description and state
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static bool led1_on = false;

/* BLE SERVICE SETUP ---------------------------------------------------------------------------- */

BT_GATT_SERVICE_DEFINE(
    ble_custom_service,  // Name of the struct that will store the config for this service
    BT_GATT_PRIMARY_SERVICE(&ble_custom_service_uuid),  // Setting the service UUID

    // Now to define the characteristic:
    BT_GATT_CHARACTERISTIC(
        &ble_custom_characteristic_uuid.uuid,  // Setting the characteristic UUID
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,  // Possible operations
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,  // Permissions that connecting devices have
        ble_custom_service_read,             // Callback for when this characteristic is read from
        ble_custom_service_write,            // Callback for when this characteristic is written to
        ble_custom_characteristic_user_data  // Initial data stored in this characteristic
        ),
    BT_GATT_CCC(  // Client characteristic configuration for the above custom characteristic
        NULL,     // Callback for when this characteristic is changed
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE  // Permissions that connecting devices have
        ),

    // End of service definition
);

/* FUNCTIONS ------------------------------------------------------------------------------------ */

static ssize_t ble_custom_service_read(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                       void* buf, uint16_t len, uint16_t offset) {
  // Send the data that is stored in the characteristic ("EiE" by default, can change if written to)
  // by fetching it directly from the characteristic object
  const char* data_to_send_to_connected_device = attr->user_data;

  return bt_gatt_attr_read(conn, attr, buf, len, offset, data_to_send_to_connected_device,
                           strlen(data_to_send_to_connected_device));
}

static ssize_t ble_custom_service_write(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                        const void* buf, uint16_t len, uint16_t offset,
                                        uint8_t flags) {
  uint8_t* value = attr->user_data;

  if (offset + len > BLE_CUSTOM_CHARACTERISTIC_MAX_DATA_LENGTH) {
    printk("[BLE] ble_custom_service_write: Bad offset %d\n", offset + len);
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
  }

  memcpy(value + offset, buf, len);
  value[offset + len] = 0;

  printk("[BLE] ble_custom_service_write (%d, %d):", offset, len);
  for (uint16_t i = 0; i < len; i++) {
    printk("%s %02X '%c'", i == 0 ? "" : ",", value[offset + i], value[offset + i]);
  }
  printk("\n");

  // Control LED 1 when LED ON or LED OFF is received
  if (strcmp((char*)value, "LED ON") == 0) {
    gpio_pin_set_dt(&led1, 1);
    led1_on = true;
    printk("[BLE] LED 1 turned ON\n");
  } else if (strcmp((char*)value, "LED OFF") == 0) {
    gpio_pin_set_dt(&led1, 0);
    led1_on = false;
    printk("[BLE] LED 1 turned OFF\n");
  }

  return len;
}

static void ble_custom_service_notify(void) {
  static uint32_t counter = 0;
  bt_gatt_notify(NULL, &ble_custom_service.attrs[2], &counter, sizeof(counter));
  counter++;
}

/* MAIN ----------------------------------------------------------------------------------------- */

int main(void) {
  int err = bt_enable(NULL);
  if (err) {
    printk("Bluetooth init failed (err %d)\n", err);
    return 0;
  } else {
    printk("Bluetooth initialized!\n");
  }

  err =
      bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ble_advertising_data, ARRAY_SIZE(ble_advertising_data),
                      ble_scan_response_data, ARRAY_SIZE(ble_scan_response_data));
  if (err) {
    printk("Advertising failed to start (err %d)\n", err);
    return 0;
  }

  if(!gpio_is_ready_dt(&led1)) {
    printk("LED 1 GPIO device not ready\n");
    return 0;
  }

  err = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
  if (err < 0) {
    printk("Failed to configure LED 1 GPIO pin (err %d)\n", err);
    return 0;
  }
  led1_on = false;

  while (1) {
    k_sleep(K_MSEC(1000));
    ble_custom_service_notify();
  }
}
