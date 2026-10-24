#ifndef SIG24131_H
#define SIG24131_H

/**
 * @file sig24131.h
 * @brief MCU-independent driver interface for the SIG24131 ADC.
 *
 * The driver does not include any MCU, RTOS, or vendor SDK header. The
 * application supplies the required platform operations through
 * sig24131_port_t when calling sig24131_ctor().
 *
 * The driver keeps no copy of the device registers: every question about the
 * device is answered by the bus, so the driver can never disagree with the
 * hardware. sig24131_ctor() only builds the object in RAM and never touches the
 * device.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* External capability interface ------------------------------------------ */

/** @brief Result of a driver operation. */
typedef enum {
    SIG24131_OK         = 0,
    SIG24131_ERR_ARG    = -1,
    SIG24131_ERR_IO     = -2,
    SIG24131_ERR_VERIFY = -3 /**< Read back configuration differs from the write. */
} sig24131_status_t;

/**
 * @brief Platform operations required by the driver.
 *
 * The SPI transfer must be full-duplex and transfer exactly len bytes. All three
 * operations are required: sig24131_ctor() rejects a port that leaves any of
 * them NULL. A platform that controls CSn inside its SPI transfer can still
 * supply cs_assert/cs_release callbacks that do nothing.
 */
typedef struct {
    void* ctx;
    sig24131_status_t (*spi_transfer)(void* ctx,
                                      const uint8_t* tx,
                                      uint8_t* rx,
                                      size_t len);
    sig24131_status_t (*cs_assert)(void* ctx);
    sig24131_status_t (*cs_release)(void* ctx);
} sig24131_port_t;

/* Registers --------------------------------------------------------------- */

/* Calibration is not supported yet. The self/system offset and system gain
 * commands would only be useful together with a data-ready wait and a known
 * input stimulus, which the driver does not provide. The offset and gain
 * calibration registers are kept out of this API for the same reason: a
 * register write alone does not calibrate anything, because the value has to be
 * derived from a measured result first. */

/** @brief PGA gain selection. */
typedef enum {
    SIG24131_GAIN_1   = 0,
    SIG24131_GAIN_2   = 1,
    SIG24131_GAIN_4   = 2,
    SIG24131_GAIN_8   = 3,
    SIG24131_GAIN_16  = 4,
    SIG24131_GAIN_32  = 5,
    SIG24131_GAIN_64  = 6,
    SIG24131_GAIN_128 = 7,
    SIG24131_GAIN_256 = 8
} sig24131_gain_t;

/** @brief ADC digital filter selection. */
typedef enum {
    SIG24131_FILTER_SINC1 = 0,
    SIG24131_FILTER_SINC4 = 1
} sig24131_filter_t;

/** @brief ADC reference source selection. */
typedef enum {
    SIG24131_REF_EXTERNAL      = 0, /**< REFP and REFN. */
    SIG24131_REF_INTERNAL_2V5  = 2,
    SIG24131_REF_INTERNAL_1V17 = 3
} sig24131_reference_t;

/** @brief Output data-rate code. */
typedef enum {
    SIG24131_RATE_2_5  = 0x00,
    SIG24131_RATE_5    = 0x01,
    SIG24131_RATE_10   = 0x02,
    SIG24131_RATE_12_5 = 0x03,
    SIG24131_RATE_15   = 0x04,
    SIG24131_RATE_20   = 0x05,
    SIG24131_RATE_25   = 0x06,
    SIG24131_RATE_30   = 0x07,
    SIG24131_RATE_40   = 0x08,
    SIG24131_RATE_50   = 0x09,
    SIG24131_RATE_60   = 0x0A,
    SIG24131_RATE_80   = 0x0B,
    SIG24131_RATE_100  = 0x0C,
    SIG24131_RATE_120  = 0x0D,
    SIG24131_RATE_160  = 0x0E,
    SIG24131_RATE_200  = 0x0F,
    SIG24131_RATE_240  = 0x10,
    SIG24131_RATE_250  = 0x31,
    SIG24131_RATE_300  = 0x11,
    SIG24131_RATE_400  = 0x12,
    SIG24131_RATE_480  = 0x13,
    SIG24131_RATE_500  = 0x34,
    SIG24131_RATE_600  = 0x14,
    SIG24131_RATE_800  = 0x15,
    SIG24131_RATE_1000 = 0x36,
    SIG24131_RATE_1200 = 0x16,
    SIG24131_RATE_2000 = 0x37,
    SIG24131_RATE_2400 = 0x17,
    SIG24131_RATE_4000 = 0x38,
    SIG24131_RATE_4800 = 0x18,
    SIG24131_RATE_8000 = 0x39,
    SIG24131_RATE_9600 = 0x19
} sig24131_data_rate_t;

