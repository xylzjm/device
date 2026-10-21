#ifndef SIG24131_H
#define SIG24131_H

/**
 * @file sig24131.h
 * @brief MCU-independent driver interface for the SIG24131 ADC.
 *
 * The driver does not include any MCU, RTOS, or vendor SDK header.  The
 * application supplies the required platform operations through
 * sig24131_port_t when calling sig24131_init().
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* External capability interface ------------------------------------------ */

/** @brief Result of a driver operation. */
typedef enum {
    SIG24131_OK      = 0,
    SIG24131_ERR_ARG = -1,
    SIG24131_ERR_IO  = -2
} sig24131_status_t;

/**
 * @brief Platform operations required by the driver.
 *
 * The SPI transfer must be full-duplex and transfer exactly len bytes.
 * If the platform controls CSn inside the SPI transfer, cs_assert and
 * cs_release may be NULL.  delay_us is optional; when it is NULL, the
 * driver performs no software delay.
 */
typedef struct {
    void* ctx;
    sig24131_status_t (*spi_transfer)(void* ctx,
                                      const uint8_t* tx,
                                      uint8_t* rx,
                                      size_t len);
    sig24131_status_t (*cs_assert)(void* ctx);
    sig24131_status_t (*cs_release)(void* ctx);
    sig24131_status_t (*delay_us)(void* ctx,
                                  uint32_t us);
} sig24131_port_t;

/* Register addresses ------------------------------------------------------ */

#define SIG24131_REG_CONF0  0x01U
#define SIG24131_REG_CONF1  0x02U
#define SIG24131_REG_OFFSET 0x03U
#define SIG24131_REG_GAIN   0x04U

/** @brief Calibration operation selector. */
typedef enum {
    SIG24131_CAL_SELF_OFFSET   = 0,
    SIG24131_CAL_SYSTEM_OFFSET = 1,
    SIG24131_CAL_SYSTEM_GAIN   = 2
} sig24131_calibration_t;

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
    SIG24131_REF_EXTERNAL      = 0,
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
 * @brief SIG24131 configuration.
 *
 * mux_p and mux_n contain the CONF1 input multiplexer values.  idac1_mux and
 * idac2_mux contain the corresponding CONF0 IDAC routing values.
 */
typedef struct {
    sig24131_gain_t gain;
    sig24131_filter_t filter;
    sig24131_reference_t reference;
    sig24131_data_rate_t data_rate;
    sig24131_conv_delay_t conv_delay;
    sig24131_idac_current_t idac_current;
    uint8_t mux_p;
    uint8_t mux_n;
    uint8_t idac1_mux;
    uint8_t idac2_mux;
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

/** @brief Driver instance. The caller owns and provides the storage. */
typedef struct {
    sig24131_port_t port;
    sig24131_config_t config;
} sig24131_t;

/** @brief ADC sample bytes in MSB-first order. */
typedef struct {
    uint8_t data[3];
} sig24131_sample_t;

/** Fill config with the driver's reset-equivalent default configuration. */
void sig24131_default_config(sig24131_config_t* config);

/** Initialize a driver instance and bind its platform operations. */
sig24131_status_t sig24131_init(sig24131_t* dev,
                                const sig24131_port_t* port);

/** Reset the ADC and wait for the required power-on/reset settling time. */
sig24131_status_t sig24131_reset(sig24131_t* dev);

/** Restart/synchronize ADC conversion timing. */
sig24131_status_t sig24131_sync(sig24131_t* dev);

/** Enter low-power sleep mode. */
sig24131_status_t sig24131_sleep(sig24131_t* dev);

/** Leave low-power sleep mode. */
sig24131_status_t sig24131_wakeup(sig24131_t* dev);

/**
 * Start one calibration operation.
 *
 * The analog inputs must be prepared according to the selected operation:
 * self offset shorts the ADC inputs internally, system offset requires a null
 * input, and system gain requires a full-scale input.
 */
sig24131_status_t sig24131_calibrate(sig24131_t* dev,
                                     sig24131_calibration_t calibration);

/** Write one 24-bit register. */
sig24131_status_t sig24131_write_register(sig24131_t* dev,
                                          uint8_t address,
                                          uint32_t value);

/** Read one 24-bit register. */
sig24131_status_t sig24131_read_register(sig24131_t* dev,
                                         uint8_t address,
                                         uint32_t* value);

/** Write CONF0 and CONF1 from config and update the cached configuration. */
sig24131_status_t sig24131_write_config(sig24131_t* dev,
                                        const sig24131_config_t* config);

/** Read CONF0 and CONF1 into config and update the cached configuration. */
sig24131_status_t sig24131_read_config(sig24131_t* dev,
                                       sig24131_config_t* config);

/**
 * Read the latest ADC conversion through the safe command-read method.
 *
 * The returned sample contains the 24-bit conversion code, MSB first.
 */
sig24131_status_t sig24131_read_data(sig24131_t* dev,
                                     sig24131_sample_t* sample);

/** Convert an ADC sample to a signed 32-bit two's-complement value. */
int32_t sig24131_code_to_int32(const sig24131_sample_t* sample);

#ifdef __cplusplus
}
#endif

#endif
