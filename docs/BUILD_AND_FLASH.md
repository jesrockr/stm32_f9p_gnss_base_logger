# Build and Flash

This guide assumes you have already installed:

- STM32CubeIDE
- STM32CubeProgrammer
- STM32CubeMX

Download STM32 tools from:

https://www.st.com/en/development-tools/stm32-software-development-tools.html

## Import the Project in STM32CubeIDE

1. Open STM32CubeIDE.
2. Select a workspace.
3. Go to `File` -> `Import`.
4. Choose `Existing Projects into Workspace`.
5. Click `Next`.
6. For `Select root directory`, browse to this repository folder.
7. STM32CubeIDE should detect the project.
8. Select the project and click `Finish`.

## Build the Firmware

1. In STM32CubeIDE, select the imported project.
2. Build the project:
   - Click the hammer icon, or
   - Go to `Project` -> `Build Project`.
3. Confirm the build finishes without errors.

The compiled firmware will be generated in the build output folder, usually:

`Debug/`

Common output files may include:

`*.elf
*.bin
*.hex`

These build output files are intentionally ignored by Git.


## FLASH THE STM32

  ### BEFORE FLASHING YOU MUST BRIDGE THE `BOOT0` JUMPER PADS. RECOMMEND USING 2 WIRES OR SWITCH FOR MULTIPLE FLASHES!


   You can flash using STM32CubeIDE or STM32CubeProgrammer.

Option 1: Flash From STM32CubeIDE

- Connect the STM32 board to your computer using ST-LINK/SWD/USB-C.
- In STM32CubeIDE, click the green Run button.
- Select the detected debug probe if prompted.
- STM32CubeIDE will build and flash the firmware.

Option 2 (Preferred): Flash With STM32CubeProgrammer

- Open STM32CubeProgrammer.
- Connect STM32 board using USB-C.
- Select `USB` and hit `⟳` then hit `Connect`.
- NOTE: USB only available when `BOOT0` jumper pad is bridged.
- Find/select the compiled firmware filepath from the Debug/ folder.
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

-By default, each boot sequence triggers a `cold start` on the f9p gnss. You will have to wait for `SURVEY IN: OK` for usable base data. The startup time depends on your `TMODE3` parameters on the f9p.
