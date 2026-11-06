#ifndef TMP75_H
#define TMP75_H

/**
 * @file tmp75.h
 * @brief MCU-independent driver interface for the TMP75 temperature sensor.
 *
 * The driver covers configuration, the temperature register, the alert limits
 * and the one-shot conversion; the application supplies the I2C transfer
 * through tmp75_port_t. Nothing is cached and no wait is performed here, so
 * every timing requirement stays with the caller. Only the TMP75 addresses
 * 0x48..0x4F are supported; see README.md for the device details and the port
 * contract.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*/
/* External capability interface                                              */
/*----------------------------------------------------------------------------*/

/** @brief Result of a driver operation. */
typedef enum {
    TMP75_OK         = 0,
    TMP75_ERR_ARG    = -1,
    TMP75_ERR_IO     = -2, /**< The platform transfer failed. */
    TMP75_ERR_VERIFY = -3, /**< Read-back value differs from the write. */
    TMP75_ERR_STATE  = -4  /**< One-shot requested while not shut down. */
} tmp75_status_t;

/**
 * @brief Platform I2C operations required by the driver.
 *
 * One call runs one complete transaction:
 *
 *     START, address+W, tx[0..tx_len-1],
 *     repeated START, address+R, rx[0..rx_len-1], STOP
 *
 * rx_len == 0 means a write that stops after the transmit phase. tx_len is
 * always at least 1, because the driver sends the pointer byte first. The
 * address is the 7-bit slave address; the platform generates the R/W bit.
 * TMP75_OK reports an acknowledged transaction, any other value is passed to
 * the caller unchanged.
 */
typedef struct {
    void* ctx; /**< Passed back to transfer. */
    tmp75_status_t (*transfer)(void* ctx,
                               uint8_t address,
                               const uint8_t* tx,
                               size_t tx_len,
                               uint8_t* rx,
                               size_t rx_len);
} tmp75_port_t;

/*----------------------------------------------------------------------------*/
/* Registers                                                                  */
/*----------------------------------------------------------------------------*/

/** @brief 7-bit slave address, 1001 A2 A1 A0. */
typedef enum {
    TMP75_ADDRESS_0 = 0x48, /**< A2=0 A1=0 A0=0 */
    TMP75_ADDRESS_1 = 0x49,
    TMP75_ADDRESS_2 = 0x4A,
    TMP75_ADDRESS_3 = 0x4B,
    TMP75_ADDRESS_4 = 0x4C,
    TMP75_ADDRESS_5 = 0x4D,
    TMP75_ADDRESS_6 = 0x4E,
    TMP75_ADDRESS_7 = 0x4F  /**< A2=1 A1=1 A0=1 */
} tmp75_address_t;

/** @brief Converter resolution selected by R1/R0. */
typedef enum {
    TMP75_RESOLUTION_9_BIT  = 0, /**< 0.5 degC per LSB. */
    TMP75_RESOLUTION_10_BIT = 1, /**< 0.25 degC per LSB. */
    TMP75_RESOLUTION_11_BIT = 2, /**< 0.125 degC per LSB. */
    TMP75_RESOLUTION_12_BIT = 3  /**< 0.0625 degC per LSB. */
} tmp75_resolution_t;

/**
 * @brief Consecutive out-of-limit conversions needed before ALERT acts.
 *
 * The values are the F1/F0 field of the configuration register, so
 * TMP75_FAULT_1 is 1 fault and TMP75_FAULT_4 is 4 faults. The TMP175 decodes
 * the same field differently (1/2/4/6); this driver supports the TMP75 only.
 */
typedef enum {
    TMP75_FAULT_1 = 0, /**< F1=0 F0=0. */
    TMP75_FAULT_2 = 1, /**< F1=0 F0=1. */
    TMP75_FAULT_3 = 2, /**< F1=1 F0=0, 4 faults on a TMP175. */
    TMP75_FAULT_4 = 3  /**< F1=1 F0=1, 6 faults on a TMP175. */
} tmp75_fault_queue_t;

/** @brief Thermostat mode (TM): how ALERT reacts to a limit crossing. */
typedef enum {
    TMP75_THERMOSTAT_COMPARATOR = 0, /**< TM=0, hysteresis, no clearing. */
    TMP75_THERMOSTAT_INTERRUPT  = 1  /**< TM=1, latched, cleared by a read. */
} tmp75_thermostat_mode_t;

