#include "sig24131.h"

/* Internal commands and timing ------------------------------------------- */
#define SIG24131_CMD_RESET 0x02U
#define SIG24131_CMD_SYNC 0x04U
#define SIG24131_CMD_SLEEP 0x0AU
#define SIG24131_CMD_WAKEUP 0x0CU
#define SIG24131_CMD_SYSOCAL 0x10U
#define SIG24131_CMD_SYSGCAL 0x11U
#define SIG24131_CMD_SELFOCAL 0x12U
#define SIG24131_CMD_READ_REG 0x40U
#define SIG24131_CMD_WRITE_REG 0x20U
#define SIG24131_CMD_READ_DATA 0x60U

#define SIG24131_POR_DELAY_US 43000U

/* SPI transaction and platform-operation helpers ------------------------- */
static sig24131_status_t transfer(sig24131_t *dev, const uint8_t *tx,
                                  uint8_t *rx, size_t len)
{
    if (dev == NULL || dev->port.spi_transfer == NULL || tx == NULL || rx == NULL)
    {
        return SIG24131_ERR_ARG;
    }
    return dev->port.spi_transfer(dev->port.ctx, tx, rx, len);
}

static sig24131_status_t begin_frame(sig24131_t *dev)
{
    if (dev == NULL)
    {
        return SIG24131_ERR_ARG;
    }
    return (dev->port.cs_assert != NULL) ? dev->port.cs_assert(dev->port.ctx)
                                         : SIG24131_OK;
}

static sig24131_status_t end_frame(sig24131_t *dev)
{
    if (dev == NULL)
    {
        return SIG24131_ERR_ARG;
    }
    return (dev->port.cs_release != NULL) ? dev->port.cs_release(dev->port.ctx)
                                          : SIG24131_OK;
}

static sig24131_status_t delay(sig24131_t *dev, uint32_t us)
{
    return (dev->port.delay_us != NULL) ? dev->port.delay_us(dev->port.ctx, us)
                                        : SIG24131_OK;
}

static sig24131_status_t command_only(sig24131_t *dev, uint8_t command)
{
    uint8_t tx[1] = {command};
    uint8_t rx[1] = {0U};
    sig24131_status_t status = begin_frame(dev);
    if (status != SIG24131_OK)
    {
        return status;
    }
    status = transfer(dev, tx, rx, 1U);
    if (end_frame(dev) != SIG24131_OK && status == SIG24131_OK)
    {
        status = SIG24131_ERR_IO;
    }
    return status;
}

/* 24-bit MSB-first byte conversion helpers ------------------------------- */
static uint32_t u24_from_bytes(const uint8_t *data)
{
    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) |
           (uint32_t)data[2];
}

static void u24_to_bytes(uint32_t value, uint8_t *data)
{
    data[0] = (uint8_t)(value >> 16);
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)value;
}

/* Default configuration and device initialization ------------------------ */
void sig24131_default_config(sig24131_config_t *config)
{
    if (config == NULL)
    {
        return;
    }
    config->gain = SIG24131_GAIN_1;
    config->filter = SIG24131_FILTER_SINC1;
    config->reference = SIG24131_REF_EXTERNAL;
    config->data_rate = SIG24131_RATE_10;
    config->conv_delay = SIG24131_DELAY_0_US;
    config->idac_current = SIG24131_IDAC_OFF;
    config->mux_p = 0U;
    config->mux_n = 1U;
    config->idac1_mux = 0U;
    config->idac2_mux = 0U;
    config->input_buffer_enable = 0U;
    config->internal_reference_enable = 0U;
    config->vbias_enable = 0U;
    config->fast_startup = 0U;
    config->burnout_enable = 0U;
    config->idac_pair_enable = 0U;
    config->idac_external_reference = 0U;
    config->latency = 0U;
    config->single_shot = 0U;
    config->reject_60hz = 0U;
}

sig24131_status_t sig24131_init(sig24131_t *dev, const sig24131_port_t *port)
{
    if (dev == NULL || port == NULL || port->spi_transfer == NULL)
    {
        return SIG24131_ERR_ARG;
    }
    dev->port = *port;
    sig24131_default_config(&dev->config);
    return SIG24131_OK;
}

/* Device control commands ------------------------------------------------ */
sig24131_status_t sig24131_reset(sig24131_t *dev)
{
    sig24131_status_t status;
    if (dev == NULL)
    {
        return SIG24131_ERR_ARG;
    }
    status = command_only(dev, SIG24131_CMD_RESET);
    if (status != SIG24131_OK)
    {
        return status;
    }
    status = delay(dev, SIG24131_POR_DELAY_US);
    if (status == SIG24131_OK)
    {
        sig24131_default_config(&dev->config);
    }
    return status;
}

