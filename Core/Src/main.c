
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"
#include "i2c.h"
#include "sdio.h"
#include "ssd1306.h"
#include "usart.h"
#include "sd_diskio.h"
#include "ffconf.h"

#include "stdio.h"
#include "string.h"
#include "stdlib.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

////////* raw GNSS stream *///////////////

#define UBX_BUF_SIZE 65535
#define OLED_ROWS 8
#define OLED_CHARS_PER_ROW 21
/*
 * Portable base safety option.
 *
 * When enabled, STM32 sends UBX-CFG-RST to the F9P at boot to clear retained
 * navigation state and force a fresh GNSS start. This helps prevent a movable
 * base from silently reusing stale survey/base state after being moved.
 *
 * Wiring required:
 *   STM32 PA9  / USART1 TX  --->  F9P UART1 RX
 *   STM32 PA10 / USART1 RX  <---  F9P UART1 TX
 *
 * The command does not erase the F9P flash configuration, but it does clear
 * retained navigation/orbit/time state. Survey-in will take longer after boot.
 */
#define F9P_COLD_START_ON_BOOT 0
uint8_t ubx_buf[UBX_BUF_SIZE];
volatile uint32_t overflow_counter = 0;
volatile uint32_t uart_dma_wrap_count = 0;
uint32_t log_bytes_written = 0;
uint32_t old_pos = 0;
uint32_t log_start_tick = 0;
uint8_t gnss_sat_count = 0;
uint8_t gnss_fix_type = 0;
uint8_t gnss_diff_solution = 0;
uint8_t gnss_time_valid = 0;
uint16_t gnss_year = 2015;
uint8_t gnss_month = 6;
uint8_t gnss_day = 4;
uint8_t gnss_hour = 0;
uint8_t gnss_min = 0;
uint8_t gnss_sec = 0;
uint32_t gnss_last_packet_tick = 0;
uint8_t gnss_svin_seen = 0;
uint8_t gnss_svin_active = 0;
uint8_t gnss_svin_valid = 0;
uint32_t gnss_svin_duration = 0;
uint32_t gnss_svin_mean_acc = 0;
static char oled_boot_lines[OLED_ROWS][OLED_CHARS_PER_ROW + 1];
static uint8_t f9p_tmode_fail_stage = 0;

typedef enum
{
  BASE_CFG_RESULT_OK = 0,
  BASE_CFG_RESULT_NOT_FOUND,
  BASE_CFG_RESULT_INVALID,
  BASE_CFG_RESULT_APPLY_FAIL
} BaseConfigResult;

typedef enum
{
  BASE_MODE_NONE = 0,
  BASE_MODE_SURVEY_IN,
  BASE_MODE_FIXED
} BaseMode;

static BaseMode active_base_mode = BASE_MODE_NONE;

typedef struct
{
  BaseMode mode;
  uint32_t svin_min_dur_s;
  uint32_t svin_acc_limit_01mm;
  int32_t fixed_lat_1e7;
  int32_t fixed_lon_1e7;
  int32_t mark_elev_cm;
  int32_t antenna_height_cm;
  int32_t fixed_height_cm;
  uint32_t fixed_acc_01mm;
  uint8_t has_lat;
  uint8_t has_lon;
  uint8_t has_mark_elev;
  uint8_t has_antenna_height;
} BaseConfig;

/////////////*  *////////////////////

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint32_t GNSS_DMA_GetWriteAbs(void)
{
  uint32_t wraps_a;
  uint32_t wraps_b;
  uint32_t pos;

  do
  {
    wraps_a = uart_dma_wrap_count;
    pos = UBX_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart1.hdmarx);
    wraps_b = uart_dma_wrap_count;
  } while (wraps_a != wraps_b);

  if (pos >= UBX_BUF_SIZE)
  {
    pos = 0;
  }

  return (wraps_a * UBX_BUF_SIZE) + pos;
}

static HAL_StatusTypeDef F9P_ColdStart(void)
{
#if F9P_COLD_START_ON_BOOT
  static const uint8_t cfg_rst_cold_start[] =
  {
      0xB5, 0x62,             /* UBX sync */
      0x06, 0x04,             /* CFG-RST */
      0x04, 0x00,             /* payload length */
      0xFF, 0xFF,             /* clear all nav BBR data */
      0x02,                   /* controlled GNSS-only reset */
      0x00,                   /* reserved */
      0x0E, 0x61              /* checksum */
  };

  return HAL_UART_Transmit(&huart1,
                           (uint8_t *)cfg_rst_cold_start,
                           sizeof(cfg_rst_cold_start),
                           100);
#else
  return HAL_OK;
#endif
}

static void PutU2(uint8_t *dst, uint16_t value)
{
  dst[0] = (uint8_t)(value & 0xFF);
  dst[1] = (uint8_t)((value >> 8) & 0xFF);
}

static void PutU4(uint8_t *dst, uint32_t value)
{
  dst[0] = (uint8_t)(value & 0xFF);
  dst[1] = (uint8_t)((value >> 8) & 0xFF);
  dst[2] = (uint8_t)((value >> 16) & 0xFF);
  dst[3] = (uint8_t)((value >> 24) & 0xFF);
}


static void ValsetPutKeyU1(uint8_t *payload, uint16_t *index, uint32_t key, uint8_t value)
{
  PutU4(&payload[*index], key);
  *index += 4;
  payload[*index] = value;
  *index += 1;
}

