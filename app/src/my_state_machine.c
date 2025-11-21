/**
 * @file my_state_machine.c
 * FSM for ASCII string entry and transmission
 */

#include <zephyr/smf.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "BTN.h"
#include "LED.h"
#include "my_state_machine.h"

// Constants
#define MAX_STRING_LENGTH 64
#define STANDBY_HOLD_TIME_MS 3000
#define BLINK_INDICATOR_TIME_MS 100

// Function prototypes for state handlers
static void char_entry_state_entry(void *o);
static enum smf_state_result char_entry_state_run(void *o);
static void char_entry_state_exit(void *o);

static void string_build_state_entry(void *o);
static enum smf_state_result string_build_state_run(void *o);
static void string_build_state_exit(void *o);

static void string_confirm_state_entry(void *o);
static enum smf_state_result string_confirm_state_run(void *o);
static void string_confirm_state_exit(void *o);

static void standby_state_entry(void *o);
static enum smf_state_result standby_state_run(void *o);
static void standby_state_exit(void *o);

// Typedefs
enum state_machine_states {
    CHAR_ENTRY_STATE,
    STRING_BUILD_STATE,
    STRING_CONFIRM_STATE,
    STANDBY_STATE
};

typedef struct {
    struct smf_ctx ctx;
    
    // String storage
    char string_buffer[MAX_STRING_LENGTH];
    uint8_t string_length;
    
    // Current character being built
    uint8_t current_char;
    uint8_t bit_count;
    
    // Standby state tracking
    uint32_t btn0_hold_start;
    uint32_t btn1_hold_start;
    bool btn0_held;
    bool btn1_held;
    enum state_machine_states previous_state;
    
    // LED blink tracking
    uint32_t led_blink_counter;
    bool led3_state;
    
    // Button input indicator
    uint32_t led0_blink_timer;
    uint32_t led1_blink_timer;
    
    // PWM pulse tracking for standby
    uint8_t pwm_duty;
    bool pwm_increasing;
    
} state_machine_context;

// local variables 
static const struct smf_state states[] = {
    [CHAR_ENTRY_STATE] = SMF_CREATE_STATE(char_entry_state_entry, char_entry_state_run, char_entry_state_exit, NULL, NULL),
    [STRING_BUILD_STATE] = SMF_CREATE_STATE(string_build_state_entry, string_build_state_run, string_build_state_exit, NULL, NULL),
    [STRING_CONFIRM_STATE] = SMF_CREATE_STATE(string_confirm_state_entry, string_confirm_state_run, string_confirm_state_exit, NULL, NULL),
    [STANDBY_STATE] = SMF_CREATE_STATE(standby_state_entry, standby_state_run, standby_state_exit, NULL, NULL)
};

static state_machine_context state_object;

// Helper functions
static void check_standby_transition(void) {
    uint32_t current_time = k_uptime_get_32();
    
    // Check if BTN0 is currently pressed
    if (BTN_is_pressed(BTN0)) {
        if (!state_object.btn0_held) {
            state_object.btn0_held = true;
            state_object.btn0_hold_start = current_time;
        }
    } else {
        state_object.btn0_held = false;
    }
    
    // Check if BTN1 is currently pressed
    if (BTN_is_pressed(BTN1)) {
        if (!state_object.btn1_held) {
            state_object.btn1_held = true;
            state_object.btn1_hold_start = current_time;
        }
    } else {
        state_object.btn1_held = false;
    }
    
    // Check if both buttons held for 3 seconds
    if (state_object.btn0_held && state_object.btn1_held) {
        uint32_t btn0_duration = current_time - state_object.btn0_hold_start;
        uint32_t btn1_duration = current_time - state_object.btn1_hold_start;
        
        if (btn0_duration >= STANDBY_HOLD_TIME_MS && btn1_duration >= STANDBY_HOLD_TIME_MS) {
            // Save current state and transition to standby
            state_object.previous_state = state_object.ctx.current - states;
            smf_set_state(SMF_CTX(&state_object), &states[STANDBY_STATE]);
            // Clear the held flags
            state_object.btn0_held = false;
            state_object.btn1_held = false;
        }
    }
}

// Public Functions
void state_machine_init(void) {
    memset(&state_object, 0, sizeof(state_object));
    smf_set_initial(SMF_CTX(&state_object), &states[CHAR_ENTRY_STATE]);
}

int state_machine_run(void) {
    return smf_run_state(SMF_CTX(&state_object));
}

