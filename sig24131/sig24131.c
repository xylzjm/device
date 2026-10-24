#include "sig24131.h"

/* Commands --------------------------------------------------------------- */

#define SIG24131_CMD_RESET     0x02U
#define SIG24131_CMD_SYNC      0x04U
#define SIG24131_CMD_SLEEP     0x0AU
#define SIG24131_CMD_WAKEUP    0x0CU
#define SIG24131_CMD_READ_REG  0x40U
#define SIG24131_CMD_WRITE_REG 0x20U
#define SIG24131_CMD_READ_DATA 0x60U

/* Frame layout ----------------------------------------------------------- */

/*
 * Every operation starts with one command byte. Register accesses append 24
 * bits of register data; a conversion read appends 24 bits of conversion data.
 */

#define SIG24131_DATA_BYTES 3U

/** Widest register address the 5-bit address field of a command can carry. */
#define SIG24131_ADDRESS_MAX 0x1FU

/* Platform operations ---------------------------------------------------- */

static sig24131_status_t begin_frame(sig24131_t* dev)
{
    if ( dev == NULL || dev->port.cs_assert == NULL ) {
        return SIG24131_ERR_ARG;
    }
    return dev->port.cs_assert(dev->port.ctx);
}

static sig24131_status_t end_frame(sig24131_t* dev)
{
    if ( dev == NULL || dev->port.cs_release == NULL ) {
        return SIG24131_ERR_ARG;
    }
    return dev->port.cs_release(dev->port.ctx);
}

static sig24131_status_t transfer(sig24131_t* dev,
                                  const uint8_t* tx,
                                  uint8_t* rx,
                                  size_t len)
{
    if ( dev == NULL
         || dev->port.spi_transfer == NULL
         || tx == NULL || rx == NULL ) {
        return SIG24131_ERR_ARG;
    }
    return dev->port.spi_transfer(dev->port.ctx, tx, rx, len);
}

/**
 * @brief Run one complete SPI frame.
 *
 * CSn is asserted for the whole transfer and released afterwards. A failing
 * CSn release is reported only when the transfer itself succeeded, so the more
 * specific error survives.
 */
static sig24131_status_t transact(sig24131_t* dev,
                                  const uint8_t* tx,
                                  uint8_t* rx,
                                  size_t len)
{
    sig24131_status_t status;

    status = begin_frame(dev);
    if ( status != SIG24131_OK ) {
        return status;
    }
    status = transfer(dev, tx, rx, len);
    if ( end_frame(dev) != SIG24131_OK && status == SIG24131_OK ) {
        status = SIG24131_ERR_IO;
    }
    return status;
}

/** @brief Send one of the single-byte commands. */
static sig24131_status_t send_command(sig24131_t* dev,
                                      uint8_t command)
{
    uint8_t tx[1] = {command};
    uint8_t rx[1] = {0U};

    return transact(dev, tx, rx, sizeof(tx));
}

/* 24-bit MSB-first helpers ----------------------------------------------- */

static uint32_t get_u24(const uint8_t* data)
{
    return ((uint32_t)data[0] << 16)
           | ((uint32_t)data[1] << 8)
           | (uint32_t)data[2];
}

static void put_u24(uint8_t* data,
                    uint32_t value)
{
    data[0] = (uint8_t)(value >> 16);
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)value;
}

/* Register encoding ------------------------------------------------------ */

/** CONF0 bits a write can set. Bit 23 reports the clock source instead. */
#define SIG24131_CONF0_WRITABLE 0x7FFFFFUL

/** CONF0 bit 15. Above gain 2 the device ignores what is written here. */
#define SIG24131_CONF0_BUF 0x008000UL

/** @brief CONF0 bits from config. */
static uint32_t encode_conf0(const sig24131_config_t* config)
{
    uint32_t value = 0U;

    /* Bit 22 (DATACHK) stays 0: the vendor advises against the data checksum. */
    value |= (uint32_t)(config->vbias_enable & 1U) << 19;
    value |= (uint32_t)(config->fast_startup & 1U) << 18;
    value |= (uint32_t)(config->internal_reference_enable & 1U) << 16;
    value |= (uint32_t)(config->input_buffer_enable & 1U) << 15;
    value |= (uint32_t)(config->conv_delay & 7U) << 12;
    value |= (uint32_t)(config->burnout_enable & 1U) << 11;
    value |= (uint32_t)(config->idac_current & 7U) << 8;
    value |= (uint32_t)(config->idac_pair_enable & 1U) << 7;
    value |= (uint32_t)(config->idac_external_reference & 1U) << 6;
    value |= ((uint32_t)config->idac2_mux & 7U) << 3;
    value |= (uint32_t)config->idac1_mux & 7U;
    return value;
}

