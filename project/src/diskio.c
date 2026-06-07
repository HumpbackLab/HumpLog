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

#include "diskio.h"
#include "sd_spi.h"

#define SD_DRIVE_NUMBER 0U

DSTATUS disk_initialize(BYTE pdrv)
{
  if(pdrv != SD_DRIVE_NUMBER)
  {
    return STA_NOINIT;
  }

  return sd_spi_initialize() != 0U ? 0U : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
  if(pdrv != SD_DRIVE_NUMBER)
  {
    return STA_NOINIT;
  }

  return sd_spi_is_initialized() != 0U ? 0U : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
  if(pdrv != SD_DRIVE_NUMBER || buff == 0 || count == 0U)
  {
    return RES_PARERR;
  }

  if(sd_spi_read_blocks((uint32_t)sector, buff, count) == 0U)
  {
    return RES_ERROR;
  }

  return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
  if(pdrv != SD_DRIVE_NUMBER || buff == 0 || count == 0U)
  {
    return RES_PARERR;
  }

  if(sd_spi_write_blocks((uint32_t)sector, buff, count) == 0U)
  {
    return RES_ERROR;
  }

  return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  uint32_t sector_count;
  uint16_t sector_size;
  uint32_t erase_block_size;

  if(pdrv != SD_DRIVE_NUMBER)
  {
    return RES_PARERR;
  }

  switch(cmd)
  {
    case CTRL_SYNC:
      sd_spi_sync();
      return RES_OK;

    case GET_SECTOR_COUNT:
      if(buff == 0 || sd_spi_get_sector_count(&sector_count) == 0U)
      {
        return RES_ERROR;
      }
      *(DWORD *)buff = sector_count;
      return RES_OK;

    case GET_SECTOR_SIZE:
      if(buff == 0 || sd_spi_get_sector_size(&sector_size) == 0U)
      {
        return RES_ERROR;
      }
      *(WORD *)buff = sector_size;
      return RES_OK;

    case GET_BLOCK_SIZE:
      if(buff == 0 || sd_spi_get_erase_block_size(&erase_block_size) == 0U)
      {
        return RES_ERROR;
      }
      *(DWORD *)buff = erase_block_size;
      return RES_OK;

    default:
      return RES_PARERR;
  }
}