// CHAR_ENTRY_STATE - Enter individual ASCII character bit by bit
static void char_entry_state_entry(void *o) {
    printk("\n=== Entering CHAR_ENTRY state ===\n");
    printk("Use BTN0 (bit 0) and BTN1 (bit 1) to enter 8-bit ASCII code\n");
    printk("BTN2: Reset current character | BTN3: Save character\n");
    
    state_object.current_char = 0;
    state_object.bit_count = 0;
    state_object.led_blink_counter = 0;
    state_object.led3_state = false;
    state_object.led0_blink_timer = 0;
    state_object.led1_blink_timer = 0;
    
    LED_set(LED0, LED_OFF);
    LED_set(LED1, LED_OFF);
    LED_set(LED2, LED_OFF);
    LED_set(LED3, LED_OFF);
}

static enum smf_state_result char_entry_state_run(void *o) {
    // Check for standby transition
    check_standby_transition();
    if (state_object.ctx.current == &states[STANDBY_STATE]) {
        return SMF_EVENT_HANDLED;
    }
    
    // Handle LED3 blinking at 1 Hz (500ms on, 500ms off)
    state_object.led_blink_counter++;
    if (state_object.led_blink_counter >= 500) {
        state_object.led3_state = !state_object.led3_state;
        LED_set(LED3, state_object.led3_state ? LED_ON : LED_OFF);
        state_object.led_blink_counter = 0;
    }
    
    // Handle button input indicator blinks
    if (state_object.led0_blink_timer > 0) {
        state_object.led0_blink_timer++;
        if (state_object.led0_blink_timer >= BLINK_INDICATOR_TIME_MS) {
            LED_set(LED0, LED_OFF);
            state_object.led0_blink_timer = 0;
        }
    }
    
    if (state_object.led1_blink_timer > 0) {
        state_object.led1_blink_timer++;
        if (state_object.led1_blink_timer >= BLINK_INDICATOR_TIME_MS) {
            LED_set(LED1, LED_OFF);
            state_object.led1_blink_timer = 0;
        }
    }
    
    // BTN0: Add bit 0 to current character
    if (BTN_check_clear_pressed(BTN0)) {
        if (state_object.bit_count < 8) {
            // No change to bit (adds 0)
            state_object.bit_count++;
            LED_set(LED0, LED_ON);
            state_object.led0_blink_timer = 1;
            printk("Bit %d: 0 | Current char: 0x%02X (%d bits)\n", 
                   state_object.bit_count - 1, state_object.current_char, state_object.bit_count);
        }
    }
    
    // BTN1: Add bit 1 to current character
    if (BTN_check_clear_pressed(BTN1)) {
        if (state_object.bit_count < 8) {
            state_object.current_char |= (1 << state_object.bit_count);
            state_object.bit_count++;
            LED_set(LED1, LED_ON);
            state_object.led1_blink_timer = 1;
            printk("Bit %d: 1 | Current char: 0x%02X (%d bits)\n", 
                   state_object.bit_count - 1, state_object.current_char, state_object.bit_count);
        }
    }
    
    // BTN2: Reset current character
    if (BTN_check_clear_pressed(BTN2)) {
        printk("Character reset\n");
        state_object.current_char = 0;
        state_object.bit_count = 0;
    }
    
    // BTN3: Save character
    if (BTN_check_clear_pressed(BTN3)) {
        if (state_object.bit_count == 8) {
            if (state_object.string_length < MAX_STRING_LENGTH - 1) {
                state_object.string_buffer[state_object.string_length] = state_object.current_char;
                state_object.string_length++;
                state_object.string_buffer[state_object.string_length] = '\0';
                
                printk("Character saved: '%c' (0x%02X)\n", 
                       state_object.current_char >= 32 ? state_object.current_char : '?', 
                       state_object.current_char);
                printk("Current string: \"%s\"\n", state_object.string_buffer);
                
                smf_set_state(SMF_CTX(&state_object), &states[STRING_BUILD_STATE]);
            } else {
                printk("String buffer full!\n");
            }
        } else {
            printk("Need 8 bits to save character (currently have %d)\n", state_object.bit_count);
        }
    }
    
    return SMF_EVENT_HANDLED;
}

static void char_entry_state_exit(void *o) {
    printk("=== Exiting CHAR_ENTRY state ===\n");
}

// STRING_BUILD_STATE - Continue building string or finalize
static void string_build_state_entry(void *o) {
    printk("\n=== Entering STRING_BUILD state ===\n");
    printk("Current string: \"%s\" (%d chars)\n", state_object.string_buffer, state_object.string_length);
    printk("BTN0/BTN1: Add another character | BTN2: Delete string | BTN3: Finalize string\n");
    
    state_object.current_char = 0;
    state_object.bit_count = 0;
    state_object.led_blink_counter = 0;
    state_object.led3_state = false;
    state_object.led0_blink_timer = 0;
    state_object.led1_blink_timer = 0;
    
    LED_set(LED0, LED_OFF);
    LED_set(LED1, LED_OFF);
    LED_set(LED2, LED_OFF);
    LED_set(LED3, LED_OFF);
}