/** @brief CONF1 bits from config. */
static uint32_t encode_conf1(const sig24131_config_t* config)
{
    uint32_t value = 0U;

    value |= (uint32_t)(config->latency & 1U) << 23;
    value |= (uint32_t)(config->single_shot & 1U) << 22;
    value |= (uint32_t)(config->reference & 3U) << 20;
    value |= (uint32_t)(config->gain & 0x0FU) << 16;
    value |= (uint32_t)(config->filter & 1U) << 15;
    value |= (uint32_t)(config->reject_60hz & 1U) << 14;
    value |= (uint32_t)(config->data_rate & 0x3FU) << 8;
    value |= ((uint32_t)config->mux_p & 0x0FU) << 4;
    value |= (uint32_t)config->mux_n & 0x0FU;
    return value;
}

/** @brief Fill the CONF0 members of config from a register value. */
static void decode_conf0(uint32_t value,
                         sig24131_config_t* config)
{
    config->vbias_enable              = (uint8_t)((value >> 19) & 1U);
    config->fast_startup              = (uint8_t)((value >> 18) & 1U);
    config->internal_reference_enable = (uint8_t)((value >> 16) & 1U);
    config->input_buffer_enable       = (uint8_t)((value >> 15) & 1U);
    config->conv_delay                = (sig24131_conv_delay_t)((value >> 12) & 7U);
    config->burnout_enable            = (uint8_t)((value >> 11) & 1U);
    config->idac_current              = (sig24131_idac_current_t)((value >> 8) & 7U);
    config->idac_pair_enable          = (uint8_t)((value >> 7) & 1U);
    config->idac_external_reference   = (uint8_t)((value >> 6) & 1U);
    config->idac2_mux                 = (sig24131_idac_mux_t)((value >> 3) & 7U);
    config->idac1_mux                 = (sig24131_idac_mux_t)(value & 7U);
}

/** @brief Fill the CONF1 members of config from a register value. */
static void decode_conf1(uint32_t value,
                         sig24131_config_t* config)
{
    config->latency     = (uint8_t)((value >> 23) & 1U);
    config->single_shot = (uint8_t)((value >> 22) & 1U);
    config->reference   = (sig24131_reference_t)((value >> 20) & 3U);
    config->gain        = (sig24131_gain_t)((value >> 16) & 0x0FU);
    config->filter      = (sig24131_filter_t)((value >> 15) & 1U);
    config->reject_60hz = (uint8_t)((value >> 14) & 1U);
    config->data_rate   = (sig24131_data_rate_t)((value >> 8) & 0x3FU);
    config->mux_p       = (sig24131_mux_t)((value >> 4) & 0x0FU);
    config->mux_n       = (sig24131_mux_t)(value & 0x0FU);
}

/* Device configuration --------------------------------------------------- */

void sig24131_default_config(sig24131_config_t* config)
{
    if ( config == NULL ) {
        return;
    }

    /* Register reset values plus the settings a caller is expected to
     * overwrite for its own signal chain. */
    config->gain                      = SIG24131_GAIN_1;
    config->filter                    = SIG24131_FILTER_SINC1;
    config->reference                 = SIG24131_REF_EXTERNAL;
    config->data_rate                 = SIG24131_RATE_10;
    config->conv_delay                = SIG24131_DELAY_0_US;
    config->idac_current              = SIG24131_IDAC_OFF;
    config->mux_p                     = SIG24131_MUX_AIN0;
    config->mux_n                     = SIG24131_MUX_AIN1;
    config->idac1_mux                 = SIG24131_IDAC_MUX_AIN0;
    config->idac2_mux                 = SIG24131_IDAC_MUX_AIN0;
    config->input_buffer_enable       = 0U;
    config->internal_reference_enable = 0U;
    config->vbias_enable              = 0U;
    config->fast_startup              = 0U;
    config->burnout_enable            = 0U;
    config->idac_pair_enable          = 0U;
    config->idac_external_reference   = 0U;
    config->latency                   = 0U;
    config->single_shot               = 0U;
    config->reject_60hz               = 0U;
}

