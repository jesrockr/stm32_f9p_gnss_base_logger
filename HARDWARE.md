# Hardware Notes

## Tested Hardware

- MCU: STM32F407ZGT6
- Board: FK407M2-ZGT6 V1.1 style development board
- GNSS receiver: u-blox ZED-F9P
- Display: SSD1306 128x64 OLED over I2C
- Storage: SD card over SDIO

## Connections

Document the exact wiring used in your build before publishing:

- F9P UART TX to STM32 USART RX
- F9P GND to STM32 GND
- OLED SDA/SCL to STM32 I2C pins
- SD card connected through board SDIO socket/interface

Recommended additions:

- Add a wiring photo.
- Add a pin table.
- Add board revision notes, especially if using a clone/development board.

## Optional External Button

The onboard KEY button was not used in the final firmware because its GPIO mapping was not confirmed reliably.

If a physical "new log file" button is desired, use an external momentary switch:

- One side of switch to a confirmed-free STM32 GPIO.
- Other side of switch to GND.
- Configure that GPIO with internal pull-up.
- Enable `BUTTON_ROTATE_LOG_ENABLED` in firmware.

Avoid pins already used for SDIO, SWD, UART, I2C, or oscillator/debug functions.

