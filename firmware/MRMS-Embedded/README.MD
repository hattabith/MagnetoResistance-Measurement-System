# MRMS-Embedded Firmware

Firmware for SparkFun Pro Micro (ATmega32U4) with:
- SSR (solid-state relay) control on pin `5`
- Hall sensor SS49E connected through ADS1115 (channel A0)
- text protocol over UART (USB Serial)

## Hardware mapping
- Relay input: digital pin `5`
- ADS1115: I2C (`SDA`, `SCL`)
- Hall sensor SS49E output: ADS1115 `A0`

## UART settings
- Speed: `115200`
- Line ending: `\n` (LF)
- `\r` is ignored
- Command is case-insensitive

## Text protocol
Controller accepts one command per line.

### Commands
- `HELP` - show command list
- `RELAY ON` or `ON` - turn relay ON (pin 5 = HIGH)
- `RELAY OFF` or `OFF` - turn relay OFF (pin 5 = LOW)
- `RELAY TOGGLE` or `TOGGLE` - toggle relay state
- `STATUS` - print relay state + Hall sensor readout
- `READ` - same as `STATUS`

### Response format
Successful status response:

```text
OK RELAY=<ON|OFF> HALL_RAW=<int16> HALL_V=<volts>
```

Error responses:

```text
ERR UNKNOWN_COMMAND=<input>
Type HELP
```

```text
ERR COMMAND_TOO_LONG
```

```text
ERR ADS1115_NOT_FOUND
```

## Typical session
```text
> HELP
Commands:
  HELP           - show this help
  RELAY ON       - turn relay ON (pin 5 = HIGH)
  RELAY OFF      - turn relay OFF (pin 5 = LOW)
  RELAY TOGGLE   - toggle relay state
  STATUS         - print relay state and Hall sensor value
  READ           - same as STATUS

> RELAY ON
OK RELAY=ON HALL_RAW=13345 HALL_V=1.6681

> STATUS
OK RELAY=ON HALL_RAW=13341 HALL_V=1.6676
```

## Build and upload (PlatformIO)
From this folder (`firmware/MRMS-Embedded`):

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

## Notes
- ADS1115 gain is configured as `GAIN_ONE` (±4.096 V).
- Reported voltage is converted by library helper `computeVolts(...)`.
- Relay defaults to OFF on boot.