static void ValsetPutKeyU4(uint8_t *payload, uint16_t *index, uint32_t key, uint32_t value)
{
  PutU4(&payload[*index], key);
  *index += 4;
  PutU4(&payload[*index], value);
  *index += 4;
}

static void ValsetPutKeyI4(uint8_t *payload, uint16_t *index, uint32_t key, int32_t value)
{
  PutU4(&payload[*index], key);
  *index += 4;
  PutU4(&payload[*index], (uint32_t)value);
  *index += 4;
}

static void F9P_ClearRx(void)
{
  uint8_t dummy;

  __HAL_UART_CLEAR_OREFLAG(&huart1);
  __HAL_UART_CLEAR_FEFLAG(&huart1);
  __HAL_UART_CLEAR_NEFLAG(&huart1);

  while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE) != RESET)
  {
    dummy = (uint8_t)(huart1.Instance->DR & 0xFF);
    (void)dummy;
  }

  __HAL_UART_CLEAR_OREFLAG(&huart1);
  __HAL_UART_CLEAR_FEFLAG(&huart1);
  __HAL_UART_CLEAR_NEFLAG(&huart1);
}

static HAL_StatusTypeDef F9P_SendUbx(uint8_t msg_class,
                                     uint8_t msg_id,
                                     const uint8_t *payload,
                                     uint16_t payload_len)
{
  uint8_t frame[128];
  uint8_t ck_a = 0;
  uint8_t ck_b = 0;
  uint16_t frame_len = payload_len + 8;

  if (payload_len > 120)
  {
    return HAL_ERROR;
  }

  frame[0] = 0xB5;
  frame[1] = 0x62;
  frame[2] = msg_class;
  frame[3] = msg_id;
  frame[4] = (uint8_t)(payload_len & 0xFF);
  frame[5] = (uint8_t)((payload_len >> 8) & 0xFF);

  for (uint16_t i = 0; i < payload_len; i++)
  {
    frame[6 + i] = payload[i];
  }

  for (uint16_t i = 2; i < (uint16_t)(6 + payload_len); i++)
  {
    ck_a = (uint8_t)(ck_a + frame[i]);
    ck_b = (uint8_t)(ck_b + ck_a);
  }

  frame[6 + payload_len] = ck_a;
  frame[7 + payload_len] = ck_b;

  F9P_ClearRx();
  return HAL_UART_Transmit(&huart1, frame, frame_len, 250);
}

static int8_t F9P_WaitAck(uint8_t ack_class, uint8_t ack_id, uint32_t timeout_ms)
{
  uint8_t byte;
  uint8_t state = 0;
  uint8_t msg_class = 0;
  uint8_t msg_id = 0;
  uint16_t payload_len = 0;
  uint16_t payload_index = 0;
  uint8_t payload[2] = {0, 0};
  uint8_t ck_a = 0;
  uint8_t ck_b = 0;
  uint32_t start = HAL_GetTick();

  while ((HAL_GetTick() - start) < timeout_ms)
  {
    if (HAL_UART_Receive(&huart1, &byte, 1, 20) != HAL_OK)
    {
      F9P_ClearRx();
      state = 0;
      continue;
    }

    switch (state)
    {
      case 0:
        state = (byte == 0xB5) ? 1 : 0;
        break;

      case 1:
        state = (byte == 0x62) ? 2 : 0;
        break;

      case 2:
        msg_class = byte;
        ck_a = byte;
        ck_b = ck_a;
        state = 3;
        break;

      case 3:
        msg_id = byte;
        ck_a = (uint8_t)(ck_a + byte);
        ck_b = (uint8_t)(ck_b + ck_a);
        state = 4;
        break;

      case 4:
        payload_len = byte;
        ck_a = (uint8_t)(ck_a + byte);
        ck_b = (uint8_t)(ck_b + ck_a);
        state = 5;
        break;

      case 5:
        payload_len |= ((uint16_t)byte << 8);
        payload_index = 0;
        payload[0] = 0;
        payload[1] = 0;
        ck_a = (uint8_t)(ck_a + byte);
        ck_b = (uint8_t)(ck_b + ck_a);
        state = (payload_len == 0) ? 7 : 6;
        break;

      case 6:
        if (payload_index < 2)
        {
          payload[payload_index] = byte;
        }
        ck_a = (uint8_t)(ck_a + byte);
        ck_b = (uint8_t)(ck_b + ck_a);
        payload_index++;
        if (payload_index >= payload_len)
        {
          state = 7;
        }
        break;

      case 7:
        state = (byte == ck_a) ? 8 : 0;
        break;

      case 8:
        if ((byte == ck_b) &&
            (msg_class == 0x05) &&
            ((msg_id == 0x01) || (msg_id == 0x00)) &&
            (payload_len == 2) &&
            (payload[0] == ack_class) &&
            (payload[1] == ack_id))
        {
          return (msg_id == 0x01) ? 1 : -1;
        }
        state = 0;
        break;

      default:
        state = 0;
        break;
    }
  }

  return 0;
}

