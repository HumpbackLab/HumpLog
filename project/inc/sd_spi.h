/**
 * HumpLog - OpenLog-compatible serial logger firmware for AT32F421
 * Copyright (C) 2025  HumpbackLab
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef SD_SPI_H
#define SD_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
  uint8_t manufacturer_id;
  char oem_id[3];
  char product_name[6];
  uint8_t product_revision_major;
  uint8_t product_revision_minor;
  uint32_t serial_number;
  uint16_t manufacture_year;
  uint8_t manufacture_month;
  uint32_t sector_count;
} sd_spi_card_info_t;

uint8_t sd_spi_initialize(void);
uint8_t sd_spi_is_initialized(void);
uint8_t sd_spi_read_blocks(uint32_t sector, uint8_t *buffer, uint32_t count);
uint8_t sd_spi_write_blocks(uint32_t sector, const uint8_t *buffer, uint32_t count);
uint8_t sd_spi_get_sector_count(uint32_t *sector_count);
uint8_t sd_spi_get_sector_size(uint16_t *sector_size);
uint8_t sd_spi_get_erase_block_size(uint32_t *erase_block_size);
uint8_t sd_spi_get_card_info(sd_spi_card_info_t *card_info);
void sd_spi_sync(void);

#ifdef __cplusplus
}
#endif

#endif
