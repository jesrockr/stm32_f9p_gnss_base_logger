# Hardware Notes

## Tested Hardware

- MCU: STM32F407ZGT6
- Board: FK407M2-ZGT6 V1.1 style development board
- GNSS receiver: u-blox ZED-F9P
- Display: SSD1306 128x64 OLED over I2C
- Storage: microSD card over SDIO (SanDisk Ultra 16GB)

## Wiring

<img src="../assets/BASEWIRINGDIAGRAM.png" alt="Base wiring diagram" width="700">

USB POWER BANK    --->  ZED-F9P USB-C 

USB POWER BANK    --->  STM32 USB-C 

ZED-F9P GND       --->  STM32 GND

ZED-F9P UART1 TX  --->  STM32 PA10 / USART1 RX

ZED-F9P UART1 RX  <---  STM32 PA9 / USART1 TX
  



OLED SCL/SCK      --->  STM32 PB6 / I2C1 SCL

OLED SDA          --->  STM32 PB7 / I2C1 SDA

OLED VDD          --->  STM32 5V

OLED GND          --->  STM32 GND


KEY NOTES:
- F9P UART1 TX/RX to STM32 USART1 RX/TX
- F9P GND to STM32 GND
- OLED SDA/SCL to STM32 I2C pins
- SD card connected through board SDIO socket/interface
- Power each board separately, not advised to share 3v3 rails


## Optional External Button

The onboard KEY button was not used in the final firmware because its GPIO mapping was not confirmed reliably.

If a physical "new log file" button is desired, use an external momentary switch:

- One side of switch to a confirmed-free STM32 GPIO.
- Other side of switch to GND.
- Configure that GPIO with internal pull-up.
- Enable `BUTTON_ROTATE_LOG_ENABLED` in firmware.

Avoid pins already used for SDIO, SWD, UART, I2C, or oscillator/debug functions.

