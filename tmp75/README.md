# tmp75 — MCU-Independent TMP75 Temperature Sensor Driver

A portable C99 driver for the Texas Instruments TMP75 temperature sensor:
configuration, 12-bit temperature reading in degrees Celsius, the T_HIGH/T_LOW
alert limits, and the one-shot conversion. The platform supplies one I2C
transfer, the driver supplies everything else.

- **Pure C99**: `tmp75.h` includes only `<stddef.h>` and `<stdint.h>` — no MCU,
  RTOS, vendor SDK or middleware header.
- **Injected bus**: `tmp75_port_t.transfer()` runs one complete write-then-read
  transaction; the driver knows nothing about your I2C peripheral or bit-bang
  routine.
- **No cache**: the object holds the port and the 7-bit address only — no
  configuration copy, no pointer, no temperature. Every access writes the
  pointer first, so the driver cannot disagree with the device.
- **No waiting, no allocation**: no delay loop, no `malloc`, no global mutable
  state, no `libm`. Timing is handed back as a millisecond value
  (`tmp75_conversion_time_ms()`) for the caller to schedule against.
- **TMP75 only**: the eight addresses `0x48..0x4F`. The TMP175's 27 addresses
  (including the floating-pin ones), the ALERT pin, the SMBus Alert Response
  Address and the General Call Reset are out of scope — see *Scope* below.

## Layout

```
tmp75/                      this module's root
├── README.md               this file
├── inc/
│   └── tmp75.h             public interface
├── src/
│   └── tmp75.c             driver implementation
├── tmp75.pdf               TMP175/TMP75 datasheet, SBOS288M
├── tmp75_CN.pdf            TMP175/TMP75 datasheet, Chinese
└── test/
    └── tmp75_test.c        host test with a fake I2C device
```

## Scope

**In**: construction and defaults, configuration read/write with read-back
verification, temperature reading and Celsius conversion, T_HIGH/T_LOW
read/write with verification, one-shot triggering from shutdown, conversion
time lookup, the host test, this README.

**Out**:

| Excluded | Why |
|---|---|
| ALERT pin reading | A GPIO signal, not an I2C transaction; handle it in the board layer. |
| SMBus Alert Response Address | Bus arbitration across devices, belongs to the bus controller. |
| General Call Reset | Resets every device on the bus; too risky for a driver to send. |
| TMP175's 27 addresses | Only TMP75 addresses `0x48..0x4F` are accepted. |
| Calibration | The TMP75 has no calibration register. |
| Hardware verification | This round validates protocol frames and numbers on the host only. |

## Device overview

The TMP75 is a digital temperature sensor in the industry-standard LM75
SOIC-8/MSOP-8 footprint, specified for −40 °C to +125 °C with ±1 °C typical
accuracy, 9- to 12-bit user-selectable resolution, and an I2C/SMBus two-wire
interface. The 7-bit slave address is `1001 A2 A1 A0`, so three address pins
give eight devices on one bus: `0x48` .. `0x4F`. Address pins are sampled at
reset or at the start of a communication and then latched.

The driver adds no behaviour of its own on top of the datasheet: it encodes and
decodes the four registers, verifies what it writes, and converts the 12-bit
two's-complement format to `float` degrees Celsius.

### Register map

The pointer register uses only its two LSBs and powers up as `00`, so a fresh
device answers temperature reads without being addressed first.

| P1 P0 | Pointer | Register | Access | Reset value |
|---|---|---|---|---|
| `00` | `0x00` | Temperature | read-only | 0 °C until the first conversion completes |
| `01` | `0x01` | Configuration | read/write | `0x00` |
| `10` | `0x02` | T_LOW | read/write | 75 °C (`0x4B0`) |
| `11` | `0x03` | T_HIGH | read/write | 80 °C (`0x500`) |

Every access starts by writing the pointer byte, so the driver never relies on
a pointer another master may have left behind.

### Configuration register (reset `0x00`)

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
|---|---|---|---|---|---|---|---|
| OS | R1 | R0 | F1 | F0 | POL | TM | SD |

- **OS** — write 1 while shut down to start one conversion; always reads back
  as 0, so it is a command, not a setting.
- **R1 R0** — resolution: 9, 10, 11 or 12 bits.
- **F1 F0** — fault queue (see below).
- **POL** — ALERT output polarity.
- **TM** — thermostat mode (see below).
- **SD** — 1 = shutdown, 0 = continuous conversion. The device finishes the
  conversion in progress and then shuts down.

`tmp75_write_config()` always writes OS = 0, which is why the read-back check
can compare the whole byte without masking anything out.

