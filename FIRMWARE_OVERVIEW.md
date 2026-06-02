# Firmware Overview

## Main Firmware Pieces

- `main.c`: OLED boot/status flow, GNSS logging loop, UBX NAV-PVT parser, file naming, optional button support.
- `sdio.c`: SDIO peripheral configuration.
- `sd_diskio.c`: FatFS disk driver bridge to BSP SD calls.
- `ssd1306.c`: OLED display driver.
- `usart.c`: UART configuration for F9P receive.

## Logging Flow

1. Initialize MCU, OLED, UART, SDIO, and FatFS.
2. Mount SD card.
3. Find the next unused `GNSSxxx.UBX` file name.
4. Open the file.
5. Start UART DMA circular receive.
6. Continuously write new DMA bytes to SD.
7. Periodically `f_sync()` the file.
8. Update OLED with write status, fix status, satellite count, UTC, and warnings.

## Overrun Detection

The firmware tracks the absolute DMA write position. If the writer falls more than one circular buffer behind the receiver, `overflow_counter` increments and the OLED displays `WARNING OVERRUN`.

For PPK use, any overrun should be treated seriously because bytes may have been dropped from the raw UBX stream.

## File Naming

The logger searches for the first unused file:

```text
GNSS001.UBX
GNSS002.UBX
GNSS003.UBX
...
```

This allows multiple base sessions on the same SD card without overwriting previous logs.

