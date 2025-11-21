/*
 * main.c
 */

/* IMPORTS -------------------------------------------------------------------------------------- */

#include <stdio.h>
#include <string.h>
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

#include "LED.h"
#include "BTN.h"

/* MACROS --------------------------------------------------------------------------------------- */

#define BLE_CUSTOM_SERVICE_UUID \
  BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define BLE_CUSTOM_CHARACTERISTIC_UUID \
  BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)

#define BLE_CUSTOM_CHARACTERISTIC_MAX_DATA_LENGTH 20

/* PROTOTYPES ----------------------------------------------------------------------------------- */

static ssize_t ble_custom_service_read(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                       void* buf, uint16_t len, uint16_t offset);
static ssize_t ble_custom_service_write(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                        const void* buf, uint16_t len, uint16_t offset,
                                        uint8_t flags);

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

static bool counter_direction_up = true;  // true = counting up, false = counting down
static bool led1_state = false;  // Track LED1 state: false = OFF, true = ON

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
  // Return the current status of LED1
  const char* led_status = led1_state ? "ON" : "OFF";
  
  printk("[BLE] Read request - LED1 status: %s\n", led_status);

  return bt_gatt_attr_read(conn, attr, buf, len, offset, led_status, strlen(led_status));
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

  // Check for LED control commands
  if (strncmp((const char*)value, "LED ON", 6) == 0) {
    LED_set(LED1, LED_ON);
    led1_state = true;
    printk("[BLE] LED1 turned ON\n");
  } else if (strncmp((const char*)value, "LED OFF", 7) == 0) {
    LED_set(LED1, LED_OFF);
    led1_state = false;
    printk("[BLE] LED1 turned OFF\n");
  }

  return len;
}

static void ble_custom_service_notify() {
  static uint32_t counter = 0;
  bt_gatt_notify(NULL, &ble_custom_service.attrs[2], &counter, sizeof(counter));
  
  if (counter_direction_up) {
    counter++;
  } else {
    counter--;
  }
}

/* MAIN ----------------------------------------------------------------------------------------- */

int main(void) {
  // Initialize LED driver
  if (LED_init() < 0) {
    printk("LED init failed\n");
    return 0;
  }
  printk("LED initialized!\n");

  // Initialize BTN driver
  if (BTN_init() < 0) {
    printk("BTN init failed\n");
    return 0;
  }
  printk("BTN initialized!\n");

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

  while (1) {
    // Check if BTN0 (physical button 1) is pressed to reverse counter direction
    if (BTN_check_clear_pressed(BTN0)) {
      counter_direction_up = !counter_direction_up;
      printk("Counter direction reversed: now counting %s\n", 
             counter_direction_up ? "UP" : "DOWN");
    }

    k_sleep(K_MSEC(1000));
    ble_custom_service_notify();
  }
}