sig24131_status_t sig24131_sync(sig24131_t *dev)
{
    return command_only(dev, SIG24131_CMD_SYNC);
}

sig24131_status_t sig24131_sleep(sig24131_t *dev)
{
    return command_only(dev, SIG24131_CMD_SLEEP);
}

sig24131_status_t sig24131_wakeup(sig24131_t *dev)
{
    return command_only(dev, SIG24131_CMD_WAKEUP);
}

sig24131_status_t sig24131_calibrate(sig24131_t *dev,
                                     sig24131_calibration_t calibration)
{
    uint8_t command;
    switch (calibration)
    {
    case SIG24131_CAL_SELF_OFFSET:
        command = SIG24131_CMD_SELFOCAL;
        break;
    case SIG24131_CAL_SYSTEM_OFFSET:
        command = SIG24131_CMD_SYSOCAL;
        break;
    case SIG24131_CAL_SYSTEM_GAIN:
        command = SIG24131_CMD_SYSGCAL;
        break;
    default:
        return SIG24131_ERR_ARG;
    }
    return command_only(dev, command);
}

/* Register read/write access --------------------------------------------- */
sig24131_status_t sig24131_write_register(sig24131_t *dev, uint8_t address,
                                          uint32_t value)
{
    uint8_t tx[4];
    uint8_t rx[4];
    sig24131_status_t status;
    if (dev == NULL || address > 0x1FU || value > 0xFFFFFFU)
    {
        return SIG24131_ERR_ARG;
    }
    tx[0] = (uint8_t)(SIG24131_CMD_WRITE_REG | address);
    u24_to_bytes(value, &tx[1]);
    status = begin_frame(dev);
    if (status != SIG24131_OK)
    {
        return status;
    }
    status = transfer(dev, tx, rx, sizeof(tx));
    if (end_frame(dev) != SIG24131_OK && status == SIG24131_OK)
    {
        status = SIG24131_ERR_IO;
    }
    return status;
}

sig24131_status_t sig24131_read_register(sig24131_t *dev, uint8_t address,
                                         uint32_t *value)
{
    uint8_t tx[4] = {0U, 0U, 0U, 0U};
    uint8_t rx[4] = {0U, 0U, 0U, 0U};
    sig24131_status_t status;
    if (dev == NULL || value == NULL || address > 0x1FU)
    {
        return SIG24131_ERR_ARG;
    }
    tx[0] = (uint8_t)(SIG24131_CMD_READ_REG | address);
    status = begin_frame(dev);
    if (status != SIG24131_OK)
    {
        return status;
    }
    status = transfer(dev, tx, rx, sizeof(tx));
    if (end_frame(dev) != SIG24131_OK && status == SIG24131_OK)
    {
        status = SIG24131_ERR_IO;
    }
    if (status == SIG24131_OK)
    {
        *value = u24_from_bytes(&rx[1]);
    }
    return status;
}

/* CONF0/CONF1 encoding and decoding -------------------------------------- */
static uint32_t config_conf0(const sig24131_config_t *config)
{
    uint32_t value = 0U;
    value |= (uint32_t)(config->vbias_enable & 1U) << 19;
    value |= (uint32_t)(config->fast_startup & 1U) << 18;
    value |= (uint32_t)(config->internal_reference_enable & 1U) << 16;
    value |= (uint32_t)(config->input_buffer_enable & 1U) << 15;
    value |= (uint32_t)(config->conv_delay & 7U) << 12;
    value |= (uint32_t)(config->burnout_enable & 1U) << 11;
    value |= (uint32_t)(config->idac_current & 7U) << 8;
    value |= (uint32_t)(config->idac_pair_enable & 1U) << 7;
    value |= (uint32_t)(config->idac_external_reference & 1U) << 6;
    value |= (uint32_t)(config->idac2_mux & 7U) << 3;
    value |= (uint32_t)(config->idac1_mux & 7U);
    return value;
}

static uint32_t config_conf1(const sig24131_config_t *config)
{
    uint32_t value = 0U;
    value |= (uint32_t)(config->latency & 1U) << 23;
    value |= (uint32_t)(config->single_shot & 1U) << 22;
    value |= (uint32_t)(config->reference & 3U) << 20;
    value |= (uint32_t)(config->gain & 0x0FU) << 16;
    value |= (uint32_t)(config->filter & 1U) << 15;
    value |= (uint32_t)(config->reject_60hz & 1U) << 14;
    value |= (uint32_t)(config->data_rate & 0x3FU) << 8;
    value |= (uint32_t)(config->mux_p & 0x0FU) << 4;
    value |= (uint32_t)(config->mux_n & 0x0FU);
    return value;
}