/** @brief ALERT output polarity (POL). */
typedef enum {
    TMP75_ALERT_ACTIVE_LOW  = 0, /**< POL=0, ALERT asserts low. */
    TMP75_ALERT_ACTIVE_HIGH = 1  /**< POL=1, ALERT asserts high. */
} tmp75_alert_polarity_t;

/** @brief Alert limit register; the value doubles as its pointer byte. */
typedef enum {
    TMP75_LIMIT_LOW  = 0x02, /**< T_LOW, reset value 75 degC. */
    TMP75_LIMIT_HIGH = 0x03  /**< T_HIGH, reset value 80 degC. */
} tmp75_limit_t;

/*----------------------------------------------------------------------------*/
/* Configuration and object                                                   */
/*----------------------------------------------------------------------------*/

/**
 * @brief Software view of the Configuration register.
 *
 * The OS bit is deliberately absent: it is a command that reads back as 0,
 * not a setting, so it is issued by tmp75_one_shot() instead of being stored
 * here.
 */
typedef struct {
    tmp75_resolution_t resolution;           /**< R1/R0. */
    tmp75_fault_queue_t fault_queue;         /**< F1/F0, consecutive faults. */
    tmp75_thermostat_mode_t thermostat_mode; /**< Comparator or interrupt. */
    tmp75_alert_polarity_t alert_polarity;   /**< POL. */
    uint8_t shutdown;                        /**< SD, 1 = shutdown mode. */
} tmp75_config_t;

/**
 * @brief Driver instance. The caller owns and provides the storage.
 *
 * The object holds the platform transfer and the address and nothing else:
 * no configuration copy and no temperature, so it cannot drift out of step
 * with the device. The members are managed by the driver and must not be
 * modified by the caller.
 */
typedef struct {
    tmp75_port_t port; /**< Platform transfer. */
    uint8_t address;   /**< 7-bit slave address, 0x48..0x4F. */
} tmp75_t;

/*----------------------------------------------------------------------------*/
/* Operations                                                                 */
/*----------------------------------------------------------------------------*/

/**
 * @brief Fill config with the values the device has after a power-up reset.
 *
 * @param config Destination: 9-bit resolution, 1 fault, comparator mode,
 *               active-low ALERT, continuous conversion. NULL is ignored.
 */
void tmp75_default_config(tmp75_config_t* config);

/**
 * @brief Construct a driver object.
 *
 * Copies the port and the address into the object. The device is not
 * accessed, so construction costs no bus transaction.
 *
 * @param dev Caller-owned storage for the object.
 * @param port Platform transfer to copy; its transfer member must not be
 *             NULL.
 * @param address One of TMP75_ADDRESS_0 .. TMP75_ADDRESS_7 (0x48..0x4F).
 * @return TMP75_OK, or TMP75_ERR_ARG for a NULL argument, a missing
 *         transfer callback or an address outside 0x48..0x4F.
 */
tmp75_status_t tmp75_ctor(tmp75_t* dev,
                          const tmp75_port_t* port,
                          tmp75_address_t address);

/**
 * @brief Write the Configuration register and verify the read-back value.
 *
 * The OS bit is written as 0, so the call never starts a conversion as a
 * side effect, and the whole byte can be compared on read-back without
 * masking. The device finishes the conversion in progress before it applies
 * a shutdown, so a caller that needs the device off has to wait one
 * conversion period before relying on it.
 *
 * @param dev Constructed driver object.
 * @param config Settings to write; every field must be in range.
 * @return TMP75_OK, TMP75_ERR_ARG for a NULL or out-of-range argument,
 *         TMP75_ERR_IO when the transfer fails, TMP75_ERR_VERIFY when the
 *         device holds a different byte.
 * @note The read-back clears a latched ALERT in interrupt mode (TM=1).
 */
tmp75_status_t tmp75_write_config(tmp75_t* dev,
                                  const tmp75_config_t* config);

/**
 * @brief Read the Configuration register and decode it into config.
 *
 * @param dev Constructed driver object.
 * @param config Destination for the decoded settings.
 * @return TMP75_OK, TMP75_ERR_ARG for a NULL argument, or TMP75_ERR_IO.
 * @note The read clears a latched ALERT in interrupt mode (TM=1).
 */
