# STM32 F9P GNSS Data Logger

Low-cost STM32-based raw GNSS datalogger for u-blox ZED-F9P receivers. The logger records a raw UBX stream to an SD card for later PPK processing and transmits RTCM corrections for RTK FIX with rover, while a small OLED displays live write status, file name, fix state, satellite count, UTC time, and various warnings.


## What It Does

- Logs the incoming F9P UBX byte stream directly to SD card.
- Creates incrementing files such as `GNSS001.UBX`, `GNSS002.UBX`, and so on.
- Uses UART DMA circular buffering to reduce packet loss risk.
- Displays SD write status and overrun warnings on OLED.
- Parses `UBX-NAV-PVT` passively for `UTC time`, `fix type`, and `satellite count` live display.
- Uses GNSS `UTC time` for FatFS file timestamps once valid time is available.
- Appears as USB Storage device on Windows through STM32 usb type c port.
  
## Hardware Used:


## STM32F407ZGT6 Board
[cost: $14.60](https://www.aliexpress.us/item/3256809863003361.html?spm=a2g0o.order_list.order_list_main.41.49c11802SIBgZa&gatewayAdapt=glo2usa)

<img src="assets/stm32-board-front.jpg" alt="STM32" width="200">






## SSD1306 OLED
[cost: $2.60](https://www.aliexpress.us/item/3256805954920554.html?spm=a2g0o.order_list.order_list_main.29.49c11802SIBgZa&gatewayAdapt=glo2usa)

<img src="assets/ssd1306-oled.jpg" alt="SSD1306 OLED module" width="200">






## ZED-F9P-01B-01 GNSS 
[cost: $103.62](https://www.aliexpress.us/item/3256806049727804.html?spm=a2g0o.order_list.order_list_main.131.49c11802SIBgZa&gatewayAdapt=glo2usa)(https://www.aliexpress.us/item/3256806049727804.html?spm=a2g0o.order_list.order_list_main.131.49c11802SIBgZa&gatewayAdapt=glo2usa)

<img src="assets/zed-f9p-module.jpg" alt="ZED-F9P module" width="200">



## Holybro Sik Radio (optional)

[cost: $89.00](https://www.aliexpress.us/item/3256812345612252.html?spm=a2g0o.productlist.main.9.2bad4371fgiW8P&algo_pvid=5108629a-dc54-40a4-8ce6-8365ed6b640d&algo_exp_id=5108629a-dc54-40a4-8ce6-8365ed6b640d-8&pdp_ext_f=%7B%22order%22%3A%222%22%2C%22eval%22%3A%221%22%2C%22fromPage%22%3A%22search%22%7D&pdp_npi=6%40dis%21USD%2188.77%2188.77%21%21%2188.77%2188.77%21%4021032c8d17868520346163325e0d47%2112000058621449509%21sea%21US%210%21ABX%211%210%21n_tag%3A-29910%3Bd%3Aeeedf0d3%3Bm03_new_user%3A-29895&curPageLogUid=mKUxOH07K1PG&utparam-url=scene%3Asearch%7Cquery_from%3A%7Cx_object_id%3A1005012531927004%7C_p_origin_prod%3A)

<img src="assets/SIK RADIO.avif" alt="Sik Radio" width="200">


## Prerequisites

Download the [latest .elf](https://github.com/jesrockr/stm32_f9p_gnss_base_logger/releases/tag/v1.0.0)

Install the STM32 development tools from STMicroelectronics:

- STM32CubeProgrammer
- STM32CubeMX (developer only)
- STM32CubeIDE (developer only)

Download them from the official ST website:

https://www.st.com/en/development-tools/stm32-software-development-tools.html

Install u-blox u-center for configuring and testing the ZED-F9P:

- u-center GNSS evaluation software

Download from u-blox:

https://www.u-blox.com/en/product/u-center

Recommended workflow:

- Use STM32CubeProgrammer to flash the .ELF file to the STM32 board.
- `NOTE: BOOT0 jumper must be soldered in order to flash board (via usb type-c), then unsoldered to run program. Recommend install of a switch or two wires to simplify multiple flashes of board. Alternatively, an ST-Link V2 module can be used to flash the board through the debug pins "DIO, CLK, GND, 3v3"`
- Use FAT32 formatted SD card inserted into onboard STM32 slot.
- Use u-center to configure the F9P output messages and verify `.UBX` log playback.

## QUICK START

- [Wiring Diagram](assets/BASEWIRINGDIAGRAM.png)
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

## SURVEY-IN (add this to the BASE.TXT on sd card)
```
MODE=SURVEY_IN

SVIN_MIN_DUR_S=600

SVIN_ACC_M=0.7
```
### OR
## FIXED (add this to the BASE.TXT on sd card)
```
MODE=FIXED

LAT=37.1234568

LON=-121.1234568

MARK_ELEV_M=100.000

ANTENNA_HEIGHT_M=2.000

FIXED_ACC_M=0.020
```
The STM32 will read this textfile upon boot and send a command to the F9P.
