/*
 * main.c
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>

#include "LED.h"
#include "BTN.h"
#include "my_state_machine.h"

#define SLEEP_TIME_MS 1

int main(void) {

    if (0 > LED_init()) {
        printk("Failed to initialize LEDs\n");
        return 0;
    }
    if (0 > BTN_init()) {
        printk("Failed to initialize buttons\n");
        return 0;
    }

    printk("\n========================================\n");
    printk("ASCII String Entry State Machine\n");
    printk("========================================\n\n");
    printk("INSTRUCTIONS:\n");
    printk("- BTN0 = Enter bit 0, BTN1 = Enter bit 1 (LSB first)\n");
    printk("- BTN2 = Reset/Delete, BTN3 = Save/Confirm\n");
    printk("- Hold BTN0 + BTN1 for 3 seconds = Standby mode\n\n");

    state_machine_init();

    while (1) {
        state_machine_run();
        k_msleep(SLEEP_TIME_MS);
    }

    return 0;
}