static HAL_StatusTypeDef F9P_SendUbxWaitAck(uint8_t msg_class,
                                            uint8_t msg_id,
                                            const uint8_t *payload,
                                            uint16_t payload_len)
{
  for (uint8_t attempt = 0; attempt < 3; attempt++)
  {
    if (F9P_SendUbx(msg_class, msg_id, payload, payload_len) == HAL_OK)
    {
      if (F9P_WaitAck(msg_class, msg_id, 2500) == 1)
      {
        return HAL_OK;
      }
    }
    HAL_Delay(300);
  }

  return HAL_ERROR;
}

static char *Trim(char *text)
{
  char *end;

  while ((*text == ' ') || (*text == '\t') || (*text == '\r') || (*text == '\n'))
  {
    text++;
  }

  end = text + strlen(text);
  while ((end > text) &&
         ((end[-1] == ' ') || (end[-1] == '\t') ||
          (end[-1] == '\r') || (end[-1] == '\n')))
  {
    end--;
  }
  *end = '\0';

  return text;
}

static char UpperAscii(char c)
{
  if ((c >= 'a') && (c <= 'z'))
  {
    return (char)(c - ('a' - 'A'));
  }

  return c;
}

static uint8_t StrEqIgnoreCase(const char *a, const char *b)
{
  while (*a && *b)
  {
    if (UpperAscii(*a) != UpperAscii(*b))
    {
      return 0;
    }
    a++;
    b++;
  }

  return (*a == '\0') && (*b == '\0');
}

static uint8_t ParseScaled(const char *text, int64_t scale, int64_t *out)
{
  int sign = 1;
  int64_t whole = 0;
  int64_t frac = 0;
  int64_t frac_scale = 1;
  uint8_t saw_digit = 0;
  const char *p = text;

  if (*p == '-')
  {
    sign = -1;
    p++;
  }
  else if (*p == '+')
  {
    p++;
  }

  while ((*p >= '0') && (*p <= '9'))
  {
    saw_digit = 1;
    whole = (whole * 10) + (*p - '0');
    p++;
  }

  if (*p == '.')
  {
    p++;
    while ((*p >= '0') && (*p <= '9') && (frac_scale < 1000000000LL))
    {
      saw_digit = 1;
      frac = (frac * 10) + (*p - '0');
      frac_scale *= 10;
      p++;
    }
  }

  if (!saw_digit)
  {
    return 0;
  }

  while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n'))
  {
    p++;
  }

  if (*p != '\0')
  {
    return 0;
  }

  *out = sign * ((whole * scale) + ((frac * scale + (frac_scale / 2)) / frac_scale));
  return 1;
}

static uint8_t ParseU32Value(const char *text, uint32_t *out)
{
  char *end = NULL;
  unsigned long value = strtoul(text, &end, 10);

  while (end && ((*end == ' ') || (*end == '\t') || (*end == '\r') || (*end == '\n')))
  {
    end++;
  }

  if ((end == text) || (end == NULL) || (*end != '\0'))
  {
    return 0;
  }

  *out = (uint32_t)value;
  return 1;
}

static void BaseConfig_Defaults(BaseConfig *cfg)
{
  memset(cfg, 0, sizeof(*cfg));
  cfg->mode = BASE_MODE_NONE;
  cfg->svin_min_dur_s = 600;
  cfg->svin_acc_limit_01mm = 7000;
  cfg->fixed_acc_01mm = 200;
}

static uint8_t BaseConfig_ParseLine(BaseConfig *cfg, char *line)
{
  char *comment = strchr(line, '#');
  char *equals;
  char *key;
  char *value;
  int64_t scaled;
  uint32_t seconds;

  if (comment != NULL)
  {
    *comment = '\0';
  }

  equals = strchr(line, '=');
  if (equals == NULL)
  {
    return (Trim(line)[0] == '\0') ? 1 : 0;
  }

  *equals = '\0';
  key = Trim(line);
  value = Trim(equals + 1);

  if (StrEqIgnoreCase(key, "MODE"))
  {
    if (StrEqIgnoreCase(value, "SURVEY") ||
        StrEqIgnoreCase(value, "SURVEY_IN") ||
        StrEqIgnoreCase(value, "SURVEY-IN"))
    {
      cfg->mode = BASE_MODE_SURVEY_IN;
      return 1;
    }

    if (StrEqIgnoreCase(value, "FIXED"))
    {
      cfg->mode = BASE_MODE_FIXED;
      return 1;
    }

    return 0;
  }

  if (StrEqIgnoreCase(key, "SVIN_MIN_DUR_S") ||
      StrEqIgnoreCase(key, "SURVEY_TIME_S") ||
      StrEqIgnoreCase(key, "TIME_S"))
  {
    if (!ParseU32Value(value, &seconds) || (seconds < 60) || (seconds > 86400))
    {
      return 0;
    }
    cfg->svin_min_dur_s = seconds;
    return 1;
  }

  if (StrEqIgnoreCase(key, "SVIN_ACC_M") ||
      StrEqIgnoreCase(key, "SURVEY_ACC_M") ||
      StrEqIgnoreCase(key, "ACC_M"))
  {
    if (!ParseScaled(value, 10000, &scaled) || (scaled < 100) || (scaled > 1000000))
    {
      return 0;
    }
    cfg->svin_acc_limit_01mm = (uint32_t)scaled;
    return 1;
  }

  if (StrEqIgnoreCase(key, "LAT") || StrEqIgnoreCase(key, "LAT_DEG"))
  {
    if (!ParseScaled(value, 10000000, &scaled) || (scaled < -900000000LL) || (scaled > 900000000LL))
    {
      return 0;
    }
    cfg->fixed_lat_1e7 = (int32_t)scaled;
    cfg->has_lat = 1;
    return 1;
  }

  if (StrEqIgnoreCase(key, "LON") || StrEqIgnoreCase(key, "LON_DEG"))
  {
    if (!ParseScaled(value, 10000000, &scaled) || (scaled < -1800000000LL) || (scaled > 1800000000LL))
    {
      return 0;
    }
    cfg->fixed_lon_1e7 = (int32_t)scaled;
    cfg->has_lon = 1;
    return 1;
  }

  if (StrEqIgnoreCase(key, "MARK_ELEV_M") || StrEqIgnoreCase(key, "MARK_ELEV"))
  {
    if (!ParseScaled(value, 100, &scaled) || (scaled < -1000000) || (scaled > 1000000))
    {
      return 0;
    }
    cfg->mark_elev_cm = (int32_t)scaled;
    cfg->has_mark_elev = 1;
    return 1;
  }

  if (StrEqIgnoreCase(key, "ANTENNA_HEIGHT_M") || StrEqIgnoreCase(key, "ANTENNA_HEIGHT"))
  {
    if (!ParseScaled(value, 100, &scaled) || (scaled < 0) || (scaled > 100000))
    {
      return 0;
    }
    cfg->antenna_height_cm = (int32_t)scaled;
    cfg->has_antenna_height = 1;
    return 1;
  }

  if (StrEqIgnoreCase(key, "FIXED_ACC_M"))
  {
    if (!ParseScaled(value, 10000, &scaled) || (scaled < 1) || (scaled > 1000000))
    {
      return 0;
    }
    cfg->fixed_acc_01mm = (uint32_t)scaled;
    return 1;
  }

  return 1;
}

