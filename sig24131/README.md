# sig24131 — MCU-Independent SIG24131 ADC Driver

A portable C99 driver for the SIG24131 24-bit delta-sigma ADC: configuration,
conversion reads, reset/sync/sleep/wakeup commands and the nominal data-rate
lookup. The platform supplies the SPI transfer and the chip-select callbacks.

- **Pure C99**: `sig24131.h` includes only `<stddef.h>` and `<stdint.h>` — no
  MCU, RTOS, vendor SDK or middleware header.
- **Injected SPI**: `sig24131_port_t` carries one full-duplex transfer plus
  `cs_assert`/`cs_release`; the driver knows nothing about your SPI peripheral.
- **No cache**: the object holds the port only, so it cannot drift out of step
  with the device. Every configuration write is read back and verified.
- **No waiting, no allocation**: the driver never delays and never allocates.
  Timing, including the 43 ms reset delay, is the caller's responsibility.
- **SIG24131 family**: the SIG24130/SIG24131/SIG24132 share the register map;
  this API is named for the SIG24131. The 16-pin SIG24130 lacks AIN4..AIN7,
  and REFP1/REFN1 is SIG24132-only.

## Layout

```
sig24131/                   this module's root
├── README.md               this file
├── inc/
│   └── sig24131.h          public interface
├── src/
│   └── sig24131.c          driver implementation
└── SIG24130_131_132_datasheet_*.pdf
                            original and translated datasheets
```

## Scope

**In**: construction and defaults, reset/sync/sleep/wakeup commands, CONF0 and
CONF1 read/write with read-back verification, 24-bit conversion reads, and the
nominal data-rate lookup.

**Out**:

| Excluded | Why |
|---|---|
| Offset and gain calibration | A calibration command needs a known stimulus and settled data; the driver has no timing model. |
| Data checksum/parity byte | The driver reads exactly three conversion bytes and leaves DATACHK cleared. |
| DRDY pin handling | DOUT/DRDYn is a board-level GPIO/ISR concern; the caller polls or reads on its own schedule. |
| Direct data read | The driver uses the read-data command (`0x60`), which is safe while a conversion is in progress. |
| Hardware verification | Only the register contract and compilation are checked; there is no host test yet. |

## Device overview

The SIG24131 is a 24-bit, low-noise delta-sigma ADC with:

- a PGA selectable from 1 to 256;
- output data rates from 2.5 SPS to 9600 SPS;
- SINC1 and SINC4 digital filters;
- external reference inputs or an internal 1.17 V/2.5 V reference;
- two matched IDACs for RTD excitation;
- an input multiplexer covering AIN0..AIN7, AINCOM, the internal temperature
  sensor, the reference, the supplies and divided supply nodes;
- a burnout current source, VBIAS generator, fast-startup and single-shot
  modes.

The SPI interface is **mode 1** (CPOL = 0, CPHA = 1), MSB first. DIN is
sampled on the SCLK falling edge and DOUT/DRDYn changes on the rising edge.
CSn must stay low for the whole command or data-read operation; taking it high
resets the serial interface and aborts the current operation. A four-wire SPI
connection with CSn toggled between frames is recommended.

Conversion data is 24-bit two's complement, MSB first and left-aligned:
`0x000000` is zero differential input, `0x7FFFFF` is positive full scale and
`0x800000` is negative full scale. Out-of-range inputs clip. The driver returns
the sign-extended value as `int32_t`.

## Register map

| Address | Register | Access | Reset value |
|---|---|---|---|
| `0x01` | CONF0 | read/write | `0xX00000` |
| `0x02` | CONF1 | read/write | `0x000201` |
| `0x03` | OFFSET | read/write | `0x000000` |
| `0x04` | GAIN | read/write | `0x400000` |

Only CONF0 and CONF1 are exposed. OFFSET and GAIN belong to the calibration
path, which is out of scope. Conversion data is read with command `0x60`, not
through a register address.

The three-byte register data is sent MSB first. A register write is
`0x20 | address` followed by the three data bytes; a register read is
`0x40 | address` followed by three bytes shifted out by the device. WREG is not
acknowledged, which is why `sig24131_write_config()` reads both registers back.

### CONF0 (`0x01`)

