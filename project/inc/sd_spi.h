#ifndef SD_SPI_H
#define SD_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint8_t sd_spi_initialize(void);
uint8_t sd_spi_is_initialized(void);
uint8_t sd_spi_read_blocks(uint32_t sector, uint8_t *buffer, uint32_t count);
uint8_t sd_spi_write_blocks(uint32_t sector, const uint8_t *buffer, uint32_t count);
uint8_t sd_spi_get_sector_count(uint32_t *sector_count);
uint8_t sd_spi_get_sector_size(uint16_t *sector_size);
uint8_t sd_spi_get_erase_block_size(uint32_t *erase_block_size);
void sd_spi_sync(void);

#ifdef __cplusplus
}
#endif

#endif