static BaseConfigResult BaseConfig_Read(BaseConfig *cfg)
{
  FIL file;
  FRESULT res;
  char line[96];

  BaseConfig_Defaults(cfg);

  res = f_open(&file, "BASE.TXT", FA_READ);
  if (res == FR_NO_FILE)
  {
    return BASE_CFG_RESULT_NOT_FOUND;
  }

  if (res != FR_OK)
  {
    return BASE_CFG_RESULT_INVALID;
  }

  while (f_gets(line, sizeof(line), &file) != NULL)
  {
    if (!BaseConfig_ParseLine(cfg, line))
    {
      f_close(&file);
      return BASE_CFG_RESULT_INVALID;
    }
  }

  f_close(&file);

  if (cfg->mode == BASE_MODE_NONE)
  {
    return BASE_CFG_RESULT_INVALID;
  }

  if ((cfg->mode == BASE_MODE_FIXED) &&
      (!cfg->has_lat || !cfg->has_lon ||
       !cfg->has_mark_elev || !cfg->has_antenna_height))
  {
    return BASE_CFG_RESULT_INVALID;
  }

  if (cfg->mode == BASE_MODE_FIXED)
  {
    cfg->fixed_height_cm = cfg->mark_elev_cm + cfg->antenna_height_cm;
  }

  return BASE_CFG_RESULT_OK;
}

static HAL_StatusTypeDef F9P_SetTmode3(const BaseConfig *cfg)
{
  uint8_t payload[96];
  uint16_t index = 0;

  f9p_tmode_fail_stage = 0;

  memset(payload, 0, sizeof(payload));
  payload[0] = 0;      /* VALSET version */
  payload[1] = 0x01;   /* apply to RAM */
  payload[2] = 0;
  payload[3] = 0;
  index = 4;

  /* First disable TMODE. */
  ValsetPutKeyU1(payload, &index, 0x20030001UL, 0); /* CFG-TMODE-MODE = disabled */
  if (F9P_SendUbxWaitAck(0x06, 0x8A, payload, index) != HAL_OK)
  {
    f9p_tmode_fail_stage = 1;
    return HAL_ERROR;
  }

  memset(payload, 0, sizeof(payload));
  payload[0] = 0;
  payload[1] = 0x01;   /* RAM */
  payload[2] = 0;
  payload[3] = 0;
  index = 4;

  if (cfg->mode == BASE_MODE_SURVEY_IN)
  {
    ValsetPutKeyU1(payload, &index, 0x20030001UL, 1);                  /* MODE survey-in */
    ValsetPutKeyU4(payload, &index, 0x40030010UL, cfg->svin_min_dur_s);
    ValsetPutKeyU4(payload, &index, 0x40030011UL, cfg->svin_acc_limit_01mm);
  }
  else if (cfg->mode == BASE_MODE_FIXED)
  {
    ValsetPutKeyU1(payload, &index, 0x20030001UL, 2);                  /* MODE fixed */
    ValsetPutKeyU1(payload, &index, 0x20030002UL, 1);                  /* POS_TYPE LLH */
    ValsetPutKeyI4(payload, &index, 0x40030009UL, cfg->fixed_lat_1e7);  /* CFG-TMODE-LAT */
    ValsetPutKeyI4(payload, &index, 0x4003000AUL, cfg->fixed_lon_1e7);  /* CFG-TMODE-LON */
    ValsetPutKeyI4(payload, &index, 0x4003000BUL, cfg->fixed_height_cm); /* CFG-TMODE-HEIGHT */
    ValsetPutKeyU4(payload, &index, 0x4003000FUL, cfg->fixed_acc_01mm);  /* CFG-TMODE-FIXED_POS_ACC */
  }
  else
  {
    return HAL_ERROR;
  }

  if (F9P_SendUbxWaitAck(0x06, 0x8A, payload, index) != HAL_OK)
  {
    f9p_tmode_fail_stage = 2;
    return HAL_ERROR;
  }

  f9p_tmode_fail_stage = 0;
  return HAL_OK;
}