| Bit(s) | Field | `sig24131_config_t` | Meaning |
|---|---|---|---|
| 23 | EXTCLK | — | Read-only clock source; write 0. |
| 22 | DATACHK | — | Data checksum enable; the driver leaves it 0. |
| 21:20 | — | — | Reserved. |
| 19 | VBIAS | `vbias_enable` | VBIAS generator enable. |
| 18 | FAST_ON | `fast_startup` | Fast startup. |
| 17 | — | — | Reserved. |
| 16 | REF_EN | `internal_reference_enable` | Internal reference enable. |
| 15 | BUF | `input_buffer_enable` | Analog input buffer; forced on above gain 2. |
| 14:12 | DELAY | `conv_delay` | Conversion-start delay. |
| 11 | BCS | `burnout_enable` | Burnout current source. |
| 10:8 | IDAC | `idac_current` | IDAC current selection. |
| 7 | IDAC_PAIR | `idac_pair_enable` | Enable both IDACs. |
| 6 | DACREF_EX | `idac_external_reference` | IDAC reference selection. |
| 5:3 | IDAC2_MUX | `idac2_mux` | IDAC2 output pin. |
| 2:0 | IDAC1_MUX | `idac1_mux` | IDAC1 output pin. |

### CONF1 (`0x02`)

| Bit(s) | Field | `sig24131_config_t` | Meaning |
|---|---|---|---|
| 23 | LATENCY | `latency` | Zero-latency mode; 0 = normal latency. |
| 22 | SINGLE | `single_shot` | 1 = one conversion, then standby/sleep. |
| 21:20 | REFSEL | `reference` | 00 = REFP/REFN, 10 = 2.5 V internal, 11 = 1.17 V internal. |
| 19:16 | PGA | `gain` | Gain code 0..8 for 1..256. |
| 15 | FILTER | `filter` | 0 = SINC1, 1 = SINC4. |
| 14 | REJ60 | `reject_60hz` | 50/60 Hz rejection when the rate is `0x09`. |
| 13:8 | DR | `data_rate` | Data-rate code. |
| 7:4 | MUXP | `mux_p` | Positive input selection. |
| 3:0 | MUXN | `mux_n` | Negative input selection. |

REFSEL code `01` (REFP1/REFN1) is not exposed because it is SIG24132-only.
The internal reference requires both `internal_reference_enable = 1` and a
`reference` value of `SIG24131_REF_INTERNAL_2V5` or
`SIG24131_REF_INTERNAL_1V17`.

## Defaults

`sig24131_default_config()` fills the reset-equivalent configuration:

- gain 1, SINC1 filter, external REFP/REFN reference;
- 10 SPS, zero conversion delay;
- IDACs off;
- MUXP = AIN0, MUXN = AIN1;
- both IDAC routing fields = AIN0;
- all enable flags cleared.

Encoded, this is CONF0 = `0x000000` and CONF1 = `0x000201`. CONF1 matches its
reset value; CONF0 matches the reset value apart from the read-only EXTCLK bit,
which the driver writes as 0. Set `gain` above 2, `filter`, `reference`,
`data_rate`, the mux fields and any enable flags explicitly for the application.

## Data rates

| SPS | Code | SPS | Code |
|---|---|---|---|
| 2.5 | `0x00` | 5 | `0x01` |
| 10 | `0x02` | 12.5 | `0x03` |
| 15 | `0x04` | 20 | `0x05` |
| 25 | `0x06` | 30 | `0x07` |
| 40 | `0x08` | 50 | `0x09` |
| 60 | `0x0A` | 80 | `0x0B` |
| 100 | `0x0C` | 120 | `0x0D` |
| 160 | `0x0E` | 200 | `0x0F` |
| 240 | `0x10` | 250 | `0x31` |
| 300 | `0x11` | 400 | `0x12` |
| 480 | `0x13` | 500 | `0x34` |
| 600 | `0x14` | 800 | `0x15` |
| 1000 | `0x36` | 1200 | `0x16` |
| 2000 | `0x37` | 2400 | `0x17` |
| 4000 | `0x38` | 4800 | `0x18` |
| 8000 | `0x39` | 9600 | `0x19` |

`sig24131_data_rate_from_millihz()` maps a nominal rate in millihertz to its
code, for example `10000U` for 10 SPS. The nominal conversion period is
`1000000 / rate_millihz` milliseconds; the filter selection and LATENCY bit can
stretch the first samples, so use the datasheet table for exact settling.

## Porting

`sig24131_port_t` has three required callbacks. `sig24131_ctor()` rejects a port
that leaves any of them NULL.

