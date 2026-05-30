#ifndef BOARD_LED_H
#define BOARD_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LED patterns — call these at module / mode transitions.
   board_led_tick() drives the actual GPIO inside the main loop. */
void board_led_init(void);
void board_led_tick(void);
void board_led_off(void);
void board_led_on(void);
void board_led_fast_blink(void);
void board_led_slow_blink(void);

#ifdef __cplusplus
}
#endif

#endif