/** @brief Optional conversion-start delay selection. */
typedef enum {
    SIG24131_DELAY_0_US     = 0,
    SIG24131_DELAY_104_US   = 1,
    SIG24131_DELAY_417_US   = 2,
    SIG24131_DELAY_1_667_MS = 3,
    SIG24131_DELAY_6_667_MS = 4,
    SIG24131_DELAY_26_67_MS = 5,
    SIG24131_DELAY_106_7_MS = 6,
    SIG24131_DELAY_426_7_MS = 7
} sig24131_conv_delay_t;

/** @brief IDAC output current selection. */
typedef enum {
    SIG24131_IDAC_OFF     = 0,
    SIG24131_IDAC_10_UA   = 1,
    SIG24131_IDAC_50_UA   = 2,
    SIG24131_IDAC_100_UA  = 3,
    SIG24131_IDAC_200_UA  = 4,
    SIG24131_IDAC_250_UA  = 5,
    SIG24131_IDAC_500_UA  = 6,
    SIG24131_IDAC_1000_UA = 7
} sig24131_idac_current_t;

/**
 * @brief Input multiplexer selection for MUXP[3:0] and MUXN[3:0].
 *
 * Codes 0x9..0xE mean slightly different things depending on the side they are
 * programmed into; the comments below give the MUXP meaning first and the MUXN
 * meaning second where the two differ.
 */
typedef enum {
    SIG24131_MUX_AIN0   = 0x0,
    SIG24131_MUX_AIN1   = 0x1,
    SIG24131_MUX_AIN2   = 0x2,
    SIG24131_MUX_AIN3   = 0x3,
    SIG24131_MUX_AIN4   = 0x4, /**< SIG24131 and SIG24132 only. */
    SIG24131_MUX_AIN5   = 0x5, /**< SIG24131 and SIG24132 only. */
    SIG24131_MUX_AIN6   = 0x6, /**< SIG24131 and SIG24132 only. */
    SIG24131_MUX_AIN7   = 0x7, /**< SIG24131 and SIG24132 only. */
    SIG24131_MUX_AINCOM = 0x8,
    SIG24131_MUX_TEMP   = 0x9, /**< Internal sensor, positive and negative. */
    SIG24131_MUX_REF    = 0xA, /**< REFP on MUXP, REFN on MUXN. */
    SIG24131_MUX_AVSS   = 0xB,
    SIG24131_MUX_DVSS   = 0xC,
    SIG24131_MUX_AVDD_4 = 0xD, /**< (AVDD - AVSS) / 4, + on MUXP, - on MUXN. */
    SIG24131_MUX_DVDD_4 = 0xE  /**< (DVDD - DVSS) / 4, + on MUXP, - on MUXN. */
} sig24131_mux_t;

/** @brief IDAC output routing for IDAC1_MUX[2:0] and IDAC2_MUX[2:0]. */
typedef enum {
    SIG24131_IDAC_MUX_AIN0 = 0,
    SIG24131_IDAC_MUX_AIN1 = 1,
    SIG24131_IDAC_MUX_AIN2 = 2,
    SIG24131_IDAC_MUX_AIN3 = 3,
    SIG24131_IDAC_MUX_AIN4 = 4, /**< SIG24131 and SIG24132 only. */
    SIG24131_IDAC_MUX_AIN5 = 5, /**< SIG24131 and SIG24132 only. */
    SIG24131_IDAC_MUX_AIN6 = 6, /**< SIG24131 and SIG24132 only. */
    SIG24131_IDAC_MUX_AIN7 = 7  /**< SIG24131 and SIG24132 only. */
} sig24131_idac_mux_t;

/**
 * @brief SIG24131 configuration.
 *
 * mux_p and mux_n are the CONF1 input multiplexer values, idac1_mux and
 * idac2_mux the CONF0 IDAC routing values.
 *
 * input_buffer_enable is honoured only at gain 1 and 2; at higher gains the
 * device enables the input buffer regardless of the bit, so leaving it 0 there
 * is harmless but also has no effect.
 *
 * The data checksum is not exposed: the vendor advises against enabling it, so
 * the driver always leaves that bit cleared.
 */
