# Build and Flash

This guide assumes you have already installed:

- STM32CubeProgrammer

Download STM32 tools from:

https://www.st.com/en/development-tools/stm32-software-development-tools.html


## FLASH THE STM32

  ### BEFORE FLASHING VIA USB TYPE C, YOU MUST BRIDGE THE `BOOT0` JUMPER PADS. RECOMMEND USING 2 WIRES OR SWITCH FOR MULTIPLE FLASHES! OR YOU CAN USE AN ST-LINK V2 MODULE CONNECTED TO THE STM32 `GND/ 3v3/ DIO/ CLK` DEBUG PINS.


Flash With STM32CubeProgrammer:

- Open STM32CubeProgrammer.
- Connect STM32 board using USB-C or ST-LINK V2.
- Select `USB` or `ST LINK` and hit `⟳` then hit `Connect`.
- NOTE: USB only available when `BOOT0` jumper pad is bridged.
- Find/select the .ELF filepath from the Debug/ folder.
- Click `Start Programming` to flash the STM32.
- Unplug USB

First Boot Check:
- After flashing:
- Insert a FAT-formatted SD card.
- Connect the ZED-F9P UART output to the STM32.
- Power the logger via USB-C.
- Watch the OLED boot sequence.


  ## Expected boot/logging behavior:

      BOOT
      UART CFG
      SDIO CFG
      FATFS OK
      MNT=OK SD=OK
      OPEN=OK SYNC=OK
      GNSS001.UBX READY
   
  ## During logging, the OLED should show something like:

      SD WRITE 55KB OK
      GNSS001.UBX
      LOGGING || 00:42
      SAT=18 3D
      UTC=18:24:11


## TROUBLESHOOTING

OLED Works But No GNSS Data
  
   -Check:

      F9P UART baud is 460800.
      STM32 USART1 baud is 460800.
      F9P UART TX is connected to STM32 PA10 / USART1 RX.
      Grounds are connected.
      F9P is outputting UBX messages on the UART connected to STM32.

SD card `Mount Fail` Appears
  
   -Check:

      SD card is inserted.
      SD card is FAT/FAT32 formatted.
      Try a different SD card.
      Confirm SDIO pins match the board design.

Log File Does Not Grow
  
   -Check:

      GNSS UART wiring.
      F9P message output configuration.
      Baud rate match.
      OLED warning messages.

`WARNING OVERRUN` Appears
  - The logger detected that incoming GNSS bytes may have been dropped.

       - Possible causes:

      `Too many F9P messages enabled.
      UART baud too low.
      SD card write latency too high.
      Measurement rate too high.
      Poor SD card quality.`
    
-For PPK, treat any overrun as a serious warning!!!