sig24131_status_t sig24131_ctor(sig24131_t* dev,
                                const sig24131_port_t* port)
{
    if ( dev == NULL || port == NULL
         || port->spi_transfer == NULL
         || port->cs_assert == NULL
         || port->cs_release == NULL ) {
        return SIG24131_ERR_ARG;
    }
    dev->port = *port;
    return SIG24131_OK;
}

sig24131_status_t sig24131_reset(sig24131_t* dev)
{
    return send_command(dev, SIG24131_CMD_RESET);
}

sig24131_status_t sig24131_sync(sig24131_t* dev)
{
    return send_command(dev, SIG24131_CMD_SYNC);
}

sig24131_status_t sig24131_sleep(sig24131_t* dev)
{
    return send_command(dev, SIG24131_CMD_SLEEP);
}

sig24131_status_t sig24131_wakeup(sig24131_t* dev)
{
    return send_command(dev, SIG24131_CMD_WAKEUP);
}

/* Register access -------------------------------------------------------- */

/** Configuration register addresses carried in the 5-bit command field. */
typedef enum {
    SIG24131_REG_CONF0 = 0x01,
    SIG24131_REG_CONF1 = 0x02
} sig24131_reg_t;

/**
 * @brief Write one 24-bit register.
 *
 * Deliberately not public: every register write goes through the configuration
 * path below, which is what keeps the cached copy in step with the device.
 */
static sig24131_status_t write_register(sig24131_t* dev,
                                        sig24131_reg_t address,
                                        uint32_t value)
{
    uint8_t tx[1U + SIG24131_DATA_BYTES];
    uint8_t rx[1U + SIG24131_DATA_BYTES];

    if ( dev == NULL
         || (uint32_t)address > SIG24131_ADDRESS_MAX
         || value > 0xFFFFFFU ) {
        return SIG24131_ERR_ARG;
    }
    tx[0] = (uint8_t)(SIG24131_CMD_WRITE_REG | (uint8_t)address);
    put_u24(&tx[1], value);
    return transact(dev, tx, rx, sizeof(tx));
}

/** Read one 24-bit register. */
static sig24131_status_t read_register(sig24131_t* dev,
                                       sig24131_reg_t address,
                                       uint32_t* value)
{
    uint8_t tx[1U + SIG24131_DATA_BYTES] = {0U};
    uint8_t rx[1U + SIG24131_DATA_BYTES] = {0U};
    sig24131_status_t status;

    if ( dev == NULL || value == NULL
         || (uint32_t)address > SIG24131_ADDRESS_MAX ) {
        return SIG24131_ERR_ARG;
    }
    tx[0]  = (uint8_t)(SIG24131_CMD_READ_REG | (uint8_t)address);
    status = transact(dev, tx, rx, sizeof(tx));
    if ( status == SIG24131_OK ) {
        *value = get_u24(&rx[1]);
    }
    return status;
}

sig24131_status_t sig24131_write_config(sig24131_t* dev,
                                        const sig24131_config_t* config)
{
    uint32_t expected_conf0;
    uint32_t expected_conf1;
    uint32_t read_conf0;
    uint32_t read_conf1;
    uint32_t care;
    sig24131_status_t status;

    if ( dev == NULL || config == NULL ) {
        return SIG24131_ERR_ARG;
    }

    expected_conf0 = encode_conf0(config);
    expected_conf1 = encode_conf1(config);

    /* CONF1 goes last: it carries the multiplexer, the gain and the data rate,
     * so the conversion that its write restarts already uses the new setup. */
    status = write_register(dev, SIG24131_REG_CONF0, expected_conf0);
    if ( status == SIG24131_OK ) {
        status = write_register(dev, SIG24131_REG_CONF1, expected_conf1);
    }
    if ( status != SIG24131_OK ) {
        return status;
    }

    /* WREG is not acknowledged, so reading the registers back is the only way to
     * know that the device accepted the configuration. */
    status = read_register(dev, SIG24131_REG_CONF0, &read_conf0);
    if ( status == SIG24131_OK ) {
        status = read_register(dev, SIG24131_REG_CONF1, &read_conf1);
    }
    if ( status != SIG24131_OK ) {
        return status;
    }

    /* Bit 23 cannot be written and the input buffer bit is not trustworthy above
     * gain 2, so neither is compared. */
    care = SIG24131_CONF0_WRITABLE;
    if ( config->gain > SIG24131_GAIN_2 ) {
        care &= ~SIG24131_CONF0_BUF;
    }
    if ( ((read_conf0 ^ expected_conf0) & care) != 0U
         || read_conf1 != expected_conf1 ) {
        return SIG24131_ERR_VERIFY;
    }
    return SIG24131_OK;
}