| Callback | Contract |
|---|---|
| `spi_transfer(ctx, tx, rx, len)` | Full-duplex, exactly `len` bytes, MSB first, SPI mode 1; `rx` receives the bytes shifted out while `tx` is sent. |
| `cs_assert(ctx)` | Drive CSn low. |
| `cs_release(ctx)` | Drive CSn high; this also resets the device's SPI interface. |

The driver holds CSn low for one complete frame: the command byte followed by
three bytes for register and conversion-data operations, then release. A frame
is never split across calls. Return `SIG24131_OK` for success; any other value
is passed back to the caller unchanged.

Command frames used by the driver:

| Operation | DIN | DOUT |
|---|---|---|
| Reset | `0x02` | ignored |
| Sync | `0x04` | ignored |
| Sleep | `0x0A` | ignored |
| Wakeup | `0x0C` | ignored |
| Read register | `0x40 \| address`, `00 00 00` | three data bytes |
| Write register | `0x20 \| address`, `d23..d0` | ignored |
| Read data | `0x60`, `00 00 00` | three data bytes |

### Lifecycle

1. Construct the object with `sig24131_ctor()`.
2. If the device was powered up or reset, wait `SIG24131_RESET_DELAY_MS`
   (43 ms) before the first command.
3. Optionally send `sig24131_reset()`, then wait 43 ms again.
4. Write the configuration with `sig24131_write_config()`; the write resets
   the digital filter and starts a new conversion.
5. Wait for a settled sample. The nominal period is 1/data-rate; filter and
   LATENCY settings can make the first samples longer.
6. Read with `sig24131_read_data()`. It does not wait; poll at the data rate,
   or use the DRDY signal at board level.
7. Use `sig24131_sync()` to restart the conversion, `sig24131_sleep()` and
   `sig24131_wakeup()` for low-power operation.

### Minimal example

```c
#include "sig24131.h"

static sig24131_t adc;

void adc_init(const sig24131_port_t* port)
{
    sig24131_config_t config;

    (void)sig24131_ctor(&adc, port);
    (void)sig24131_reset(&adc);
    delay_ms(SIG24131_RESET_DELAY_MS);

    sig24131_default_config(&config);
    config.gain                      = SIG24131_GAIN_128;
    config.reference                 = SIG24131_REF_INTERNAL_2V5;
    config.internal_reference_enable = 1U;
    config.data_rate                 = SIG24131_RATE_10;
    (void)sig24131_write_config(&adc, &config);

    /* The write restarted the filter; wait for the first settled sample. */
    delay_ms(100U); /* 10 SPS nominal period; adjust for filter/latency. */
}

int32_t adc_read(void)
{
    int32_t code = 0;

    (void)sig24131_read_data(&adc, &code);
    return code;
}
```

`code` is the sign-extended 24-bit conversion result. The application converts
it to a voltage with the selected reference and gain:

```text
VIN = code * VREF / (gain * 2^23)
```

### Reset, sync and sleep

- `sig24131_reset()` sends `0x02`; the registers return to their reset values
  and the device needs `SIG24131_RESET_DELAY_MS` before the next command.
- `sig24131_sync()` sends `0x04`; the digital filter restarts and the result in
  progress is discarded.
- `sig24131_sleep()` sends `0x0A`; `sig24131_wakeup()` sends `0x0C`.

## Status codes

| Code | Meaning |
|---|---|
| `SIG24131_OK` | Operation succeeded. |
| `SIG24131_ERR_ARG` | NULL argument, a callback missing from the port, or an invalid data-rate argument. |
| `SIG24131_ERR_IO` | A platform SPI or CS callback failed; its value is passed through. |
| `SIG24131_ERR_VERIFY` | Read-back configuration differs from the write. |

## Testing

This module currently has no host test. The compile check is:

```sh
gcc -std=c99 -Wall -Wextra -Werror -pedantic -I device/sig24131/inc \
    -c device/sig24131/src/sig24131.c -o sig24131.o
```

On Windows (the MinGW GCC this repository was developed with):

```powershell
C:\Users\HP\mingw64\bin\gcc.exe -std=c99 -Wall -Wextra -Werror -pedantic `
  -I device/sig24131/inc `
  -c device/sig24131/src/sig24131.c `
  -o sig24131.o
```

No warnings, and no executable is produced. A host test with a fake SPI port
would be the next verification step; it is not part of this module yet.