static enum smf_state_result string_build_state_run(void *o) {
    // Check for standby transition
    check_standby_transition();
    if (state_object.ctx.current == &states[STANDBY_STATE]) {
        return SMF_EVENT_HANDLED;
    }
    
    // Handle LED3 blinking at 4 Hz (125ms on, 125ms off)
    state_object.led_blink_counter++;
    if (state_object.led_blink_counter >= 125) {
        state_object.led3_state = !state_object.led3_state;
        LED_set(LED3, state_object.led3_state ? LED_ON : LED_OFF);
        state_object.led_blink_counter = 0;
    }
    
    // Handle button input indicator blinks
    if (state_object.led0_blink_timer > 0) {
        state_object.led0_blink_timer++;
        if (state_object.led0_blink_timer >= BLINK_INDICATOR_TIME_MS) {
            LED_set(LED0, LED_OFF);
            state_object.led0_blink_timer = 0;
        }
    }
    
    if (state_object.led1_blink_timer > 0) {
        state_object.led1_blink_timer++;
        if (state_object.led1_blink_timer >= BLINK_INDICATOR_TIME_MS) {
            LED_set(LED1, LED_OFF);
            state_object.led1_blink_timer = 0;
        }
    }
    
    // BTN0: Add bit 0 to current character
    if (BTN_check_clear_pressed(BTN0)) {
        if (state_object.bit_count < 8) {
            state_object.bit_count++;
            LED_set(LED0, LED_ON);
            state_object.led0_blink_timer = 1;
            printk("Bit %d: 0 | Current char: 0x%02X (%d bits)\n", 
                   state_object.bit_count - 1, state_object.current_char, state_object.bit_count);
        } else if (state_object.bit_count == 8) {
            // Character complete, save it and start new one
            if (state_object.string_length < MAX_STRING_LENGTH - 1) {
                state_object.string_buffer[state_object.string_length] = state_object.current_char;
                state_object.string_length++;
                state_object.string_buffer[state_object.string_length] = '\0';
                
                printk("Character auto-saved: '%c' (0x%02X)\n", 
                       state_object.current_char >= 32 ? state_object.current_char : '?', 
                       state_object.current_char);
                printk("Current string: \"%s\"\n", state_object.string_buffer);
                
                // Start new character
                state_object.current_char = 0;
                state_object.bit_count = 1; // This bit
                LED_set(LED0, LED_ON);
                state_object.led0_blink_timer = 1;
                printk("Bit 0: 0 | Current char: 0x%02X (1 bits)\n", state_object.current_char);
            }
        }
    }
    
    // BTN1: Add bit 1 to current character
    if (BTN_check_clear_pressed(BTN1)) {
        if (state_object.bit_count < 8) {
            state_object.current_char |= (1 << state_object.bit_count);
            state_object.bit_count++;
            LED_set(LED1, LED_ON);
            state_object.led1_blink_timer = 1;
            printk("Bit %d: 1 | Current char: 0x%02X (%d bits)\n", 
                   state_object.bit_count - 1, state_object.current_char, state_object.bit_count);
        } else if (state_object.bit_count == 8) {
            // Character complete, save it and start new one
            if (state_object.string_length < MAX_STRING_LENGTH - 1) {
                state_object.string_buffer[state_object.string_length] = state_object.current_char;
                state_object.string_length++;
                state_object.string_buffer[state_object.string_length] = '\0';
                
                printk("Character auto-saved: '%c' (0x%02X)\n", 
                       state_object.current_char >= 32 ? state_object.current_char : '?', 
                       state_object.current_char);
                printk("Current string: \"%s\"\n", state_object.string_buffer);
                
                // Start new character
                state_object.current_char = 1; // This bit
                state_object.bit_count = 1;
                LED_set(LED1, LED_ON);
                state_object.led1_blink_timer = 1;
                printk("Bit 0: 1 | Current char: 0x%02X (1 bits)\n", state_object.current_char);
            }
        }
    }
    
    // BTN2: Delete entire string
    if (BTN_check_clear_pressed(BTN2)) {
        printk("String deleted\n");
        memset(state_object.string_buffer, 0, sizeof(state_object.string_buffer));
        state_object.string_length = 0;
        smf_set_state(SMF_CTX(&state_object), &states[CHAR_ENTRY_STATE]);
    }
    
    // BTN3: Save and finalize string
    if (BTN_check_clear_pressed(BTN3)) {
        // If there's a partial character, save it first
        if (state_object.bit_count == 8 && state_object.string_length < MAX_STRING_LENGTH - 1) {
            state_object.string_buffer[state_object.string_length] = state_object.current_char;
            state_object.string_length++;
            state_object.string_buffer[state_object.string_length] = '\0';
            printk("Final character saved: '%c' (0x%02X)\n", 
                   state_object.current_char >= 32 ? state_object.current_char : '?', 
                   state_object.current_char);
        }
        
        printk("String finalized: \"%s\"\n", state_object.string_buffer);
        smf_set_state(SMF_CTX(&state_object), &states[STRING_CONFIRM_STATE]);
    }
    
