# Firmware Overview

## Main Firmware Pieces

- `main.c`: OLED boot/status flow, GNSS logging loop, UBX NAV-PVT/NAV-SVIN parser, file naming, F9P cold-start command, optional button support.
- `sdio.c`: SDIO peripheral configuration.
- `sd_diskio.c`: FatFS disk driver bridge to BSP SD calls.
- `ssd1306.c`: OLED display driver.
- `usart.c`: UART configuration for F9P receive/transmit at 460800 baud.

## Logging Flow

1. Initialize MCU, OLED, UART, SDIO, and FatFS.
2. Send a UBX cold-start command to the F9P over USART1 TX.
3. Mount SD card.
4. Find the next unused `GNSSxxx.UBX` file name.
5. Open the file.
6. Start UART DMA circular receive.
7. Continuously write new DMA bytes to SD.
8. Periodically `f_sync()` the file.
9. Update OLED with write status, fix status, satellite count, survey-in state, UTC, and warnings.

## F9P Cold Start

The firmware sends `UBX-CFG-RST` at boot when `F9P_COLD_START_ON_BOOT` is enabled. This clears retained navigation state and performs a controlled GNSS-only reset. It does not erase the F9P flash configuration.

This feature is intended for portable base use, where the base station may move between sessions and should not silently reuse stale retained survey/base state.

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
