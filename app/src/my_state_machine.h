/**
 * @file new_state_machine.h
 */

#ifndef NEW_STATE_MACHINE_H
#define NEW_STATE_MACHINE_H

#include <stdint.h>

void new_state_machine_init(void);
int  new_state_machine_run(void);

void new_state_machine_button_pressed(uint8_t button);

#endif // NEW_STATE_MACHINE_H