sig24131_status_t sig24131_read_config(sig24131_t* dev,
                                       sig24131_config_t* config)
{
    uint32_t conf0;
    uint32_t conf1;
    sig24131_status_t status;

    if ( dev == NULL || config == NULL ) {
        return SIG24131_ERR_ARG;
    }
    status = read_register(dev, SIG24131_REG_CONF0, &conf0);
    if ( status != SIG24131_OK ) {
        return status;
    }
    status = read_register(dev, SIG24131_REG_CONF1, &conf1);
    if ( status != SIG24131_OK ) {
        return status;
    }

    /* The two decoders between them cover every member of the structure, so the
     * caller's storage does not have to be initialised first. */
    decode_conf0(conf0, config);
    decode_conf1(conf1, config);
    return SIG24131_OK;
}

/* Conversion data -------------------------------------------------------- */

sig24131_status_t sig24131_read_data(sig24131_t* dev,
                                     int32_t* code)
{
    uint8_t tx[1U + SIG24131_DATA_BYTES] = {SIG24131_CMD_READ_DATA};
    uint8_t rx[1U + SIG24131_DATA_BYTES] = {0U};
    uint32_t raw;
    sig24131_status_t status;

    if ( dev == NULL || code == NULL ) {
        return SIG24131_ERR_ARG;
    }

    status = transact(dev, tx, rx, sizeof(tx));
    if ( status != SIG24131_OK ) {
        return status;
    }

    /* Sign-extend the 24-bit two's-complement result without relying on an
     * implementation-defined unsigned-to-signed conversion. */
    raw   = get_u24(&rx[1]);
    *code = (int32_t)raw;
    if ( (raw & 0x00800000UL) != 0U ) {
        *code = (int32_t)(*code - 0x01000000L);
    }
    return SIG24131_OK;
}

/* Data rates ------------------------------------------------------------- */

typedef struct {
    sig24131_data_rate_t code;
    uint32_t millihz;
} data_rate_entry_t;

static const data_rate_entry_t data_rate_table[] = {
    {SIG24131_RATE_2_5, 2500U},
    {SIG24131_RATE_5, 5000U},
    {SIG24131_RATE_10, 10000U},
    {SIG24131_RATE_12_5, 12500U},
    {SIG24131_RATE_15, 15000U},
    {SIG24131_RATE_20, 20000U},
    {SIG24131_RATE_25, 25000U},
    {SIG24131_RATE_30, 30000U},
    {SIG24131_RATE_40, 40000U},
    {SIG24131_RATE_50, 50000U},
    {SIG24131_RATE_60, 60000U},
    {SIG24131_RATE_80, 80000U},
    {SIG24131_RATE_100, 100000U},
    {SIG24131_RATE_120, 120000U},
    {SIG24131_RATE_160, 160000U},
    {SIG24131_RATE_200, 200000U},
    {SIG24131_RATE_240, 240000U},
    {SIG24131_RATE_250, 250000U},
    {SIG24131_RATE_300, 300000U},
    {SIG24131_RATE_400, 400000U},
    {SIG24131_RATE_480, 480000U},
    {SIG24131_RATE_500, 500000U},
    {SIG24131_RATE_600, 600000U},
    {SIG24131_RATE_800, 800000U},
    {SIG24131_RATE_1000, 1000000U},
    {SIG24131_RATE_1200, 1200000U},
    {SIG24131_RATE_2000, 2000000U},
    {SIG24131_RATE_2400, 2400000U},
    {SIG24131_RATE_4000, 4000000U},
    {SIG24131_RATE_4800, 4800000U},
    {SIG24131_RATE_8000, 8000000U},
    {SIG24131_RATE_9600, 9600000U},
};

sig24131_status_t sig24131_data_rate_from_millihz(uint32_t rate_millihz,
                                                  sig24131_data_rate_t* data_rate)
{
    size_t i;

    if ( data_rate == NULL || rate_millihz == 0U ) {
        return SIG24131_ERR_ARG;
    }

    for ( i = 0U; i < sizeof(data_rate_table) / sizeof(data_rate_table[0]); i++ ) {
        if ( data_rate_table[i].millihz == rate_millihz ) {
            *data_rate = data_rate_table[i].code;
            return SIG24131_OK;
        }
    }
    return SIG24131_ERR_ARG;
}
