/* add user code begin Header */
/**
  **************************************************************************
  * @file     wk_spi.c
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
#include "wk_spi.h"

static uint8_t g_wk_spi_dma_dummy_tx = 0xFFU;
static uint8_t g_wk_spi_dma_dummy_rx;

static uint8_t wk_spi_transfer_dma_internal(spi_type *spi_x,
                                            dma_channel_type *rx_channel,
                                            dma_channel_type *tx_channel,
                                            uint32_t rx_flag,
                                            uint32_t tx_flag,
                                            const uint8_t *tx_data,
                                            uint8_t *rx_data,
                                            uint16_t length);
static uint8_t wk_spi_transfer_blocking_internal(spi_type *spi_x,
                                                 const uint8_t *tx_data,
                                                 uint8_t *rx_data,
                                                 uint16_t length);
static void wk_spi1_reconfigure(spi_mclk_freq_div_type divider);

/* add user code begin 0 */

/* add user code end 0 */

/**
  * @brief  init spi1 function
  * @param  none
  * @retval none
  */
void wk_spi1_init(void)
{
  /* add user code begin spi1_init 0 */

  /* add user code end spi1_init 0 */

  gpio_init_type gpio_init_struct;
  spi_init_type spi_init_struct;

  gpio_default_para_init(&gpio_init_struct);
  spi_default_para_init(&spi_init_struct);

  /* add user code begin spi1_init 1 */

  /* add user code end spi1_init 1 */

  /* configure the SCK pin */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE5, GPIO_MUX_0);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_5;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the MISO pin */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE6, GPIO_MUX_0);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_6;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the MOSI pin */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE7, GPIO_MUX_0);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_7;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the CS pin as software-controlled gpio */
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_4;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_bits_set(GPIOA, GPIO_PINS_4);

  /* configure param */
  spi_init_struct.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi_init_struct.master_slave_mode = SPI_MODE_MASTER;
  spi_init_struct.frame_bit_num = SPI_FRAME_8BIT;
  spi_init_struct.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi_init_struct.mclk_freq_division = SPI_MCLK_DIV_256;
  spi_init_struct.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  spi_init_struct.clock_phase = SPI_CLOCK_PHASE_1EDGE;
  spi_init_struct.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  spi_init(SPI1, &spi_init_struct);
  spi_software_cs_internal_level_set(SPI1, SPI_SWCS_INTERNAL_LEVEL_HIGHT);

  /* add user code begin spi1_init 2 */

  /* add user code end spi1_init 2 */
  
  spi_enable(SPI1, TRUE);

  /* add user code begin spi1_init 3 */

  /* add user code end spi1_init 3 */
}

/**
  * @brief  init spi2 function
  * @param  none
  * @retval none
  */
void wk_spi2_init(void)
{
  /* add user code begin spi2_init 0 */

  /* add user code end spi2_init 0 */

  gpio_init_type gpio_init_struct;
  spi_init_type spi_init_struct;

  gpio_default_para_init(&gpio_init_struct);
  spi_default_para_init(&spi_init_struct);

  /* add user code begin spi2_init 1 */

  /* add user code end spi2_init 1 */

  /* configure the SCK pin */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE3, GPIO_MUX_6);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_3;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the MISO pin */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE4, GPIO_MUX_6);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_4;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the MOSI pin */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE5, GPIO_MUX_6);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_5;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the CS pin */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE15, GPIO_MUX_6);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_15;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure param */
  spi_init_struct.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi_init_struct.master_slave_mode = SPI_MODE_MASTER;
  spi_init_struct.frame_bit_num = SPI_FRAME_8BIT;
  spi_init_struct.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi_init_struct.mclk_freq_division = SPI_MCLK_DIV_4;
  spi_init_struct.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  spi_init_struct.clock_phase = SPI_CLOCK_PHASE_1EDGE;
  spi_init_struct.cs_mode_selection = SPI_CS_HARDWARE_MODE;
  spi_init(SPI2, &spi_init_struct);

  /* configure the cs pin output */
  spi_hardware_cs_output_enable(SPI2, TRUE);

  /* add user code begin spi2_init 2 */

  /* add user code end spi2_init 2 */
  
  spi_enable(SPI2, TRUE);

  /* add user code begin spi2_init 3 */

  /* add user code end spi2_init 3 */
}

void wk_spi1_set_clock_div(spi_mclk_freq_div_type divider)
{
  wk_spi1_reconfigure(divider);
}

void wk_spi1_cs_set(uint8_t asserted)
{
  if(asserted != 0U)
  {
    gpio_bits_reset(GPIOA, GPIO_PINS_4);
  }
  else
  {
    gpio_bits_set(GPIOA, GPIO_PINS_4);
  }
}

uint8_t wk_spi1_transfer_byte(uint8_t tx_value)
{
  uint8_t rx_value;

  rx_value = 0xFFU;
  (void)wk_spi_transfer_blocking_internal(SPI1, &tx_value, &rx_value, 1U);
  return rx_value;
}

uint8_t wk_spi1_transfer(const uint8_t *tx_data, uint8_t *rx_data, uint16_t length)
{
  return wk_spi_transfer_blocking_internal(SPI1, tx_data, rx_data, length);
}