typedef struct {
    sig24131_gain_t gain;
    sig24131_filter_t filter;
    sig24131_reference_t reference;
    sig24131_data_rate_t data_rate;
    sig24131_conv_delay_t conv_delay;
    sig24131_idac_current_t idac_current;
    sig24131_mux_t mux_p;
    sig24131_mux_t mux_n;
    sig24131_idac_mux_t idac1_mux;
    sig24131_idac_mux_t idac2_mux;
    uint8_t input_buffer_enable;
    uint8_t internal_reference_enable;
    uint8_t vbias_enable;
    uint8_t fast_startup;
    uint8_t burnout_enable;
    uint8_t idac_pair_enable;
    uint8_t idac_external_reference;
    uint8_t latency;
    uint8_t single_shot;
    uint8_t reject_60hz;
} sig24131_config_t;

/**
 * @brief Driver instance. The caller owns and provides the storage.
 *
 * The object holds the platform operations and nothing else, so it cannot drift
 * out of step with the hardware. The member is managed by the driver and must
 * not be modified by the caller.
 */
typedef struct {
    sig24131_port_t port;
} sig24131_t;

/** Fill config with the driver's reset-equivalent default configuration. */
void sig24131_default_config(sig24131_config_t* config);

/**
 * @brief Construct a driver object.
 *
 * Copies the platform operations into the object. The device is not accessed,
 * so the object is a pure RAM construct; the caller still has to reset or
 * configure the device before the first conversion is meaningful.
 *
 * Returns SIG24131_ERR_ARG when dev or port is NULL or when any of the three
 * port operations is missing.
 */
sig24131_status_t sig24131_ctor(sig24131_t* dev,
                                const sig24131_port_t* port);

/**
 * @brief Settling time the device needs after a reset, in milliseconds.
 *
 * No register may be written or read before it has elapsed. The driver does not
 * wait, so a caller that resets several devices on one bus can send every
 * command first and wait once afterwards.
 */
#define SIG24131_RESET_DELAY_MS 43U

/**
 * @brief Send the reset command.
 *
 * The registers return to their reset values and the device needs
 * SIG24131_RESET_DELAY_MS before it is usable again; see the note on that
 * constant.
 */
sig24131_status_t sig24131_reset(sig24131_t* dev);

/**
 * @brief Restart the conversion.
 *
 * The digital filter is reset and a new conversion starts, so the result that
 * was in progress is discarded. Use it to align the conversion phase after a
 * sleep or to drop a sample taken during a disturbance.
 */
sig24131_status_t sig24131_sync(sig24131_t* dev);

/** Enter low-power sleep mode. */
sig24131_status_t sig24131_sleep(sig24131_t* dev);

/** Leave low-power sleep mode. */
sig24131_status_t sig24131_wakeup(sig24131_t* dev);

/**
 * @brief Write CONF0 and CONF1 and verify that the device stored them.
 *
 * A write to either configuration register resets the digital filter and starts
 * a new conversion, so every successful call discards the conversion that was
 * in progress and the caller has to wait for settled data again before the next
 * sample is meaningful.
 *
 * WREG has no acknowledge, so the driver reads both registers back and compares
 * them with what it sent. SIG24131_ERR_VERIFY means the device holds something
 * else.
 */
sig24131_status_t sig24131_write_config(sig24131_t* dev,
                                        const sig24131_config_t* config);

/**
 * @brief Read CONF0 and CONF1 from the device and decode them into config.
 *
 * Costs two SPI register reads. Use it to inspect a device whose configuration
 * is not known, for example after a supply event that may have reset it.
 */
sig24131_status_t sig24131_read_config(sig24131_t* dev,
                                       sig24131_config_t* config);

/**
 * @brief Read the latest ADC conversion as a signed 24-bit code.
 *
 * The 24-bit two's-complement result is sign-extended into *code, so a zero
 * differential input reads as 0 and both rails read as +-8388608.
 *
 * The function does not wait for new data: the application polls it at the
 * configured data rate. A poll that arrives before the next conversion returns
 * the previous result again, which the caller has to tolerate or filter.
 */
sig24131_status_t sig24131_read_data(sig24131_t* dev,
                                     int32_t* code);

/**
 * @brief Data-rate code that produces a nominal output rate.
 *
 * Returns SIG24131_OK and stores the code in *data_rate when rate_millihz is
 * one of the defined rates. Returns SIG24131_ERR_ARG when data_rate is NULL,
 * rate_millihz is zero, or no code has that nominal rate.
 *
 * The nominal conversion period is 1000000 / rate_millihz milliseconds; the
 * filter and the latency bit can stretch it further.
 */
sig24131_status_t sig24131_data_rate_from_millihz(uint32_t rate_millihz,
                                                  sig24131_data_rate_t* data_rate);

#ifdef __cplusplus
}
#endif

#endif