static BaseConfigResult BaseConfig_LoadAndApply(char *status, size_t status_size)
{
  BaseConfig cfg;
  BaseConfigResult result = BaseConfig_Read(&cfg);

  if (result == BASE_CFG_RESULT_NOT_FOUND)
  {
    snprintf(status, status_size, "BASE.TXT NONE");
    return result;
  }

  if (result != BASE_CFG_RESULT_OK)
  {
    snprintf(status, status_size, "BASE.TXT ERR");
    return result;
  }

  if (F9P_SetTmode3(&cfg) != HAL_OK)
  {
    if (f9p_tmode_fail_stage == 1)
    {
      snprintf(status, status_size, "TMODE OFF FAIL");
    }
    else if (f9p_tmode_fail_stage == 2)
    {
      snprintf(status, status_size, "TMODE SET FAIL");
    }
    else
    {
      snprintf(status, status_size, "TMODE3 FAIL");
    }
    return BASE_CFG_RESULT_APPLY_FAIL;
  }

  active_base_mode = cfg.mode;

  if (cfg.mode == BASE_MODE_SURVEY_IN)
  {
    snprintf(status, status_size, "VALSET SURV %lus", cfg.svin_min_dur_s);
  }
  else
  {
    snprintf(status, status_size, "VALSET FIXED SENT");
  }

  return BASE_CFG_RESULT_OK;
}

static FRESULT GNSS_OpenNextLogFile(FIL *file, char *name, UINT name_size)
{
  FILINFO info;
  FRESULT res;

  for (uint16_t index = 1; index <= 999; index++)
  {
    snprintf(name, name_size, "GNSS%03u.UBX", index);

    res = f_stat(name, &info);
    if (res == FR_NO_FILE)
    {
      return f_open(file, name, FA_CREATE_NEW | FA_WRITE);
    }

    if (res != FR_OK)
    {
      return res;
    }
  }

  return FR_DENIED;
}

