# ZED-F9P Configuration Notes

## Required Output For PPK

Enable these UBX messages on the UART connected to the STM32 logger:

- `UBX-RXM-RAWX`
- `UBX-RXM-SFRBX`
- `UBX-NAV-PVT` at 1 Hz

`RAWX` and `SFRBX` are the important raw-observation/navigation messages for post-processing. `NAV-PVT` is used by the firmware for OLED status and GNSS UTC file timestamps.

## Messages To Disable

Unless specifically needed, disable:

- NMEA output on the logger UART
- Unneeded high-rate `NAV-*` messages
- RTCM output on the logger UART

## Baud Rate

The working setup has not yet been changed from its current baud. A future recommended setting is:

```text
F9P UART:     460800
STM32 USART: 460800
```

Change both sides together. If connected to the F9P over USB in u-center, remember that USB baud display may not reflect the physical UART baud going to the STM32.

## Saving Configuration

In u-center, save the receiver configuration to persistent memory when possible:

- RAM
- BBR
- Flash, if available

If saving is unreliable, first test with RAM-only settings, then power-cycle to confirm whether the configuration is retained.