### Temperature and limit format

Temperature, T_HIGH and T_LOW share one format: 12-bit two's complement, most
significant byte first, 1 LSB = 0.0625 °C, representable range −128.0 °C to
+127.9375 °C. The lower four bits of byte 2 read as 0 in the temperature
register. All resolutions use this 12-bit layout; the unused low bits read as
0 in 9-, 10- and 11-bit mode.

| °C | Code | °C | Code |
|---|---|---|---|
| 127.9375 | `0x7FF` | −0.25 | `0xFFC` |
| 100 | `0x640` | −25 | `0xE70` |
| 80 | `0x500` | −55 | `0xC90` |
| 75 | `0x4B0` | −128 | `0x800` |
| 25 | `0x190` | 0.25 | `0x004` |
| 0 | `0x000` | | |

The driver multiplies by 0.0625 in `float`: that is a negative power of two,
so the scaling is exact and no rounding error is introduced. The device's
operating range (−40 °C to +125 °C) is narrower than the format; the format is
what `tmp75_write_limit()` enforces, so +127.9375 °C is the largest limit that
can be stored at all.

## Fault queue (F1/F0)

**Fault queue** is the datasheet's name for F1/F0, and it is not a software
queue: it is the setting "how many *consecutive* out-of-limit conversions are
needed before ALERT is allowed to act". A fault is one conversion whose result
is outside the T_HIGH/T_LOW window. Requiring several in a row filters noise —
a single noisy sample no longer flips ALERT — at the cost of a slower response:
the ALERT reacts after roughly `N × conversion time`.

Table 7-9 of SBOS288M, and the important part is that the two parts do **not**
decode the last two settings the same way:

| F1 | F0 | TMP75 consecutive faults | TMP175 consecutive faults |
|---|---|---|---|
| 0 | 0 | 1 | 1 |
| 0 | 1 | 2 | 2 |
| 1 | 0 | **3** | 4 |
| 1 | 1 | **4** | 6 |

- `tmp75_fault_queue_t` maps 1:1 onto the F1/F0 field: `TMP75_FAULT_1` ..
  `TMP75_FAULT_4` are `0..3`. The enumerator names count *faults*, not the
  field value.
- The default `TMP75_FAULT_1` comes from the register reset value `0x00`.
- The driver is TMP75-only, so `TMP75_FAULT_3` and `TMP75_FAULT_4` mean 3 and 4
  conversions here. Do not carry them over to a TMP175.
- Resolution does not shrink the comparison: at 9-bit resolution the low three
  bits of T_HIGH/T_LOW still take part in the ALERT comparison, so a limit that
  is not a multiple of 0.5 °C behaves as its full 12-bit value.

Choosing a queue is a trade-off: `TMP75_FAULT_1` gives the fastest reaction
(one conversion time), `TMP75_FAULT_4` gives the strongest noise immunity at
four conversion times.

## Thermostat mode (TM)

**Thermostat mode** is the name of the TM bit — not a third operating mode.
The bit has exactly two values, and they change how ALERT behaves after a
limit crossing:

| | `TM = 0` Comparator (`TMP75_THERMOSTAT_COMPARATOR`) | `TM = 1` Interrupt (`TMP75_THERMOSTAT_INTERRUPT`) |
|---|---|---|
| ALERT sets | temperature ≥ T_HIGH for N consecutive faults | same |
| ALERT clears | temperature < T_LOW for N consecutive faults | any register read, SMBus ARA response, shutdown, General Call Reset |
| Reading a register | does **not** clear ALERT | clears ALERT |
| Re-trigger after clearing | automatic once the temperature re-crosses the limit | requires crossing the *opposite* limit first |
| Character | hysteresis, behaves like a mechanical thermostat | one latched event per crossing, alternating high/low events |

Comparator mode therefore produces a level that tracks the temperature with
hysteresis, and the host can read as often as it likes without disturbing it.
Interrupt mode produces an event: the ALERT pin latches, the host services it,
and the next ALERT means the temperature went past the *other* limit — the two
limits alternate as events.

Notes that matter in practice:

- `tmp75_thermostat_mode_t` maps 1:1 onto TM: comparator `0`, interrupt `1`.
  The default `TMP75_THERMOSTAT_COMPARATOR` comes from the reset value `0x00`.
- In interrupt mode **every register read clears ALERT**, including
  `tmp75_read_temperature()`, `tmp75_read_config()` and `tmp75_read_limit()`,
  as well as the read-back inside `tmp75_write_config()` and
  `tmp75_write_limit()`. Service the ALERT pin first, then read or write.