static void GNSS_ParseByte(uint8_t byte)
{
  static uint8_t state = 0;
  static uint8_t msg_class = 0;
  static uint8_t msg_id = 0;
  static uint16_t payload_len = 0;
  static uint16_t payload_index = 0;
  static uint8_t ck_a = 0;
  static uint8_t ck_b = 0;
  static uint8_t nav_pvt_valid = 0;
  static uint16_t nav_pvt_year = 2015;
  static uint8_t nav_pvt_month = 6;
  static uint8_t nav_pvt_day = 4;
  static uint8_t nav_pvt_hour = 0;
  static uint8_t nav_pvt_min = 0;
  static uint8_t nav_pvt_sec = 0;
  static uint8_t nav_pvt_fix_type = 0;
  static uint8_t nav_pvt_diff_solution = 0;
  static uint8_t nav_pvt_num_sv = 0;
  static uint8_t nav_svin_active = 0;
  static uint8_t nav_svin_valid = 0;
  static uint32_t nav_svin_duration = 0;
  static uint32_t nav_svin_mean_acc = 0;

  switch (state)
  {
    case 0:
      state = (byte == 0xB5) ? 1 : 0;
      break;

    case 1:
      state = (byte == 0x62) ? 2 : 0;
      break;

    case 2:
      msg_class = byte;
      ck_a = byte;
      ck_b = ck_a;
      state = 3;
      break;

    case 3:
      msg_id = byte;
      ck_a = (ck_a + byte) & 0xFF;
      ck_b = (ck_b + ck_a) & 0xFF;
      state = 4;
      break;

    case 4:
      payload_len = byte;
      ck_a = (ck_a + byte) & 0xFF;
      ck_b = (ck_b + ck_a) & 0xFF;
      state = 5;
      break;

    case 5:
      payload_len |= ((uint16_t)byte << 8);
      payload_index = 0;
      nav_pvt_valid = 0;
      nav_pvt_year = 2015;
      nav_pvt_month = 6;
      nav_pvt_day = 4;
      nav_pvt_hour = 0;
      nav_pvt_min = 0;
      nav_pvt_sec = 0;
      nav_pvt_fix_type = 0;
      nav_pvt_diff_solution = 0;
      nav_pvt_num_sv = 0;
      nav_svin_active = 0;
      nav_svin_valid = 0;
      nav_svin_duration = 0;
      nav_svin_mean_acc = 0;
      ck_a = (ck_a + byte) & 0xFF;
      ck_b = (ck_b + ck_a) & 0xFF;
      state = (payload_len == 0) ? 7 : 6;
      break;

    case 6:
      if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_index == 4))
      {
        nav_pvt_year = byte;
      }

      if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_index == 5))
      {
        nav_pvt_year |= ((uint16_t)byte << 8);
      }

      if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_index == 6))
      {
        nav_pvt_month = byte;
      }

      if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_index == 7))
      {
        nav_pvt_day = byte;
      }

      if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_index == 8))
      {
        nav_pvt_hour = byte;
      }

      if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_index == 9))
      {
        nav_pvt_min = byte;
      }

      if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_index == 10))
      {
        nav_pvt_sec = byte;
      }

      if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_index == 11))
      {
        nav_pvt_valid = byte;
      }

      if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_index == 20))
      {
        nav_pvt_fix_type = byte;
      }

      if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_index == 21))
      {
        nav_pvt_diff_solution = (byte & 0x02) ? 1 : 0;
      }

      if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_index == 23))
      {
        nav_pvt_num_sv = byte;
      }

      if ((msg_class == 0x01) && (msg_id == 0x3B) &&
          (payload_index >= 8) && (payload_index <= 11))
      {
        nav_svin_duration |= ((uint32_t)byte << (8U * (payload_index - 8)));
      }

      if ((msg_class == 0x01) && (msg_id == 0x3B) &&
          (payload_index >= 28) && (payload_index <= 31))
      {
        nav_svin_mean_acc |= ((uint32_t)byte << (8U * (payload_index - 28)));
      }

      if ((msg_class == 0x01) && (msg_id == 0x3B) && (payload_index == 36))
      {
        nav_svin_valid = byte ? 1 : 0;
      }

      if ((msg_class == 0x01) && (msg_id == 0x3B) && (payload_index == 37))
      {
        nav_svin_active = byte ? 1 : 0;
      }

      ck_a = (ck_a + byte) & 0xFF;
      ck_b = (ck_b + ck_a) & 0xFF;
      payload_index++;

      if (payload_index >= payload_len)
      {
        state = 7;
      }
      break;

    case 7:
      state = (byte == ck_a) ? 8 : 0;
      break;

    case 8:
      if (byte == ck_b)
      {
        if ((msg_class == 0x01) && (msg_id == 0x07) && (payload_len >= 24))
        {
          gnss_time_valid = ((nav_pvt_valid & 0x03) == 0x03) ? 1 : 0;
          gnss_year = nav_pvt_year;
          gnss_month = nav_pvt_month;
          gnss_day = nav_pvt_day;
          gnss_hour = nav_pvt_hour;
          gnss_min = nav_pvt_min;
          gnss_sec = nav_pvt_sec;
          gnss_fix_type = nav_pvt_fix_type;
          gnss_diff_solution = nav_pvt_diff_solution;
          gnss_sat_count = nav_pvt_num_sv;
          gnss_last_packet_tick = HAL_GetTick();
        }

        if ((msg_class == 0x01) && (msg_id == 0x3B) && (payload_len >= 38))
        {
          gnss_svin_seen = 1;
          gnss_svin_active = nav_svin_active;
          gnss_svin_valid = nav_svin_valid;
          gnss_svin_duration = nav_svin_duration;
          gnss_svin_mean_acc = nav_svin_mean_acc;
        }
      }
      state = 0;
      break;

    default:
      state = 0;
      break;
  }
}

static void GNSS_ParseBytes(const uint8_t *data, uint32_t len)
{
  for (uint32_t i = 0; i < len; i++)
  {
    GNSS_ParseByte(data[i]);
  }
}

static const char *GNSS_FixLabel(void)
{
  if (gnss_diff_solution)
  {
    return "DGPS";
  }

  switch (gnss_fix_type)
  {
    case 2:
      return "2D";
    case 3:
      return "3D";
    case 4:
      return "GNSS+DR";
    case 5:
      return "TIME";
    default:
      return "NOFIX";
  }
}

static uint8_t GNSS_FixIsPpkOk(void)
{
  return (gnss_diff_solution || (gnss_fix_type == 3) || (gnss_fix_type == 5));
}

static const char *GNSS_SvinLabel(void)
{
  if (active_base_mode == BASE_MODE_FIXED)
  {
    return "FIXED BASE";
  }

  if (!gnss_svin_seen)
  {
    if ((active_base_mode == BASE_MODE_SURVEY_IN) || (gnss_fix_type == 5))
    {
      return "SURVEYING IN";
    }

    return "";
  }

  if (gnss_svin_valid)
  {
    return "SURVEY IN OK";
  }

  if (gnss_svin_active)
  {
    return "SURVEYING IN";
  }

  return "SURVEYING IN";
}

DWORD get_fattime(void)
{
  uint16_t year = gnss_year;
  uint8_t month = gnss_month;
  uint8_t day = gnss_day;
  uint8_t hour = gnss_hour;
  uint8_t min = gnss_min;
  uint8_t sec = gnss_sec;

  if (!gnss_time_valid || (year < 1980) || (year > 2107) ||
      (month < 1) || (month > 12) || (day < 1) || (day > 31))
  {
    year = 2015;
    month = 6;
    day = 4;
    hour = 0;
    min = 0;
    sec = 0;
  }

  return ((DWORD)(year - 1980) << 25) |
         ((DWORD)month << 21) |
         ((DWORD)day << 16) |
         ((DWORD)hour << 11) |
         ((DWORD)min << 5) |
         ((DWORD)(sec / 2));
}