static uint8_t wk_spi_transfer_dma_internal(spi_type *spi_x,
                                            dma_channel_type *rx_channel,
                                            dma_channel_type *tx_channel,
                                            uint32_t rx_flag,
                                            uint32_t tx_flag,
                                            const uint8_t *tx_data,
                                            uint8_t *rx_data,
                                            uint16_t length)
{
  dma_init_type dma_init_struct;

  if(length == 0U)
  {
    return 1U;
  }

  dma_channel_enable(rx_channel, FALSE);
  dma_channel_enable(tx_channel, FALSE);
  dma_reset(rx_channel);
  dma_reset(tx_channel);
  dma_flag_clear(rx_flag | tx_flag);

  dma_default_para_init(&dma_init_struct);
  dma_init_struct.peripheral_base_addr = (uint32_t)&spi_x->dt;
  dma_init_struct.memory_base_addr = rx_data != NULL ? (uint32_t)rx_data : (uint32_t)&g_wk_spi_dma_dummy_rx;
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.buffer_size = length;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.memory_inc_enable = rx_data != NULL ? TRUE : FALSE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_VERY_HIGH;
  dma_init(rx_channel, &dma_init_struct);

  dma_default_para_init(&dma_init_struct);
  dma_init_struct.peripheral_base_addr = (uint32_t)&spi_x->dt;
  dma_init_struct.memory_base_addr = tx_data != NULL ? (uint32_t)tx_data : (uint32_t)&g_wk_spi_dma_dummy_tx;
  dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
  dma_init_struct.buffer_size = length;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.memory_inc_enable = tx_data != NULL ? TRUE : FALSE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_VERY_HIGH;
  dma_init(tx_channel, &dma_init_struct);

  spi_i2s_dma_receiver_enable(spi_x, TRUE);
  spi_i2s_dma_transmitter_enable(spi_x, TRUE);
  dma_channel_enable(rx_channel, TRUE);
  dma_channel_enable(tx_channel, TRUE);

  while(dma_flag_get(rx_flag) == RESET || dma_flag_get(tx_flag) == RESET)
  {
  }

  while(spi_i2s_flag_get(spi_x, SPI_I2S_BF_FLAG) != RESET)
  {
  }

  dma_channel_enable(rx_channel, FALSE);
  dma_channel_enable(tx_channel, FALSE);
  spi_i2s_dma_receiver_enable(spi_x, FALSE);
  spi_i2s_dma_transmitter_enable(spi_x, FALSE);
  dma_flag_clear(rx_flag | tx_flag);
  return 1U;
}

uint8_t wk_spi1_transfer_dma(const uint8_t *tx_data, uint8_t *rx_data, uint16_t length)
{
  return wk_spi_transfer_dma_internal(SPI1,
                                      DMA1_CHANNEL2,
                                      DMA1_CHANNEL3,
                                      DMA1_GL2_FLAG,
                                      DMA1_GL3_FLAG,
                                      tx_data,
                                      rx_data,
                                      length);
}

uint8_t wk_spi2_transfer_dma(const uint8_t *tx_data, uint8_t *rx_data, uint16_t length)
{
  return wk_spi_transfer_blocking_internal(SPI2, tx_data, rx_data, length);
}

static uint8_t wk_spi_transfer_blocking_internal(spi_type *spi_x,
                                                 const uint8_t *tx_data,
                                                 uint8_t *rx_data,
                                                 uint16_t length)
{
  uint16_t index;
  uint8_t tx_value;

  for(index = 0U; index < length; ++index)
  {
    tx_value = tx_data != NULL ? tx_data[index] : g_wk_spi_dma_dummy_tx;
    while(spi_i2s_flag_get(spi_x, SPI_I2S_TDBE_FLAG) == RESET)
    {
    }
    spi_i2s_data_transmit(spi_x, tx_value);
    while(spi_i2s_flag_get(spi_x, SPI_I2S_RDBF_FLAG) == RESET)
    {
    }
    tx_value = (uint8_t)spi_i2s_data_receive(spi_x);
    if(rx_data != NULL)
    {
      rx_data[index] = tx_value;
    }
  }

  while(spi_i2s_flag_get(spi_x, SPI_I2S_BF_FLAG) != RESET)
  {
  }

  return 1U;
}

static void wk_spi1_reconfigure(spi_mclk_freq_div_type divider)
{
  spi_init_type spi_init_struct;

  spi_enable(SPI1, FALSE);
  spi_default_para_init(&spi_init_struct);
  spi_init_struct.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi_init_struct.master_slave_mode = SPI_MODE_MASTER;
  spi_init_struct.frame_bit_num = SPI_FRAME_8BIT;
  spi_init_struct.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi_init_struct.mclk_freq_division = divider;
  spi_init_struct.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  spi_init_struct.clock_phase = SPI_CLOCK_PHASE_1EDGE;
  spi_init_struct.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  spi_init(SPI1, &spi_init_struct);
  spi_software_cs_internal_level_set(SPI1, SPI_SWCS_INTERNAL_LEVEL_HIGHT);
  spi_enable(SPI1, TRUE);
}

/* add user code begin 1 */

/* add user code end 1 */
