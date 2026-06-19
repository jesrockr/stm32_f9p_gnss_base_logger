# STM32 F9P GNSS Data Logger

Low-cost STM32-based raw GNSS datalogger for u-blox ZED-F9P receivers. The logger records a raw UBX stream to an SD card for later PPK processing and transmits RTCM corrections for RTK FIX with rover, while a small OLED displays live write status, file name, fix state, satellite count, UTC time, and various warnings.


## What It Does

- Logs the incoming F9P UBX byte stream directly to SD card.
- Creates incrementing files such as `GNSS001.UBX`, `GNSS002.UBX`, and so on.
- Uses UART DMA circular buffering to reduce packet loss risk.
- Displays SD write status and overrun warnings on OLED.
- Parses `UBX-NAV-PVT` passively for `UTC time`, `fix type`, and `satellite count` live display.
- Uses GNSS `UTC time` for FatFS file timestamps once valid time is available.

## Hardware Used:

Listings may change, and equivalent boards/modules should work if the pinout, voltage levels, and interfaces match.




## STM32F407ZGT6 Board
cost: $14.60

<img src="assets/stm32-board-front.jpg" alt="STM32" width="200">


Purchase link: [AliExpress STM32F407ZGT6 board](https://www.aliexpress.us/item/3256809863003361.html?spm=a2g0o.order_list.order_list_main.41.49c11802SIBgZa&gatewayAdapt=glo2usa)




## SSD1306 OLED
cost: $2.60

<img src="assets/ssd1306-oled.jpg" alt="SSD1306 OLED module" width="200">

Purchase link: [AliExpress SSD1306 OLED](https://www.aliexpress.us/item/3256805954920554.html?spm=a2g0o.order_list.order_list_main.29.49c11802SIBgZa&gatewayAdapt=glo2usa)




## ZED-F9P-01B-01 GNSS Module
cost: $103.62

<img src="assets/zed-f9p-module.jpg" alt="ZED-F9P module" width="200">

Purchase link: [AliExpress ZED-F9P module](https://www.aliexpress.us/item/3256806049727804.html?spm=a2g0o.order_list.order_list_main.131.49c11802SIBgZa&gatewayAdapt=glo2usa)

## Prerequisites

Install the STM32 development tools from STMicroelectronics:

- STM32CubeMX
- STM32CubeIDE
- STM32CubeProgrammer

Download them from the official ST website:

https://www.st.com/en/development-tools/stm32-software-development-tools.html

Install u-blox u-center for configuring and testing the ZED-F9P:

- u-center GNSS evaluation software

Download from u-blox:

https://www.u-blox.com/en/product/u-center

Recommended workflow:

- Use STM32CubeMX to inspect or regenerate peripheral configuration.
- Use STM32CubeIDE to build/import the firmware project.
- Use STM32CubeProgrammer to flash the compiled firmware to the STM32 board.
- `NOTE: BOOT0 jumper must be soldered in order to flash board (via usb type-c), then unsoldered to run program. Recommend install of a switch or two wires to simplify multiple flashes of board. Alternatively, an ST-Link V2 module can be used to flash the board through the debug pins "DIO, CLK, GND, 3v3"`
- Use FAT32 formatted SD card inserted into onboard STM32 slot.
- Use u-center to configure the F9P output messages and verify `.UBX` log playback.

## QUICK START

- [Build and Flash](docs/BUILD_AND_FLASH.md)
- [F9P Configuration](docs/F9P_CONFIGURATION.md)

## Recommended F9P Output Messages

For PPK logging, enable on `UART 1` (or whichever F9P `PORT` is connected to STM32 `Rx` pin):

- `UBX-RXM-RAWX`
- `UBX-RXM-SFRBX`
- `UBX-NAV-PVT` at 1 Hz
- `UBX-NAV-SVIN` at 1 Hz

Optional:

- `UBX-NAV-SAT` at 1 Hz for richer satellite diagnostics

Disable unnecessary `NMEA` and high-rate navigation messages unless you have confirmed the UART and SD write pipeline have enough bandwidth.


## OLED Logging Screen

Typical boot up / logging screen:

<img src="assets/boot screen.jpg" alt="boot screen" width="200">

<img src="assets/logging screen.jpg" alt="logging screen" width="200">

Warnings:

- `WARNING OVERRUN`: UART receive/write pipeline fell behind. The log may have dropped bytes.
- `NO GNSS DATA`: no recent valid `NAV-PVT` packet has been parsed.
- Fix type flashes if the solution is not acceptable for PPK.
- `SURVEYING IN` flashes until base position is `SURVEY IN OK`.
- `TIME` is the standard fix type once position is surveyed-in.
- If `SAT=0` and `NO FIX`, check antenna.


## Important Reliability Notes

- For PPK, dropped bytes matter. Watch `WARNING OVERRUN`.
- Use a good SD card and avoid removing power before writes are synced.
- Keep unnecessary f9p receiver messages disabled. We want bandwidth to remain as lean as possible.
- Confirm logged `.UBX` files open correctly in u-center before relying on field data.
- Long-duration testing is strongly recommended before survey use.

## TMODE3 (BASE COORDINATE CONFIGURATION)

You are able to set this base up on known coordinates (`FIXED`, more repeatable but requires known LAT/LONG/ELEV and ANTENNA HEIGHT) or `SURVEY-IN` your position (requires you to set minimum observation time and minimum accuracy in meters). These can be changed in Ucenter under VIEW-CONFIGURATION VIEW-TMODE3. 
You are also able to change TMODE3 by creating a BASE.TXT file on the SD card in the STM32, with the following text format:

## Survey-In (add this to BASE.TXT on sd card)
```
MODE=SURVEY_IN

SVIN_MIN_DUR_S=600

SVIN_ACC_M=0.7
```
### OR
## Fixed (add this to BASE.TXT on sd card)
```
MODE=FIXED

LAT=37.1234568

LON=-121.1234568

MARK_ELEV_M=100.000

ANTENNA_HEIGHT_M=2.000

FIXED_ACC_M=0.020
```
The STM32 will read this textfile upon boot and send a command to the F9P.
