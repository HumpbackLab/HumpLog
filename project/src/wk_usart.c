/* add user code begin Header */
/**
  **************************************************************************
  * @file     wk_usart.c
  * @brief    work bench config program
  **************************************************************************
  * Copyright (c) 2025, Artery Technology, All rights reserved.
  *
  * The software Board Support Package (BSP) that is made available to
  * download from Artery official website is the copyrighted work of Artery.
  * Artery authorizes customers to use, copy, and distribute the BSP
  * software and its related documentation for the purpose of design and
  * development in conjunction with Artery microcontrollers. Use of the
  * software is governed by this copyright notice and the following disclaimer.
  *
  * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
  * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
  * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
  * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
  * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
  *
  **************************************************************************
  */
/* add user code end Header */

/* Includes ------------------------------------------------------------------*/
#include "wk_usart.h"
#include <stddef.h>
#include <string.h>

#define WK_USART1_RX_DMA_BUFFER_SIZE 1536U
#define WK_USART1_TX_DMA_BUFFER_SIZE 768U

static volatile uint8_t g_wk_usart1_rx_dma_buffer[WK_USART1_RX_DMA_BUFFER_SIZE];
static volatile uint16_t g_wk_usart1_rx_read_index;
static volatile uint32_t g_wk_usart1_rx_wrap_count;
static volatile uint32_t g_wk_usart1_rx_total_read_count;
static volatile uint32_t g_wk_usart1_rx_overrun_count;
static volatile uint8_t g_wk_usart1_tx_dma_buffer[WK_USART1_TX_DMA_BUFFER_SIZE];
static volatile uint16_t g_wk_usart1_tx_read_index;
static volatile uint16_t g_wk_usart1_tx_write_index;
static volatile uint16_t g_wk_usart1_tx_dma_length;
static volatile uint8_t g_wk_usart1_tx_dma_busy;
static volatile uint8_t g_wk_usart1_dma_suspended;

static void wk_usart1_rx_dma_config(void);
static void wk_usart1_tx_dma_channel_reset(void);
static void wk_usart1_tx_kick_locked(void);
static uint16_t wk_usart1_rx_dma_write_index_get(void);
static uint32_t wk_usart1_rx_total_written_get(uint16_t *write_index);
static void wk_usart1_rx_sync(void);
static uint16_t wk_usart1_tx_free_space_get(void);
static void wk_usart1_rx_state_reset(void);

/* add user code begin 0 */

/* add user code end 0 */

/**
  * @brief  init usart1 function
  * @param  none
  * @retval none
  */
void wk_usart1_init(void)
{
  /* add user code begin usart1_init 0 */

  /* add user code end usart1_init 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin usart1_init 1 */

  /* add user code end usart1_init 1 */

  /* configure the TX pin */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE9, GPIO_MUX_1);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_9;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the RX pin */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE10, GPIO_MUX_1);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_10;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure param */
  usart_init(USART1, 115200, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(USART1, TRUE);
  usart_receiver_enable(USART1, TRUE);
  usart_parity_selection_config(USART1, USART_PARITY_NONE);

  usart_hardware_flow_control_set(USART1, USART_HARDWARE_FLOW_NONE);

  scfg_usart1_tx_dma_channel_remap(SCFG_USART1_TX_TO_DMA_CHANNEL_4);
  scfg_usart1_rx_dma_channel_remap(SCFG_USART1_RX_TO_DMA_CHANNEL_5);

  /* add user code begin usart1_init 2 */

  /* add user code end usart1_init 2 */
  
  usart_enable(USART1, TRUE);
  wk_usart1_rx_state_reset();
  wk_usart1_rx_dma_config();
  wk_usart1_tx_dma_channel_reset();
  usart_dma_receiver_enable(USART1, TRUE);

  /* add user code begin usart1_init 3 */

  /* add user code end usart1_init 3 */
}

void wk_usart1_set_baud(uint32_t baud_rate)
{
  wk_usart1_dma_suspend();
  wk_usart1_rx_state_reset();
  usart_enable(USART1, FALSE);
  usart_init(USART1, baud_rate, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(USART1, TRUE);
  usart_receiver_enable(USART1, TRUE);
  usart_parity_selection_config(USART1, USART_PARITY_NONE);
  usart_hardware_flow_control_set(USART1, USART_HARDWARE_FLOW_NONE);
  usart_enable(USART1, TRUE);
  wk_usart1_dma_resume();
}

static void wk_usart1_rx_state_reset(void)
{
  memset((void *)g_wk_usart1_rx_dma_buffer, 0, sizeof(g_wk_usart1_rx_dma_buffer));
  g_wk_usart1_rx_read_index = 0U;
  g_wk_usart1_rx_wrap_count = 0U;
  g_wk_usart1_rx_total_read_count = 0U;
  g_wk_usart1_rx_overrun_count = 0U;
}

static void wk_usart1_rx_dma_config(void)
{
  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL5);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.peripheral_base_addr = (uint32_t)&USART1->dt;
  dma_init_struct.memory_base_addr = (uint32_t)g_wk_usart1_rx_dma_buffer;
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.buffer_size = WK_USART1_RX_DMA_BUFFER_SIZE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.loop_mode_enable = TRUE;
  dma_init_struct.priority = DMA_PRIORITY_HIGH;
  dma_init(DMA1_CHANNEL5, &dma_init_struct);
  dma_flag_clear(DMA1_GL5_FLAG);
  dma_interrupt_enable(DMA1_CHANNEL5, DMA_FDT_INT | DMA_DTERR_INT, TRUE);
  dma_channel_enable(DMA1_CHANNEL5, TRUE);
}

