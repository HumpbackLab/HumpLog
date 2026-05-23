#include "sd_spi.h"

#include "wk_spi.h"
#include "wk_system.h"

#include <string.h>

#define SD_SPI_SECTOR_SIZE 512U
#define SD_SPI_CMD_TIMEOUT_MS 500U
#define SD_SPI_DATA_TIMEOUT_MS 200U

#define SD_CMD0   0U
#define SD_CMD1   1U
#define SD_CMD8   8U
#define SD_CMD9   9U
#define SD_CMD10 10U
#define SD_CMD12 12U
#define SD_CMD16 16U
#define SD_CMD17 17U
#define SD_CMD24 24U
#define SD_CMD55 55U
#define SD_CMD58 58U
#define SD_ACMD41 (0x80U + 41U)

#define SD_TOKEN_START_BLOCK 0xFEU
#define SD_TOKEN_DATA_ACCEPTED 0x05U

#define SD_CARD_TYPE_NONE 0x00U
#define SD_CARD_TYPE_MMC  0x01U
#define SD_CARD_TYPE_SD1  0x02U
#define SD_CARD_TYPE_SD2  0x04U
#define SD_CARD_TYPE_BLOCK 0x08U

static uint8_t g_sd_card_type;
static uint8_t g_sd_initialized;
static uint32_t g_sd_sector_count;
static uint8_t g_sd_cid[16];

static void sd_spi_select(void);
static void sd_spi_deselect(void);
static uint8_t sd_spi_wait_ready(uint32_t timeout_ms);
static uint8_t sd_spi_send_command(uint8_t command, uint32_t argument);
static uint8_t sd_spi_receive_data_block(uint8_t *buffer, uint16_t length);
static uint8_t sd_spi_transmit_data_block(const uint8_t *buffer, uint8_t token);
static uint8_t sd_spi_read_csd(uint8_t *csd);
static uint8_t sd_spi_read_cid(uint8_t *cid);
static uint8_t sd_spi_parse_sector_count(const uint8_t *csd, uint32_t *sector_count);

static void sd_spi_select(void)
{
  wk_spi1_cs_set(1U);
}

static void sd_spi_deselect(void)
{
  wk_spi1_cs_set(0U);
  (void)wk_spi1_transfer_byte(0xFFU);
}

static uint8_t sd_spi_wait_ready(uint32_t timeout_ms)
{
  uint32_t elapsed;

  for(elapsed = 0U; elapsed < timeout_ms; ++elapsed)
  {
    if(wk_spi1_transfer_byte(0xFFU) == 0xFFU)
    {
      return 1U;
    }
    wk_delay_ms(1U);
  }

  return 0U;
}

static uint8_t sd_spi_send_command(uint8_t command, uint32_t argument)
{
  uint8_t response;
  uint8_t packet[6];
  uint8_t retries;

  if(command & 0x80U)
  {
    command &= 0x7FU;
    response = sd_spi_send_command(SD_CMD55, 0U);
    if(response > 1U)
    {
      return response;
    }
  }

  sd_spi_deselect();
  sd_spi_select();
  if(!sd_spi_wait_ready(SD_SPI_CMD_TIMEOUT_MS))
  {
    sd_spi_deselect();
    return 0xFFU;
  }

  packet[0] = (uint8_t)(0x40U | command);
  packet[1] = (uint8_t)(argument >> 24);
  packet[2] = (uint8_t)(argument >> 16);
  packet[3] = (uint8_t)(argument >> 8);
  packet[4] = (uint8_t)argument;
  packet[5] = 0x01U;
  if(command == SD_CMD0)
  {
    packet[5] = 0x95U;
  }
  else if(command == SD_CMD8)
  {
    packet[5] = 0x87U;
  }

  (void)wk_spi1_transfer(packet, NULL, sizeof(packet));

  if(command == SD_CMD12)
  {
    (void)wk_spi1_transfer_byte(0xFFU);
  }

  for(retries = 0U; retries < 10U; ++retries)
  {
    response = wk_spi1_transfer_byte(0xFFU);
    if((response & 0x80U) == 0U)
    {
      return response;
    }
  }

  return response;
}

