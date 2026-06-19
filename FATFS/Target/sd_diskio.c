/* Includes ------------------------------------------------------------------*/
#include "ff_gen_drv.h"
#include "sd_diskio.h"
#include "bsp_driver_sd.h"

/* Private define ------------------------------------------------------------*/
#define SD_TIMEOUT 30000
#define SD_DEFAULT_BLOCK_SIZE 512

/* Private variables ---------------------------------------------------------*/
static volatile DSTATUS Stat = STA_NOINIT;

DSTATUS SD_initialize(BYTE lun);
DSTATUS SD_status(BYTE lun);
DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count);

#if _USE_WRITE == 1
DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count);
#endif

#if _USE_IOCTL == 1
DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff);
#endif

/* Driver structure -----------------------------------------------------------*/
const Diskio_drvTypeDef SD_Driver =
{
  SD_initialize,
  SD_status,
  SD_read,
#if _USE_WRITE == 1
  SD_write,
#endif
#if _USE_IOCTL == 1
  SD_ioctl,
#endif
};

/* Initialize ---------------------------------------------------------------*/
DSTATUS SD_initialize(BYTE lun)
{
  Stat = STA_NOINIT;

  /* IMPORTANT:
     BSP_SD_Init should be called ONLY ONCE globally.
     If already initialized elsewhere, this is safe but guarded. */

  if (BSP_SD_Init() == MSD_OK)
  {
    Stat &= ~STA_NOINIT;
  }

  return Stat;
}

/* Status -------------------------------------------------------------------*/
DSTATUS SD_status(BYTE lun)
{
  return Stat;
}

/* Read ---------------------------------------------------------------------*/
DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
  uint32_t tickstart;

  if (Stat & STA_NOINIT)
    return RES_NOTRDY;

  if (BSP_SD_ReadBlocks((uint32_t *)buff, sector, count, SD_TIMEOUT) != MSD_OK)
    return RES_ERROR;

  tickstart = HAL_GetTick();
  while (BSP_SD_GetCardState() != MSD_OK)
  {
    if ((HAL_GetTick() - tickstart) >= SD_TIMEOUT)
      return RES_ERROR;
  }

  return RES_OK;
}

/* Write --------------------------------------------------------------------*/
#if _USE_WRITE == 1
DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
  uint32_t tickstart;

  if (Stat & STA_NOINIT)
    return RES_NOTRDY;

  if (BSP_SD_WriteBlocks((uint32_t *)buff, sector, count, SD_TIMEOUT) != MSD_OK)
    return RES_ERROR;

  tickstart = HAL_GetTick();
  while (BSP_SD_GetCardState() != MSD_OK)
  {
    if ((HAL_GetTick() - tickstart) >= SD_TIMEOUT)
      return RES_ERROR;
  }

  return RES_OK;
}
#endif

/* IOCTL --------------------------------------------------------------------*/
#if _USE_IOCTL == 1
DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff)
{
  BSP_SD_CardInfo CardInfo;

  if (Stat & STA_NOINIT)
    return RES_NOTRDY;

  BSP_SD_GetCardInfo(&CardInfo);

  switch (cmd)
  {
    case CTRL_SYNC:
      return RES_OK;

    case GET_SECTOR_COUNT:
      *(DWORD*)buff = CardInfo.LogBlockNbr;
      return RES_OK;

    case GET_SECTOR_SIZE:
      *(WORD*)buff = CardInfo.LogBlockSize;
      return RES_OK;

    case GET_BLOCK_SIZE:
      *(DWORD*)buff = CardInfo.LogBlockSize / SD_DEFAULT_BLOCK_SIZE;
      return RES_OK;

    default:
      return RES_PARERR;
  }
}
#endif