static void wk_usart1_tx_dma_channel_reset(void)
{
  dma_reset(DMA1_CHANNEL4);
  dma_flag_clear(DMA1_GL4_FLAG);
  dma_interrupt_enable(DMA1_CHANNEL4, DMA_FDT_INT | DMA_DTERR_INT, TRUE);
}

static uint16_t wk_usart1_rx_dma_write_index_get(void)
{
  return (uint16_t)(WK_USART1_RX_DMA_BUFFER_SIZE - dma_data_number_get(DMA1_CHANNEL5)) %
         WK_USART1_RX_DMA_BUFFER_SIZE;
}

static uint32_t wk_usart1_rx_total_written_get(uint16_t *write_index)
{
  uint32_t wrap_count_before;
  uint32_t wrap_count_after;
  uint16_t current_write_index;

  do
  {
    wrap_count_before = g_wk_usart1_rx_wrap_count;
    current_write_index = wk_usart1_rx_dma_write_index_get();
    wrap_count_after = g_wk_usart1_rx_wrap_count;
  }
  while(wrap_count_before != wrap_count_after);

  if(write_index != NULL)
  {
    *write_index = current_write_index;
  }

  return wrap_count_before + current_write_index;
}

static void wk_usart1_rx_sync(void)
{
  uint16_t current_write_index;
  uint32_t total_written;
  uint32_t unread;
  uint32_t dropped;

  total_written = wk_usart1_rx_total_written_get(&current_write_index);
  unread = total_written - g_wk_usart1_rx_total_read_count;
  if(unread <= WK_USART1_RX_DMA_BUFFER_SIZE)
  {
    return;
  }

  dropped = unread - WK_USART1_RX_DMA_BUFFER_SIZE;
  g_wk_usart1_rx_overrun_count += dropped;
  g_wk_usart1_rx_total_read_count += dropped;
  g_wk_usart1_rx_read_index = current_write_index;
}

static uint16_t wk_usart1_tx_free_space_get(void)
{
  uint16_t read_index;
  uint16_t write_index;

  read_index = g_wk_usart1_tx_read_index;
  write_index = g_wk_usart1_tx_write_index;
  if(write_index >= read_index)
  {
    return (uint16_t)(WK_USART1_TX_DMA_BUFFER_SIZE - (write_index - read_index) - 1U);
  }

  return (uint16_t)(read_index - write_index - 1U);
}

static void wk_usart1_tx_kick_locked(void)
{
  dma_init_type dma_init_struct;
  uint16_t transfer_length;

  if(g_wk_usart1_dma_suspended != 0U || g_wk_usart1_tx_dma_busy != 0U)
  {
    return;
  }

  if(g_wk_usart1_tx_read_index == g_wk_usart1_tx_write_index)
  {
    return;
  }

  if(g_wk_usart1_tx_write_index > g_wk_usart1_tx_read_index)
  {
    transfer_length = (uint16_t)(g_wk_usart1_tx_write_index - g_wk_usart1_tx_read_index);
  }
  else
  {
    transfer_length = (uint16_t)(WK_USART1_TX_DMA_BUFFER_SIZE - g_wk_usart1_tx_read_index);
  }

  dma_channel_enable(DMA1_CHANNEL4, FALSE);
  dma_flag_clear(DMA1_GL4_FLAG);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.peripheral_base_addr = (uint32_t)&USART1->dt;
  dma_init_struct.memory_base_addr = (uint32_t)&g_wk_usart1_tx_dma_buffer[g_wk_usart1_tx_read_index];
  dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
  dma_init_struct.buffer_size = transfer_length;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_HIGH;
  dma_init(DMA1_CHANNEL4, &dma_init_struct);
  dma_interrupt_enable(DMA1_CHANNEL4, DMA_FDT_INT | DMA_DTERR_INT, TRUE);
  g_wk_usart1_tx_dma_length = transfer_length;
  g_wk_usart1_tx_dma_busy = 1U;
  usart_dma_transmitter_enable(USART1, TRUE);
  dma_channel_enable(DMA1_CHANNEL4, TRUE);
}

uint8_t wk_usart1_readable(void)
{
  wk_usart1_rx_sync();
  return wk_usart1_rx_total_written_get(NULL) != g_wk_usart1_rx_total_read_count ? 1U : 0U;
}