static uint8_t sd_spi_receive_data_block(uint8_t *buffer, uint16_t length)
{
  uint8_t token;
  uint32_t elapsed;

  for(elapsed = 0U; elapsed < SD_SPI_DATA_TIMEOUT_MS; ++elapsed)
  {
    token = wk_spi1_transfer_byte(0xFFU);
    if(token == SD_TOKEN_START_BLOCK)
    {
      (void)wk_spi1_transfer(NULL, buffer, length);
      (void)wk_spi1_transfer_byte(0xFFU);
      (void)wk_spi1_transfer_byte(0xFFU);
      return 1U;
    }
    wk_delay_ms(1U);
  }

  return 0U;
}

static uint8_t sd_spi_transmit_data_block(const uint8_t *buffer, uint8_t token)
{
  uint8_t response;

  if(!sd_spi_wait_ready(SD_SPI_DATA_TIMEOUT_MS))
  {
    return 0U;
  }

  (void)wk_spi1_transfer_byte(token);
  if(token == 0xFDU)
  {
    return 1U;
  }

  (void)wk_spi1_transfer(buffer, NULL, SD_SPI_SECTOR_SIZE);
  (void)wk_spi1_transfer_byte(0xFFU);
  (void)wk_spi1_transfer_byte(0xFFU);
  response = wk_spi1_transfer_byte(0xFFU);
  if((response & 0x1FU) != SD_TOKEN_DATA_ACCEPTED)
  {
    return 0U;
  }

  return sd_spi_wait_ready(SD_SPI_DATA_TIMEOUT_MS);
}

static uint8_t sd_spi_read_csd(uint8_t *csd)
{
  if(sd_spi_send_command(SD_CMD9, 0U) != 0U)
  {
    sd_spi_deselect();
    return 0U;
  }

  if(!sd_spi_receive_data_block(csd, 16U))
  {
    sd_spi_deselect();
    return 0U;
  }

  sd_spi_deselect();
  return 1U;
}

static uint8_t sd_spi_read_cid(uint8_t *cid)
{
  if(sd_spi_send_command(SD_CMD10, 0U) != 0U)
  {
    sd_spi_deselect();
    return 0U;
  }

  if(!sd_spi_receive_data_block(cid, 16U))
  {
    sd_spi_deselect();
    return 0U;
  }

  sd_spi_deselect();
  return 1U;
}

static uint8_t sd_spi_parse_sector_count(const uint8_t *csd, uint32_t *sector_count)
{
  uint32_t csize;
  uint32_t mult;
  uint32_t blocknr;
  uint32_t block_len;

  if((csd[0] >> 6) == 1U)
  {
    csize = ((uint32_t)(csd[7] & 0x3FU) << 16) |
            ((uint32_t)csd[8] << 8) |
            csd[9];
    *sector_count = (csize + 1U) << 10;
    return 1U;
  }

  csize = ((uint32_t)(csd[6] & 0x03U) << 10) |
          ((uint32_t)csd[7] << 2) |
          ((csd[8] & 0xC0U) >> 6);
  mult = (uint32_t)((csd[9] & 0x03U) << 1) | ((csd[10] & 0x80U) >> 7);
  block_len = 1UL << (csd[5] & 0x0FU);
  blocknr = (csize + 1UL) << (mult + 2UL);
  *sector_count = (blocknr * block_len) / SD_SPI_SECTOR_SIZE;
  return 1U;
}