    return SMF_EVENT_HANDLED;
}

static void string_build_state_exit(void *o) {
    printk("=== Exiting STRING_BUILD state ===\n");
}

// STRING_CONFIRM_STATE - Confirm or delete finalized string
static void string_confirm_state_entry(void *o) {
    printk("\n=== Entering STRING_CONFIRM state ===\n");
    printk("String ready: \"%s\"\n", state_object.string_buffer);
    printk("BTN2: Delete and restart | BTN3: Send to serial\n");
    
    state_object.led_blink_counter = 0;
    state_object.led3_state = false;
    
    LED_set(LED0, LED_OFF);
    LED_set(LED1, LED_OFF);
    LED_set(LED2, LED_OFF);
    LED_set(LED3, LED_OFF);
}

static enum smf_state_result string_confirm_state_run(void *o) {
    // Check for standby transition
    check_standby_transition();
    if (state_object.ctx.current == &states[STANDBY_STATE]) {
        return SMF_EVENT_HANDLED;
    }
    
    // Handle LED3 blinking at 16 Hz (31.25ms on, 31.25ms off)
    state_object.led_blink_counter++;
    if (state_object.led_blink_counter >= 31) {
        state_object.led3_state = !state_object.led3_state;
        LED_set(LED3, state_object.led3_state ? LED_ON : LED_OFF);
        state_object.led_blink_counter = 0;
    }
    
    // BTN2: Delete string and go back to entry
    if (BTN_check_clear_pressed(BTN2)) {
        printk("String deleted, returning to entry mode\n");
        memset(state_object.string_buffer, 0, sizeof(state_object.string_buffer));
        state_object.string_length = 0;
        smf_set_state(SMF_CTX(&state_object), &states[CHAR_ENTRY_STATE]);
    }
    
    // BTN3: Send to serial monitor
    if (BTN_check_clear_pressed(BTN3)) {
        printk("\n========================================\n");
        printk("TRANSMITTED STRING: \"%s\"\n", state_object.string_buffer);
        printk("========================================\n\n");
        
        // Reset and go back to entry
        memset(state_object.string_buffer, 0, sizeof(state_object.string_buffer));
        state_object.string_length = 0;
        smf_set_state(SMF_CTX(&state_object), &states[CHAR_ENTRY_STATE]);
    }
    
    return SMF_EVENT_HANDLED;
}

static void string_confirm_state_exit(void *o) {
    printk("=== Exiting STRING_CONFIRM state ===\n");
}

// STANDBY_STATE - All LEDs pulse, return to previous state on any button
static void standby_state_entry(void *o) {
    printk("\n=== Entering STANDBY state ===\n");
    printk("All LEDs pulsing. Press any button to return.\n");
    
    state_object.pwm_duty = 0;
    state_object.pwm_increasing = true;
    
    LED_pwm(LED0, 0);
    LED_pwm(LED1, 0);
    LED_pwm(LED2, 0);
    LED_pwm(LED3, 0);
}

static enum smf_state_result standby_state_run(void *o) {
    // Handle PWM pulsing
    if (state_object.pwm_increasing) {
        state_object.pwm_duty += 2;
        if (state_object.pwm_duty >= 100) {
            state_object.pwm_duty = 100;
            state_object.pwm_increasing = false;
        }
    } else {
        if (state_object.pwm_duty >= 2) {
            state_object.pwm_duty -= 2;
        } else {
            state_object.pwm_duty = 0;
            state_object.pwm_increasing = true;
        }
    }
    
    LED_pwm(LED0, state_object.pwm_duty);
    LED_pwm(LED1, state_object.pwm_duty);
    LED_pwm(LED2, state_object.pwm_duty);
    LED_pwm(LED3, state_object.pwm_duty);
    
    // Check for any button press to exit
    if (BTN_check_clear_pressed(BTN0) || BTN_check_clear_pressed(BTN1) || 
        BTN_check_clear_pressed(BTN2) || BTN_check_clear_pressed(BTN3)) {
        
        printk("Exiting standby, returning to previous state\n");
        smf_set_state(SMF_CTX(&state_object), &states[state_object.previous_state]);
    }
    
    return SMF_EVENT_HANDLED;
}

static void standby_state_exit(void *o) {
    printk("=== Exiting STANDBY state ===\n");
    
    // Turn off PWM on all LEDs
    LED_set(LED0, LED_OFF);
    LED_set(LED1, LED_OFF);
    LED_set(LED2, LED_OFF);
    LED_set(LED3, LED_OFF);
}
