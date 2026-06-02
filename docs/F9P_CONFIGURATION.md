# ZED-F9P Configuration Notes

## Required Output For PPK

Enable these UBX messages on the UART connected to the STM32 logger:

- `UBX-RXM-RAWX`
- `UBX-RXM-SFRBX`
- `UBX-NAV-PVT` at 1 Hz
- `UBX-NAV-SVIN` at 1 Hz

`RAWX` and `SFRBX` are the important raw-observation/navigation messages for post-processing. `NAV-PVT` is used by the firmware for OLED status and GNSS UTC file timestamps.

## Messages To Disable

Unless specifically needed, disable:

- NMEA output on the logger UART
- Unneeded high-rate `NAV-*` messages
- RTCM output on the logger UART

## Baud Rate

The STM32 firmware expects the F9P UART connected to the logger to run at `460800` baud:

```text
F9P UART:     460800
STM32 USART1: 460800
```

If connected to the F9P over USB in u-center, remember that USB baud display may not reflect the physical UART baud going to the STM32. Configure the actual F9P UART port that feeds the logger.

## TMODE3

For static base station, recommend
- Mode:  1-Survey in
        or
        2-Fixed Mode
        
- Minimum Observation Time:
        600s (For 10-minute survey-in)
- Required Position Accuracy
        User-defined (Recommend 0.7M)

Please note that STM32 will force F9p to cold-start on each boot, to prevent re-using surveyed-in coordinates.
If you wish to be able to hot-start for rover/ fixed base, you must remove the STM32 tx --> F9P rx

## Saving Configuration

In u-center, save the receiver configuration to persistent memory when possible:

- RAM
- BBR
- Flash, if available

If saving is unreliable, first test with RAM-only settings, then power-cycle to confirm whether the configuration is retained.
