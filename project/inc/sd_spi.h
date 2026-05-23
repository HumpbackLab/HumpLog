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