- `tmp75_one_shot()` reads the configuration register before it writes, so it
  can clear a latched ALERT in interrupt mode too.
- Changing TM clears an existing ALERT in either mode (TMP75 behavior
  clarification in SBOS288M revision M).
- ALERT is an open-drain output and needs a pull-up (4.7 kΩ is the
  datasheet recommendation). POL only chooses the polarity — active low (reset)
  or active high — it does not change push-pull vs open-drain.

## Continuous conversion and when to read

With SD = 0 the device converts continuously, and the temperature register
holds the result of the **most recent completed** conversion.

- Reading over I2C returns that result. A read never starts, stops or
  interrupts a conversion.
- Reading faster than the conversion period simply returns the same value
  again. Freshness is set by the conversion period, not by how often you poll.
- After power-up or reset the temperature register reads 0 °C until the first
  conversion completes — a legitimate reading of exactly 0 °C is
  indistinguishable from "not converted yet".
- The TMP75 has **no data-ready indicator**: no status register, no DRDY pin,
  no pollable bit (OS reads back as 0). "Is this sample new?" cannot be asked.

Consequences for the caller:

- Poll no more often than once per `tmp75_conversion_time_ms(resolution)`:
  leave at least the worst-case conversion time between reads if you want each
  read to be a new sample.
- When you need a deterministic sample — a known temperature at a known moment
  — use shutdown plus one-shot (below) instead of polling.

Conversion time (SBOS288M, electrical characteristics):

| R1 R0 | Resolution | Typical | Maximum | `tmp75_conversion_time_ms()` |
|---|---|---|---|---|
| 0 0 | 9 bit | 27.5 ms | 37.5 ms | 38 |
| 0 1 | 10 bit | 55 ms | 75 ms | 75 |
| 1 0 | 11 bit | 110 ms | 150 ms | 150 |
| 1 1 | 12 bit | 220 ms | 300 ms | 300 |

The driver returns the maximum rounded up to whole milliseconds, so a wait
based on it is always long enough; the typical values are for information only.

Read timing in continuous mode:

```text
conversion N completes  -->  temperature register = T(N)
    read                 -->  T(N)
    read                 -->  T(N)      (conversion N+1 not finished: repeat)
conversion N+1 completes -->  temperature register = T(N+1)
    read                 -->  T(N+1)
```

## Limits, ALERT and one-shot

- T_HIGH resets to 80 °C, T_LOW to 75 °C; `tmp75_write_limit()` and
  `tmp75_read_limit()` take `float` Celsius and convert to/from the 12-bit
  format, rounding to the 0.0625 °C grid. Values outside
  −128.0 .. +127.9375 °C — and NaN — return `TMP75_ERR_ARG` before any bus
  traffic.
- The ALERT pin itself is not read by the driver. Wire it to a GPIO
  interrupt, or use the SMBus Alert Response Address at bus level.
- Because `tmp75_write_limit()` reads back what it wrote, a device that
  stored something else reports `TMP75_ERR_VERIFY` instead of a silent
  mismatch.

Single-conversion sequence:

```text
tmp75_write_config(shutdown = 1)
    (if the device was converting continuously, wait one conversion period
     so it actually reaches shutdown)
tmp75_one_shot()                       --> TMP75_ERR_STATE if not shut down
wait tmp75_conversion_time_ms(resolution)
tmp75_read_temperature()
```

`tmp75_one_shot()` refuses to run while SD = 0 and returns
`TMP75_ERR_STATE` without writing anything: the datasheet only starts a
conversion when the device is shut down. A device that has SD = 1 but is still
finishing the conversion that preceded shutdown looks idle to the driver, so
the caller must wait one conversion period as shown above. Wait times come from
the conversion time table above.

## Porting and usage

### The transfer contract

`tmp75_port_t.transfer()` performs exactly one transaction:

```text
START -> address+W -> tx[0..tx_len-1]
      -> repeated START -> address+R -> rx[0..rx_len-1] -> STOP
```

- `rx_len == 0` means a pure write: the frame stops after the transmit phase.
- `tx_len` is at least 1 — the driver always sends the pointer byte first.
  A register write is `tx = {pointer, data...}` with `rx_len = 0`; a register
  read is `tx = {pointer}` with `rx_len = 1 or 2`.
- `address` is the 7-bit slave address; the platform generates the R/W bit.
- Return `TMP75_OK` for an acknowledged transaction. Any other value is passed
  to the caller unchanged (`TMP75_ERR_IO` is the conventional choice for a
  hardware failure).