tmp75_status_t tmp75_read_config(tmp75_t* dev,
                                 tmp75_config_t* config);

/**
 * @brief Read the temperature register as degrees Celsius.
 *
 * @param dev Constructed driver object.
 * @param celsius Destination, sign-extended 12-bit result scaled by
 *                0.0625 degC.
 * @return TMP75_OK, TMP75_ERR_ARG for a NULL argument, or TMP75_ERR_IO.
 * @note The function does not wait for a conversion. In continuous mode it
 *       returns the most recent completed conversion, and until the first
 *       conversion completes after power-up that is 0 degC. The resolution
 *       stays 0.0625 degC in code, but a 9-bit device only has 0.5 degC of real
 *       information. The read clears a latched ALERT in interrupt mode
 *       (TM=1).
 */
tmp75_status_t tmp75_read_temperature(tmp75_t* dev,
                                      float* celsius);

/**
 * @brief Write an alert limit as degrees Celsius and verify the read-back.
 *
 * @param dev Constructed driver object.
 * @param limit TMP75_LIMIT_LOW or TMP75_LIMIT_HIGH.
 * @param celsius Value to store, -128.0 .. +127.9375 degC, rounded to the
 *                0.0625 degC grid.
 * @return TMP75_OK, TMP75_ERR_ARG for a NULL argument, an unknown limit or a
 *         value outside the 12-bit range, TMP75_ERR_IO when the transfer
 *         fails, TMP75_ERR_VERIFY when the device holds a different value.
 * @note The 12-bit format is narrower than the device operating range of
 *       -40 .. +125 degC at the extremes: +127.9375 degC is the largest value
 *       that can be stored at all. The read-back clears a latched ALERT in
 *       interrupt mode (TM=1).
 */
tmp75_status_t tmp75_write_limit(tmp75_t* dev,
                                 tmp75_limit_t limit,
                                 float celsius);

/**
 * @brief Read an alert limit as degrees Celsius.
 *
 * @param dev Constructed driver object.
 * @param limit TMP75_LIMIT_LOW or TMP75_LIMIT_HIGH.
 * @param celsius Destination, sign-extended 12-bit result scaled by
 *                0.0625 degC.
 * @return TMP75_OK, TMP75_ERR_ARG for a NULL argument or an unknown limit,
 *         or TMP75_ERR_IO.
 * @note The read clears a latched ALERT in interrupt mode (TM=1).
 */
tmp75_status_t tmp75_read_limit(tmp75_t* dev,
                                tmp75_limit_t limit,
                                float* celsius);

/**
 * @brief Start one conversion from shutdown mode.
 *
 * Reads the Configuration register, refuses to proceed unless SD=1, then
 * writes the same byte back with OS=1. The device returns to shutdown when
 * the conversion completes.
 *
 * @param dev Constructed driver object.
 * @return TMP75_OK, TMP75_ERR_ARG for a NULL argument, TMP75_ERR_IO when a
 *         transfer fails, TMP75_ERR_STATE when the device is not shut down.
 * @note Order: write shutdown=1, wait one conversion period for the device
 *       to stop, call this, wait tmp75_conversion_time_ms(resolution), then
 *       read the temperature. A device that is still finishing the
 *       conversion that preceded shutdown is indistinguishable from an idle
 *       one, so that wait is the caller's responsibility. The OS bit reads
 *       back as 0, so it is not verified. The internal register read clears a
 *       latched ALERT in interrupt mode (TM=1).
 */
tmp75_status_t tmp75_one_shot(tmp75_t* dev);

/**
 * @brief Worst-case conversion time for a resolution, in milliseconds.
 *
 * The datasheet maximum (37.5 / 75 / 150 / 300 ms) rounded up to whole
 * milliseconds, so a wait based on it is always long enough. The driver
 * never waits itself; the caller schedules against this value.
 *
 * @param resolution One of the four tmp75_resolution_t values.
 * @return 38, 75, 150 or 300 for 9, 10, 11 or 12 bits, or 0 for any other
 *         value.
 */
uint32_t tmp75_conversion_time_ms(tmp75_resolution_t resolution);

#ifdef __cplusplus
}
#endif

#endif