uint8_t wk_usart1_read_byte(void)
{
  uint8_t value;

  while(wk_usart1_readable() == 0U)
  {
  }

  value = g_wk_usart1_rx_dma_buffer[g_wk_usart1_rx_read_index];
  g_wk_usart1_rx_read_index = (uint16_t)((g_wk_usart1_rx_read_index + 1U) % WK_USART1_RX_DMA_BUFFER_SIZE);
  ++g_wk_usart1_rx_total_read_count;
  return value;
}

void wk_usart1_discard_rx(void)
{
  uint16_t current_write_index;

  g_wk_usart1_rx_total_read_count = wk_usart1_rx_total_written_get(&current_write_index);
  g_wk_usart1_rx_read_index = current_write_index;
}

uint32_t wk_usart1_rx_overrun_bytes(void)
{
  wk_usart1_rx_sync();
  return g_wk_usart1_rx_overrun_count;
}

void wk_usart1_rx_overrun_clear(void)
{
  g_wk_usart1_rx_overrun_count = 0U;
}

void wk_usart1_write_byte(uint8_t byte)
{
  while(wk_usart1_tx_free_space_get() == 0U)
  {
    wk_usart1_tx_kick_locked();
  }

  g_wk_usart1_tx_dma_buffer[g_wk_usart1_tx_write_index] = byte;
  g_wk_usart1_tx_write_index = (uint16_t)((g_wk_usart1_tx_write_index + 1U) % WK_USART1_TX_DMA_BUFFER_SIZE);
  wk_usart1_tx_kick_locked();
}

void wk_usart1_write_buffer(const uint8_t *data, uint32_t length)
{
  uint32_t index;

  if(data == NULL)
  {
    return;
  }

  for(index = 0; index < length; ++index)
  {
    wk_usart1_write_byte(data[index]);
  }
}

void wk_usart1_write_string(const char *text)
{
  if(text == NULL)
  {
    return;
  }

  while(*text != '\0')
  {
    wk_usart1_write_byte((uint8_t)*text);
    ++text;
  }
}

void wk_usart1_flush(void)
{
  while(g_wk_usart1_tx_dma_busy != 0U || g_wk_usart1_tx_read_index != g_wk_usart1_tx_write_index)
  {
    wk_usart1_tx_kick_locked();
  }
}

void wk_usart1_dma_suspend(void)
{
  if(g_wk_usart1_dma_suspended != 0U)
  {
    return;
  }

  wk_usart1_flush();
  g_wk_usart1_dma_suspended = 1U;
  dma_channel_enable(DMA1_CHANNEL4, FALSE);
  dma_channel_enable(DMA1_CHANNEL5, FALSE);
  dma_flag_clear(DMA1_GL4_FLAG | DMA1_GL5_FLAG);
  usart_dma_transmitter_enable(USART1, FALSE);
  usart_dma_receiver_enable(USART1, FALSE);
  g_wk_usart1_tx_dma_busy = 0U;
  g_wk_usart1_tx_dma_length = 0U;
}

void wk_usart1_dma_resume(void)
{
  if(g_wk_usart1_dma_suspended == 0U)
  {
    return;
  }

  wk_usart1_rx_state_reset();
  wk_usart1_rx_dma_config();
  wk_usart1_tx_dma_channel_reset();
  usart_dma_receiver_enable(USART1, TRUE);
  g_wk_usart1_dma_suspended = 0U;
  wk_usart1_tx_kick_locked();
}

void wk_usart1_dma_irq_handler(void)
{
  if(dma_interrupt_flag_get(DMA1_FDT5_FLAG) != RESET)
  {
    dma_flag_clear(DMA1_GL5_FLAG);
    g_wk_usart1_rx_wrap_count += WK_USART1_RX_DMA_BUFFER_SIZE;
  }

  if(dma_interrupt_flag_get(DMA1_DTERR5_FLAG) != RESET)
  {
    dma_flag_clear(DMA1_GL5_FLAG);
  }

  if(dma_interrupt_flag_get(DMA1_FDT4_FLAG) != RESET)
  {
    dma_channel_enable(DMA1_CHANNEL4, FALSE);
    dma_flag_clear(DMA1_GL4_FLAG);
    g_wk_usart1_tx_read_index = (uint16_t)((g_wk_usart1_tx_read_index + g_wk_usart1_tx_dma_length) %
                                           WK_USART1_TX_DMA_BUFFER_SIZE);
    g_wk_usart1_tx_dma_length = 0U;
    g_wk_usart1_tx_dma_busy = 0U;
    wk_usart1_tx_kick_locked();
  }

  if(dma_interrupt_flag_get(DMA1_DTERR4_FLAG) != RESET)
  {
    dma_channel_enable(DMA1_CHANNEL4, FALSE);
    dma_flag_clear(DMA1_GL4_FLAG);
    g_wk_usart1_tx_dma_busy = 0U;
    g_wk_usart1_tx_dma_length = 0U;
  }
}

/* add user code begin 1 */

/* add user code end 1 */