A single transfer keeps the pointer write and the data read in one atomic
transaction, so another master cannot change the pointer in between. The
TMP75 does not stretch SCL, so a software I2C master without clock-stretch
support is compatible.

### Adapting `i2c_soft`

The board layer needs only a thin adapter:

```c
static i2c_soft_t i2c_bus;      /* built with i2c_soft_ctor() and _init() */

static tmp75_status_t bus_transfer(void* ctx, uint8_t addr,
                                   const uint8_t* tx, size_t tx_len,
                                   uint8_t* rx, size_t rx_len)
{
    i2c_soft_t* bus = (i2c_soft_t*)ctx;
    i2c_soft_status_t status;

    if ( rx_len == 0U ) {
        status = i2c_soft_write(bus, addr, tx, tx_len);
    } else {
        status = i2c_soft_write_read(bus, addr, tx, tx_len, rx, rx_len);
    }
    return (status == I2C_SOFT_OK) ? TMP75_OK : TMP75_ERR_IO;
}

static const tmp75_port_t sensor_port = {&i2c_bus, bus_transfer};
```

`protocol/i2c_soft/` already offers exactly the two shapes a register access
needs — `i2c_soft_write()` for `rx_len == 0`, `i2c_soft_write_read()` for the
repeated-start read — so the adapter stays this thin. Any other I2C master
with the same pair works the same way.

### Minimal example

```c
#include "tmp75.h"

static tmp75_t sensor;

void sensor_init(const tmp75_port_t* port)
{
    tmp75_config_t config;

    (void)tmp75_ctor(&sensor, port, TMP75_ADDRESS_0);

    tmp75_default_config(&config);      /* 9-bit, comparator, continuous */
    config.resolution = TMP75_RESOLUTION_12_BIT;
    (void)tmp75_write_config(&sensor, &config);   /* verified read-back */

    (void)tmp75_write_limit(&sensor, TMP75_LIMIT_HIGH, 60.0f);
    (void)tmp75_write_limit(&sensor, TMP75_LIMIT_LOW, 40.0f);
}

float sensor_read_celsius(void)
{
    float celsius = 0.0f;

    /* Call at most once per tmp75_conversion_time_ms(resolution). */
    (void)tmp75_read_temperature(&sensor, &celsius);
    return celsius;
}
```

Deterministic single sample (see the sequence in the previous section):

```c
void sensor_read_once(float* celsius)
{
    tmp75_config_t config;

    (void)tmp75_read_config(&sensor, &config);  /* keep resolution and mode */
    config.shutdown = 1U;
    (void)tmp75_write_config(&sensor, &config);
    delay_ms(tmp75_conversion_time_ms(config.resolution)); /* off now */
    (void)tmp75_one_shot(&sensor);         /* ERR_STATE if not shut down */
    delay_ms(tmp75_conversion_time_ms(config.resolution)); /* done */
    (void)tmp75_read_temperature(&sensor, celsius);
}
```

Status codes: `TMP75_OK`, `TMP75_ERR_ARG` (NULL, out-of-range enum, temperature
outside the 12-bit range), `TMP75_ERR_IO` (the platform transfer failed),
`TMP75_ERR_VERIFY` (the device stored something else), `TMP75_ERR_STATE`
(one-shot requested while the device is not shut down).

## Testing

`test/tmp75_test.c` is a host test with a fake I2C device: a four-register
model that parses each transaction, records the frames, and can be told to flip
a bit after a write to prove the read-back verification fires. It covers the
constructor, defaults, configuration round-trips over every field combination,
temperature decoding against the datasheet vector table, limit round-trips and
range checks, one-shot state handling, frame shapes and the error paths. There
is no framework and nothing is compiled into the driver:

```sh
gcc -std=c99 -Wall -Wextra -Werror -pedantic -I device/tmp75/inc \
    device/tmp75/src/tmp75.c \
    device/tmp75/test/tmp75_test.c -o tmp75_test
./tmp75_test
```

On Windows (the MinGW GCC this repository was developed with):

```powershell
C:\Users\HP\mingw64\bin\gcc.exe -std=c99 -Wall -Wextra -Werror -pedantic `
  -I device/tmp75/inc `
  device/tmp75/src/tmp75.c `
  device/tmp75/test/tmp75_test.c `
  -o tmp75_test.exe
.\tmp75_test.exe
```

Expected output, exit code 0, no compiler warnings:

```text
1190 checks, 0 failures
```

The test prints `N checks, M failures` and exits non-zero on any failure, so it
can be dropped into a CI step unchanged.
