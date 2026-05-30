/**
  * @file     board_led.c
  * @brief    LED state machine — cleanly decoupled from protocol logic.
  *
  * The caller selects a *pattern* at mode-transition points; the
  * tick function (called from the main loop) handles timing and GPIO.
  */

#include "board_led.h"
#include "at32f421.h"
#include "wk_system.h"

/* ── hardware abstraction (PB0, active-low) ─────────────────────── */

static void board_led_gpio_write(uint8_t on)
{
  if(on != 0U)
  {
    gpio_bits_reset(GPIOB, GPIO_PINS_0);
  }
  else
  {
    gpio_bits_set(GPIOB, GPIO_PINS_0);
  }
}

/* ── pattern definitions ────────────────────────────────────────── */

typedef enum
{
  BOARD_LED_PAT_OFF        = 0,
  BOARD_LED_PAT_ON         = 1,
  BOARD_LED_PAT_FAST_BLINK = 2,
  BOARD_LED_PAT_SLOW_BLINK = 3
} board_led_pattern_t;

#define BOARD_LED_FAST_HALF_US  25000U   /* 20 Hz */
#define BOARD_LED_SLOW_HALF_US 250000U   /*  2 Hz */

/* ── state machine ──────────────────────────────────────────────── */

static board_led_pattern_t g_led_pattern = BOARD_LED_PAT_OFF;
static uint32_t            g_led_toggle_tick;
static uint8_t             g_led_state;

void board_led_init(void)
{
  gpio_init_type gpio_init_struct;

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode           = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins           = GPIO_PINS_0;
  gpio_init_struct.gpio_pull           = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  g_led_pattern      = BOARD_LED_PAT_OFF;
  g_led_toggle_tick  = 0U;
  g_led_state        = 0U;
  board_led_gpio_write(0U);
}

/* ── public API ─────────────────────────────────────────────────── */

void board_led_off(void)
{
  g_led_pattern = BOARD_LED_PAT_OFF;
  g_led_state   = 0U;
  board_led_gpio_write(0U);
}

void board_led_on(void)
{
  g_led_pattern = BOARD_LED_PAT_ON;
  g_led_state   = 1U;
  board_led_gpio_write(1U);
}

void board_led_fast_blink(void)
{
  /* Only restart the phase when transitioning into the pattern,
     otherwise a fast stream of calls would keep resetting the
     toggle timer and the LED would stay stuck in one state. */
  if(g_led_pattern != BOARD_LED_PAT_FAST_BLINK)
  {
    g_led_pattern     = BOARD_LED_PAT_FAST_BLINK;
    g_led_toggle_tick = wk_timebase_raw_tick();
    g_led_state       = 0U;
    board_led_gpio_write(0U);
  }
}

void board_led_slow_blink(void)
{
  if(g_led_pattern != BOARD_LED_PAT_SLOW_BLINK)
  {
    g_led_pattern     = BOARD_LED_PAT_SLOW_BLINK;
    g_led_toggle_tick = wk_timebase_raw_tick();
    g_led_state       = 0U;
    board_led_gpio_write(0U);
  }
}

/* ── per-loop tick ──────────────────────────────────────────────── */

void board_led_tick(void)
{
  uint32_t elapsed;
  uint32_t half_period_us;

  if(g_led_pattern == BOARD_LED_PAT_OFF || g_led_pattern == BOARD_LED_PAT_ON)
  {
    return;  /* static level — nothing to do */
  }

  half_period_us = (g_led_pattern == BOARD_LED_PAT_FAST_BLINK)
                     ? BOARD_LED_FAST_HALF_US
                     : BOARD_LED_SLOW_HALF_US;

  elapsed = wk_timebase_elapsed_us(g_led_toggle_tick);
  if(elapsed >= half_period_us)
  {
    g_led_state ^= 1U;
    board_led_gpio_write(g_led_state);
    g_led_toggle_tick = wk_timebase_raw_tick();
  }
}