static void OLED_BootSetLine(uint8_t row, const char *text)
{
  if (row >= OLED_ROWS)
  {
    return;
  }

  snprintf(oled_boot_lines[row], sizeof(oled_boot_lines[row]), "%s", text);

  SSD1306_Clear();
  for (uint8_t i = 0; i < OLED_ROWS; i++)
  {
    if (oled_boot_lines[i][0] != '\0')
    {
      SSD1306_SetCursor(0, i);
      SSD1306_WriteString(oled_boot_lines[i]);
    }
  }
  SSD1306_UpdateScreen();
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  MX_GPIO_Init();
  MX_I2C1_Init();

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */
  char msg[32];

  HAL_Delay(100);
  SSD1306_Init();
  OLED_BootSetLine(0, "BOOT");
  HAL_Delay(500);

  OLED_BootSetLine(1, "UART CFG");
  HAL_Delay(500);

  if (F9P_ColdStart() == HAL_OK)
  {
      OLED_BootSetLine(1, "UART OK");
  }
  else
  {
      OLED_BootSetLine(1, "F9P RST FAIL");
  }
  HAL_Delay(5000);

  /* INIT SD + FATFS (DO NOT CALL HAL_SD OR BSP_SD HERE) */
  MX_SDIO_SD_Init();

  OLED_BootSetLine(2, "SDIO CFG");
  HAL_Delay(200);

  MX_FATFS_Init();

  OLED_BootSetLine(3, "FATFS OK");
  HAL_Delay(500);

  /* -------- MOUNT -------- */

  FRESULT res = f_mount(&SDFatFS, SDPath, 1);
  DSTATUS ds = disk_status(0);

  if ((res == FR_OK) && (ds == 0))
  {
      sprintf(msg, "MNT=OK SD=OK");
  }
  else
  {
      sprintf(msg, "MNT=%d DS=%u", res, ds);
  }
  OLED_BootSetLine(4, msg);
  HAL_Delay(1000);

  if (res != FR_OK)
  {
      SSD1306_Clear();
      sprintf(msg, "MFAIL D=%u", ds);
      SSD1306_WriteString(msg);
      SSD1306_UpdateScreen();
      while (1);
  }

  /* -------- FREE SPACE -------- */

  DWORD free_clusters;
  FATFS *fs;

  res = f_getfree(SDPath, &free_clusters, &fs);

  SSD1306_Clear();
  if (res == FR_OK)
  {
      DWORD free_sectors = free_clusters * fs->csize;

      uint32_t free_mb = free_sectors / 2048UL;

      sprintf(msg, "FREE=%luMB", free_mb);
  }
  else
  {
      sprintf(msg, "FREE FAIL");
  }

  OLED_BootSetLine(5, msg);
  HAL_Delay(1500);

  char base_status[OLED_CHARS_PER_ROW + 1];
  BaseConfigResult base_result = BaseConfig_LoadAndApply(base_status, sizeof(base_status));
  OLED_BootSetLine(5, base_status);
  HAL_Delay(1500);

  if ((base_result == BASE_CFG_RESULT_INVALID) ||
      (base_result == BASE_CFG_RESULT_APPLY_FAIL))
  {
      SSD1306_Clear();
      SSD1306_WriteString(base_status);
      SSD1306_UpdateScreen();
      while (1);
  }

  /* -------- OPEN FILE -------- */

  FIL log_file;
  UINT bw;
  char log_name[13];

  res = GNSS_OpenNextLogFile(&log_file, log_name, sizeof(log_name));

  if (res == FR_OK)
  {
      sprintf(msg, "OPEN=OK");
  }
  else
  {
      sprintf(msg, "OPEN=%d", res);
  }
  OLED_BootSetLine(6, msg);
  HAL_Delay(1000);

  if (res != FR_OK)
  {
      SSD1306_Clear();
      SSD1306_WriteString("OPEN FAIL");
      SSD1306_UpdateScreen();
      while (1);
  }

  OLED_BootSetLine(7, log_name);
  HAL_Delay(1000);

  res = f_sync(&log_file);

  if (res == FR_OK)
  {
      sprintf(msg, "OPEN=OK SYNC=OK");
  }
  else
  {
      sprintf(msg, "OPEN=OK SYNC=%d", res);
  }
  OLED_BootSetLine(6, msg);
  HAL_Delay(1000);

  if (res != FR_OK)
  {
      SSD1306_Clear();
      SSD1306_WriteString("SYNC FAIL");
      SSD1306_UpdateScreen();
      f_close(&log_file);
      while (1);
  }

  uart_dma_wrap_count = 0;
  overflow_counter = 0;
  log_bytes_written = 0;
  old_pos = 0;

  if (HAL_UART_Receive_DMA(&huart1, ubx_buf, UBX_BUF_SIZE) != HAL_OK)
  {
      SSD1306_Clear();
      SSD1306_WriteString("UART DMA FAIL");
      SSD1306_UpdateScreen();
      f_close(&log_file);
      while (1);
  }
  log_start_tick = HAL_GetTick();
  OLED_BootSetLine(1, "UART RX OK");
  HAL_Delay(500);

  /* -------- DONE -------- */

  sprintf(msg, "%s READY", log_name);
  OLED_BootSetLine(7, msg);
  HAL_Delay(1000);

  /* USER CODE END 2 */


  ///////////////////////* Infinite loop *//////////////////////////

  old_pos = 0;

  while (1)
  {
      uint32_t write_abs = GNSS_DMA_GetWriteAbs();
      uint32_t available = write_abs - old_pos;

      if (available > UBX_BUF_SIZE)
      {
          overflow_counter++;
          old_pos = write_abs;
          SSD1306_Clear();
          sprintf(msg, "OVR=%lu", overflow_counter);
          SSD1306_WriteString(msg);
          SSD1306_UpdateScreen();
          continue;
      }

      while (available > 0)
      {
          uint32_t read_pos = old_pos % UBX_BUF_SIZE;
          uint32_t chunk = UBX_BUF_SIZE - read_pos;

          if (chunk > available)
          {
              chunk = available;
          }

          GNSS_ParseBytes(&ubx_buf[read_pos], chunk);
          res = f_write(&log_file, &ubx_buf[read_pos], chunk, &bw);

          if ((res != FR_OK) || (bw != chunk))
          {
              SSD1306_Clear();
              sprintf(msg, "LOG ERR=%d", res);
              SSD1306_WriteString(msg);
              SSD1306_UpdateScreen();
              f_close(&log_file);
              while (1);
          }

          old_pos += bw;
          log_bytes_written += bw;
          write_abs = GNSS_DMA_GetWriteAbs();
          available = write_abs - old_pos;

          if (available > UBX_BUF_SIZE)
          {
              overflow_counter++;
              old_pos = write_abs;
              break;
          }
      }

      static uint32_t last_sync = 0;
      if (HAL_GetTick() - last_sync > 5000)
      {
          res = f_sync(&log_file);
          last_sync = HAL_GetTick();

          if (res != FR_OK)
          {
              SSD1306_Clear();
              sprintf(msg, "SYNC ERR=%d", res);
              SSD1306_WriteString(msg);
              SSD1306_UpdateScreen();
              f_close(&log_file);
              while (1);
          }
      }

      static uint32_t last_oled = 0;
      if (HAL_GetTick() - last_oled > 1000)
      {
          static uint8_t spinner_index = 0;
          const char spinner[] = ".-*+";

          SSD1306_Clear();
          SSD1306_SetCursor(0, 0);
          sprintf(msg, "SD WRITE %luKB %s",
                  log_bytes_written / 1024UL,
                  (overflow_counter == 0) ? "OK" : "BAD");
          SSD1306_WriteString(msg);
          if ((overflow_counter > 0) && ((spinner_index & 0x01) == 0))
          {
              SSD1306_SetCursor(0, 1);
              SSD1306_WriteString("WARNING OVERRUN");
          }
          else if (((gnss_last_packet_tick == 0) ||
                    ((HAL_GetTick() - gnss_last_packet_tick) > 3000)) &&
                   ((spinner_index & 0x01) == 0))
          {
              SSD1306_SetCursor(0, 1);
              SSD1306_WriteString("NO GNSS DATA");
          }
          SSD1306_SetCursor(0, 2);
          SSD1306_WriteString(log_name);
          SSD1306_SetCursor(0, 4);
          uint32_t elapsed = (HAL_GetTick() - log_start_tick) / 1000UL;
          sprintf(msg, "LOGGING %c%c %02lu:%02lu",
                  spinner[spinner_index],
                  spinner[spinner_index],
                  elapsed / 60UL,
                  elapsed % 60UL);
          SSD1306_WriteString(msg);
          SSD1306_SetCursor(0, 6);
          if (GNSS_SvinLabel()[0] != '\0' && gnss_svin_valid)
          {
              snprintf(msg, sizeof(msg), "SAT=%u %s",
                       gnss_sat_count, GNSS_SvinLabel());
              SSD1306_WriteString(msg);
          }
          else if (GNSS_SvinLabel()[0] == '\0')
          {
              snprintf(msg, sizeof(msg), "SAT=%u", gnss_sat_count);
              SSD1306_WriteString(msg);
          }
          else if ((spinner_index & 0x01) == 0)
          {
              snprintf(msg, sizeof(msg), "SAT=%u %s",
                       gnss_sat_count, GNSS_SvinLabel());
              SSD1306_WriteString(msg);
          }
          else
          {
              snprintf(msg, sizeof(msg), "SAT=%u", gnss_sat_count);
              SSD1306_WriteString(msg);
          }
          SSD1306_SetCursor(0, 7);
          if (GNSS_FixIsPpkOk() || ((spinner_index & 0x01) == 0))
          {
              SSD1306_WriteString(GNSS_FixLabel());
          }
          if (gnss_time_valid)
          {
              sprintf(msg, "UTC=%02u:%02u:%02u", gnss_hour, gnss_min, gnss_sec);
          }
          else
          {
              sprintf(msg, "UTC=--:--:--");
          }
          SSD1306_SetCursor(SSD1306_WIDTH - (strlen(msg) * 6), 7);
          SSD1306_WriteString(msg);
          SSD1306_UpdateScreen();
          spinner_index = (spinner_index + 1) % 4;
          last_oled = HAL_GetTick();
      }
  }
}
    /* USER CODE BEGIN 3 */

  /* USER CODE END 3 */


/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */


/**
  * @brief SDIO Initialization Function
  * @param None
  * @retval None
  */

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */


/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
  /* DMA2_Stream3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
  /* DMA2_Stream6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream6_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */

}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    uart_dma_wrap_count++;
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */

void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