uint8_t sd_spi_initialize(void)
{
  uint8_t index;
  uint8_t response;
  uint8_t ocr[4];
  uint8_t csd[16];

  g_sd_initialized = 0U;
  g_sd_card_type = SD_CARD_TYPE_NONE;
  g_sd_sector_count = 0U;
  memset(g_sd_cid, 0, sizeof(g_sd_cid));

  wk_spi1_set_clock_div(SPI_MCLK_DIV_256);
  sd_spi_deselect();
  for(index = 0U; index < 10U; ++index)
  {
    (void)wk_spi1_transfer_byte(0xFFU);
  }

  response = 0xFFU;
  for(index = 0U; index < 10U; ++index)
  {
    response = sd_spi_send_command(SD_CMD0, 0U);
    sd_spi_deselect();
    if(response == 0x01U)
    {
      break;
    }
    wk_delay_ms(10U);
  }
  if(response != 0x01U)
  {
    return 0U;
  }

  response = sd_spi_send_command(SD_CMD8, 0x1AAU);
  if(response == 0x01U)
  {
    for(index = 0U; index < 4U; ++index)
    {
      ocr[index] = wk_spi1_transfer_byte(0xFFU);
    }
    sd_spi_deselect();

    if(ocr[2] == 0x01U && ocr[3] == 0xAAU)
    {
      for(index = 0U; index < 100U; ++index)
      {
        response = sd_spi_send_command(SD_ACMD41, 1UL << 30);
        sd_spi_deselect();
        if(response == 0x00U)
        {
          break;
        }
        wk_delay_ms(10U);
      }

      if(response == 0x00U && sd_spi_send_command(SD_CMD58, 0U) == 0x00U)
      {
        for(index = 0U; index < 4U; ++index)
        {
          ocr[index] = wk_spi1_transfer_byte(0xFFU);
        }
        sd_spi_deselect();
        g_sd_card_type = SD_CARD_TYPE_SD2;
        if((ocr[0] & 0x40U) != 0U)
        {
          g_sd_card_type |= SD_CARD_TYPE_BLOCK;
        }
      }
    }
  }
  else
  {
    uint8_t command;

    sd_spi_deselect();
    if(sd_spi_send_command(SD_ACMD41, 0U) <= 1U)
    {
      g_sd_card_type = SD_CARD_TYPE_SD1;
      command = SD_ACMD41;
    }
    else
    {
      g_sd_card_type = SD_CARD_TYPE_MMC;
      command = SD_CMD1;
    }
    sd_spi_deselect();

    for(index = 0U; index < 100U; ++index)
    {
      response = sd_spi_send_command(command, 0U);
      sd_spi_deselect();
      if(response == 0x00U)
      {
        break;
      }
      wk_delay_ms(10U);
    }

    if(response != 0x00U || sd_spi_send_command(SD_CMD16, SD_SPI_SECTOR_SIZE) != 0x00U)
    {
      sd_spi_deselect();
      g_sd_card_type = SD_CARD_TYPE_NONE;
      return 0U;
    }
    sd_spi_deselect();
  }

  if(g_sd_card_type == SD_CARD_TYPE_NONE)
  {
    return 0U;
  }

  if(!sd_spi_read_csd(csd) || !sd_spi_parse_sector_count(csd, &g_sd_sector_count))
  {
    g_sd_card_type = SD_CARD_TYPE_NONE;
    return 0U;
  }
  if(!sd_spi_read_cid(g_sd_cid))
  {
    memset(g_sd_cid, 0, sizeof(g_sd_cid));
  }

  wk_spi1_set_clock_div(SPI_MCLK_DIV_8);
  g_sd_initialized = 1U;
  return 1U;
}

uint8_t sd_spi_is_initialized(void)
{
  return g_sd_initialized;
}