sig24131_status_t sig24131_write_config(sig24131_t *dev,
                                        const sig24131_config_t *config)
{
    sig24131_status_t status;
    if (dev == NULL || config == NULL)
    {
        return SIG24131_ERR_ARG;
    }
    status = sig24131_write_register(dev, SIG24131_REG_CONF0, config_conf0(config));
    if (status != SIG24131_OK)
    {
        return status;
    }
    status = sig24131_write_register(dev, SIG24131_REG_CONF1, config_conf1(config));
    if (status == SIG24131_OK)
    {
        dev->config = *config;
    }
    return status;
}

sig24131_status_t sig24131_read_config(sig24131_t *dev,
                                       sig24131_config_t *config)
{
    uint32_t conf0;
    uint32_t conf1;
    sig24131_status_t status;
    if (dev == NULL || config == NULL)
    {
        return SIG24131_ERR_ARG;
    }
    status = sig24131_read_register(dev, SIG24131_REG_CONF0, &conf0);
    if (status != SIG24131_OK)
    {
        return status;
    }
    status = sig24131_read_register(dev, SIG24131_REG_CONF1, &conf1);
    if (status != SIG24131_OK)
    {
        return status;
    }
    sig24131_default_config(config);
    config->vbias_enable = (uint8_t)((conf0 >> 19) & 1U);
    config->fast_startup = (uint8_t)((conf0 >> 18) & 1U);
    config->internal_reference_enable = (uint8_t)((conf0 >> 16) & 1U);
    config->input_buffer_enable = (uint8_t)((conf0 >> 15) & 1U);
    config->conv_delay = (sig24131_conv_delay_t)((conf0 >> 12) & 7U);
    config->burnout_enable = (uint8_t)((conf0 >> 11) & 1U);
    config->idac_current = (sig24131_idac_current_t)((conf0 >> 8) & 7U);
    config->idac_pair_enable = (uint8_t)((conf0 >> 7) & 1U);
    config->idac_external_reference = (uint8_t)((conf0 >> 6) & 1U);
    config->idac2_mux = (uint8_t)((conf0 >> 3) & 7U);
    config->idac1_mux = (uint8_t)(conf0 & 7U);
    config->latency = (uint8_t)((conf1 >> 23) & 1U);
    config->single_shot = (uint8_t)((conf1 >> 22) & 1U);
    config->reference = (sig24131_reference_t)((conf1 >> 20) & 3U);
    config->gain = (sig24131_gain_t)((conf1 >> 16) & 0x0FU);
    config->filter = (sig24131_filter_t)((conf1 >> 15) & 1U);
    config->reject_60hz = (uint8_t)((conf1 >> 14) & 1U);
    config->data_rate = (sig24131_data_rate_t)((conf1 >> 8) & 0x3FU);
    config->mux_p = (uint8_t)((conf1 >> 4) & 0x0FU);
    config->mux_n = (uint8_t)(conf1 & 0x0FU);
    dev->config = *config;
    return SIG24131_OK;
}

/* ADC conversion data readout and code conversion ------------------------- */
sig24131_status_t sig24131_read_data(sig24131_t *dev, sig24131_sample_t *sample)
{
    uint8_t tx[4] = {SIG24131_CMD_READ_DATA, 0U, 0U, 0U};
    uint8_t rx[4] = {0U, 0U, 0U, 0U};
    sig24131_status_t status;
    if (dev == NULL || sample == NULL)
    {
        return SIG24131_ERR_ARG;
    }
    status = begin_frame(dev);
    if (status != SIG24131_OK)
    {
        return status;
    }
    status = transfer(dev, tx, rx, sizeof(tx));
    if (end_frame(dev) != SIG24131_OK && status == SIG24131_OK)
    {
        status = SIG24131_ERR_IO;
    }
    if (status != SIG24131_OK)
    {
        return status;
    }
    sample->data[0] = rx[1];
    sample->data[1] = rx[2];
    sample->data[2] = rx[3];
    return SIG24131_OK;
}

int32_t sig24131_code_to_int32(const sig24131_sample_t *sample)
{
    int32_t value;
    if (sample == NULL)
    {
        return 0;
    }
    value = (int32_t)u24_from_bytes(sample->data);
    if ((value & 0x00800000) != 0)
    {
        value |= (int32_t)0xFF000000;
    }
    return value;
}