uint8_t sd_spi_read_blocks(uint32_t sector, uint8_t *buffer, uint32_t count)
{
  uint32_t offset;
  uint32_t address;

  if(g_sd_initialized == 0U || buffer == NULL || count == 0U)
  {
    return 0U;
  }

  for(offset = 0U; offset < count; ++offset)
  {
    address = sector + offset;
    if((g_sd_card_type & SD_CARD_TYPE_BLOCK) == 0U)
    {
      address *= SD_SPI_SECTOR_SIZE;
    }

    if(sd_spi_send_command(SD_CMD17, address) != 0x00U)
    {
      sd_spi_deselect();
      return 0U;
    }
    if(!sd_spi_receive_data_block(&buffer[offset * SD_SPI_SECTOR_SIZE], SD_SPI_SECTOR_SIZE))
    {
      sd_spi_deselect();
      return 0U;
    }
    sd_spi_deselect();
  }

  return 1U;
}

uint8_t sd_spi_write_blocks(uint32_t sector, const uint8_t *buffer, uint32_t count)
{
  uint32_t offset;
  uint32_t address;

  if(g_sd_initialized == 0U || buffer == NULL || count == 0U)
  {
    return 0U;
  }

  for(offset = 0U; offset < count; ++offset)
  {
    address = sector + offset;
    if((g_sd_card_type & SD_CARD_TYPE_BLOCK) == 0U)
    {
      address *= SD_SPI_SECTOR_SIZE;
    }

    if(sd_spi_send_command(SD_CMD24, address) != 0x00U)
    {
      sd_spi_deselect();
      return 0U;
    }
    if(!sd_spi_transmit_data_block(&buffer[offset * SD_SPI_SECTOR_SIZE], SD_TOKEN_START_BLOCK))
    {
      sd_spi_deselect();
      return 0U;
    }
    sd_spi_deselect();
  }

  return 1U;
}

uint8_t sd_spi_get_sector_count(uint32_t *sector_count)
{
  if(g_sd_initialized == 0U || sector_count == NULL)
  {
    return 0U;
  }

  *sector_count = g_sd_sector_count;
  return 1U;
}

uint8_t sd_spi_get_sector_size(uint16_t *sector_size)
{
  if(g_sd_initialized == 0U || sector_size == NULL)
  {
    return 0U;
  }

  *sector_size = SD_SPI_SECTOR_SIZE;
  return 1U;
}

uint8_t sd_spi_get_erase_block_size(uint32_t *erase_block_size)
{
  if(g_sd_initialized == 0U || erase_block_size == NULL)
  {
    return 0U;
  }

  *erase_block_size = 1U;
  return 1U;
}

uint8_t sd_spi_get_card_info(sd_spi_card_info_t *card_info)
{
  uint16_t mdt_year;
  uint8_t mdt_month;

  if(g_sd_initialized == 0U || card_info == NULL)
  {
    return 0U;
  }

  memset(card_info, 0, sizeof(*card_info));
  card_info->manufacturer_id = g_sd_cid[0];
  card_info->oem_id[0] = (char)g_sd_cid[1];
  card_info->oem_id[1] = (char)g_sd_cid[2];
  card_info->oem_id[2] = '\0';
  memcpy(card_info->product_name, &g_sd_cid[3], 5U);
  card_info->product_name[5] = '\0';
  card_info->product_revision_major = (uint8_t)(g_sd_cid[8] >> 4);
  card_info->product_revision_minor = (uint8_t)(g_sd_cid[8] & 0x0FU);
  card_info->serial_number = ((uint32_t)g_sd_cid[9] << 24) |
                             ((uint32_t)g_sd_cid[10] << 16) |
                             ((uint32_t)g_sd_cid[11] << 8) |
                             (uint32_t)g_sd_cid[12];
  mdt_year = (uint16_t)(((uint16_t)(g_sd_cid[13] & 0x0FU) << 4) |
                        ((uint16_t)g_sd_cid[14] >> 4));
  mdt_month = (uint8_t)(g_sd_cid[14] & 0x0FU);
  card_info->manufacture_year = (uint16_t)(2000U + mdt_year);
  card_info->manufacture_month = mdt_month;
  card_info->sector_count = g_sd_sector_count;
  return 1U;
}

void sd_spi_sync(void)
{
  sd_spi_deselect();
}
