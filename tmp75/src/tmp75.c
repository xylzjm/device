#include "tmp75.h"

/*----------------------------------------------------------------------------*/
/* Registers                                                                  */
/*----------------------------------------------------------------------------*/

/** @brief Pointer values; the power-up value 00 selects temperature. */
#define TMP75_POINTER_TEMPERATURE 0x00U
#define TMP75_POINTER_CONFIG      0x01U

/** @brief Configuration layout: OS R1 R0 F1 F0 POL TM SD, reset 0x00. */
#define TMP75_CONFIG_OS 0x80U
#define TMP75_CONFIG_SD 0x01U

/** @brief Pointer byte plus the widest data field, the two bytes of a limit. */
#define TMP75_FRAME_MAX 3U

/*----------------------------------------------------------------------------*/
/* Port                                                                       */
/*----------------------------------------------------------------------------*/

static tmp75_status_t transfer(tmp75_t* dev,
                               const uint8_t* tx,
                               size_t tx_len,
                               uint8_t* rx,
                               size_t rx_len)
{
    if ( dev == NULL || dev->port.transfer == NULL
         || tx == NULL || tx_len == 0U
         || (rx_len > 0U && rx == NULL) ) {
        return TMP75_ERR_ARG;
    }
    return dev->port.transfer(dev->port.ctx, dev->address,
                              tx, tx_len, rx, rx_len);
}

/*----------------------------------------------------------------------------*/
/* Register access                                                            */
/*----------------------------------------------------------------------------*/

static tmp75_status_t write_register(tmp75_t* dev,
                                     uint8_t pointer,
                                     const uint8_t* data,
                                     size_t len)
{
    uint8_t frame[TMP75_FRAME_MAX];
    size_t i;

    if ( data == NULL || len == 0U || len > (TMP75_FRAME_MAX - 1U) ) {
        return TMP75_ERR_ARG;
    }
    frame[0] = pointer;
    for ( i = 0U; i < len; ++i ) {
        frame[i + 1U] = data[i];
    }
    return transfer(dev, frame, len + 1U, NULL, 0U);
}

static tmp75_status_t read_register(tmp75_t* dev,
                                    uint8_t pointer,
                                    uint8_t* data,
                                    size_t len)
{
    uint8_t tx[1];

    if ( data == NULL || len == 0U || len > (TMP75_FRAME_MAX - 1U) ) {
        return TMP75_ERR_ARG;
    }
    tx[0] = pointer;
    return transfer(dev, tx, sizeof(tx), data, len);
}

/*----------------------------------------------------------------------------*/
/* Configuration                                                              */
/*----------------------------------------------------------------------------*/

static tmp75_status_t check_config(const tmp75_config_t* config)
{
    unsigned int resolution;
    unsigned int fault_queue;
    unsigned int thermostat_mode;
    unsigned int alert_polarity;

    if ( config == NULL ) {
        return TMP75_ERR_ARG;
    }

    /* Cast to unsigned so that a negative enumerator is rejected too. */
    resolution      = (unsigned int)config->resolution;
    fault_queue     = (unsigned int)config->fault_queue;
    thermostat_mode = (unsigned int)config->thermostat_mode;
    alert_polarity  = (unsigned int)config->alert_polarity;

    if ( resolution > (unsigned int)TMP75_RESOLUTION_12_BIT
         || fault_queue > (unsigned int)TMP75_FAULT_4
         || thermostat_mode > (unsigned int)TMP75_THERMOSTAT_INTERRUPT
         || alert_polarity > (unsigned int)TMP75_ALERT_ACTIVE_HIGH
         || config->shutdown > 1U ) {
        return TMP75_ERR_ARG;
    }
    return TMP75_OK;
}

static uint8_t encode_config(const tmp75_config_t* config)
{
    uint8_t value = 0U;

    /* OS stays 0: a written 1 would start a conversion as a side effect. */
    value |= (uint8_t)(((uint8_t)config->resolution & 0x03U) << 5);
    value |= (uint8_t)(((uint8_t)config->fault_queue & 0x03U) << 3);
    value |= (uint8_t)(((uint8_t)config->alert_polarity & 0x01U) << 2);
    value |= (uint8_t)(((uint8_t)config->thermostat_mode & 0x01U) << 1);
    value |= (uint8_t)(config->shutdown & 0x01U);
    return value;
}

static void decode_config(uint8_t value,
                          tmp75_config_t* config)
{
    config->resolution      = (tmp75_resolution_t)((value >> 5) & 0x03U);
    config->fault_queue     = (tmp75_fault_queue_t)((value >> 3) & 0x03U);
    config->alert_polarity  = (tmp75_alert_polarity_t)((value >> 2) & 0x01U);
    config->thermostat_mode = (tmp75_thermostat_mode_t)((value >> 1) & 0x01U);
    config->shutdown        = (uint8_t)(value & 0x01U);
}

void tmp75_default_config(tmp75_config_t* config)
{
    if ( config == NULL ) {
        return;
    }
    config->resolution      = TMP75_RESOLUTION_9_BIT;
    config->fault_queue     = TMP75_FAULT_1;
    config->thermostat_mode = TMP75_THERMOSTAT_COMPARATOR;
    config->alert_polarity  = TMP75_ALERT_ACTIVE_LOW;
    config->shutdown        = 0U;
}

tmp75_status_t tmp75_ctor(tmp75_t* dev,
                          const tmp75_port_t* port,
                          tmp75_address_t address)
{
    if ( dev == NULL || port == NULL || port->transfer == NULL
         || (unsigned int)address < (unsigned int)TMP75_ADDRESS_0
         || (unsigned int)address > (unsigned int)TMP75_ADDRESS_7 ) {
        return TMP75_ERR_ARG;
    }
    dev->port    = *port;
    dev->address = (uint8_t)address;
    return TMP75_OK;
}

tmp75_status_t tmp75_write_config(tmp75_t* dev,
                                  const tmp75_config_t* config)
{
    uint8_t expected;
    uint8_t read_back;
    tmp75_status_t status;

    if ( dev == NULL ) {
        return TMP75_ERR_ARG;
    }
    status = check_config(config);
    if ( status != TMP75_OK ) {
        return status;
    }

    expected = encode_config(config);
    status   = write_register(dev, TMP75_POINTER_CONFIG, &expected, 1U);
    if ( status != TMP75_OK ) {
        return status;
    }
    status = read_register(dev, TMP75_POINTER_CONFIG, &read_back, 1U);
    if ( status != TMP75_OK ) {
        return status;
    }

    /* The driver always writes OS=0 and the device reads OS back as 0, so the
     * whole byte is comparable without masking any bit out. */
    if ( read_back != expected ) {
        return TMP75_ERR_VERIFY;
    }
    return TMP75_OK;
}

tmp75_status_t tmp75_read_config(tmp75_t* dev,
                                 tmp75_config_t* config)
{
    uint8_t value;
    tmp75_status_t status;

    if ( dev == NULL || config == NULL ) {
        return TMP75_ERR_ARG;
    }
    status = read_register(dev, TMP75_POINTER_CONFIG, &value, 1U);
    if ( status != TMP75_OK ) {
        return status;
    }
    decode_config(value, config);
    return TMP75_OK;
}

/*----------------------------------------------------------------------------*/
/* Temperature                                                                */
/*----------------------------------------------------------------------------*/

static float decode_celsius(const uint8_t* data)
{
    uint16_t raw;
    int32_t code;

    raw  = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    code = (int32_t)(raw >> 4);
    if ( (code & 0x0800) != 0 ) {
        code -= 0x1000;
    }
    /* 0.0625 is a negative power of two, so a 12-bit code scales exactly. */
    return (float)code * 0.0625f;
}

tmp75_status_t tmp75_read_temperature(tmp75_t* dev,
                                      float* celsius)
{
    uint8_t data[2] = {0U, 0U};
    tmp75_status_t status;

    if ( dev == NULL || celsius == NULL ) {
        return TMP75_ERR_ARG;
    }
    status = read_register(dev, TMP75_POINTER_TEMPERATURE, data, sizeof(data));
    if ( status != TMP75_OK ) {
        return status;
    }
    *celsius = decode_celsius(data);
    return TMP75_OK;
}

/*----------------------------------------------------------------------------*/
/* Limits                                                                     */
/*----------------------------------------------------------------------------*/

static tmp75_status_t check_limit(tmp75_limit_t limit)
{
    if ( limit != TMP75_LIMIT_LOW && limit != TMP75_LIMIT_HIGH ) {
        return TMP75_ERR_ARG;
    }
    return TMP75_OK;
}

static tmp75_status_t encode_celsius(float celsius,
                                     uint8_t* data)
{
    float scaled;
    int32_t code;
    uint16_t bits;

    if ( data == NULL ) {
        return TMP75_ERR_ARG;
    }
    /* One comparison pair so that a NaN is rejected together with the
     * out-of-range values. */
    if ( !(celsius >= -128.0f && celsius <= 127.9375f) ) {
        return TMP75_ERR_ARG;
    }

    scaled = celsius * 16.0f;
    code   = (int32_t)(scaled + ((scaled >= 0.0f) ? 0.5f : -0.5f));
    if ( code < -2048 || code > 2047 ) {
        return TMP75_ERR_ARG;
    }

    bits    = (uint16_t)((uint16_t)code & 0x0FFFU);
    data[0] = (uint8_t)(bits >> 4);
    data[1] = (uint8_t)(bits << 4);
    return TMP75_OK;
}

tmp75_status_t tmp75_write_limit(tmp75_t* dev,
                                 tmp75_limit_t limit,
                                 float celsius)
{
    uint8_t data[2];
    uint8_t read_back[2];
    tmp75_status_t status;

    if ( dev == NULL ) {
        return TMP75_ERR_ARG;
    }
    status = check_limit(limit);
    if ( status != TMP75_OK ) {
        return status;
    }
    status = encode_celsius(celsius, data);
    if ( status != TMP75_OK ) {
        return status;
    }

    status = write_register(dev, (uint8_t)limit, data, sizeof(data));
    if ( status != TMP75_OK ) {
        return status;
    }
    status = read_register(dev, (uint8_t)limit, read_back, sizeof(read_back));
    if ( status != TMP75_OK ) {
        return status;
    }

    /* Only the 12 data bits are defined; the low nibble of byte 2 is left
     * out of the comparison. */
    if ( read_back[0] != data[0]
         || ((uint8_t)(read_back[1] ^ data[1]) & 0xF0U) != 0U ) {
        return TMP75_ERR_VERIFY;
    }
    return TMP75_OK;
}

tmp75_status_t tmp75_read_limit(tmp75_t* dev,
                                tmp75_limit_t limit,
                                float* celsius)
{
    uint8_t data[2] = {0U, 0U};
    tmp75_status_t status;

    if ( dev == NULL || celsius == NULL ) {
        return TMP75_ERR_ARG;
    }
    status = check_limit(limit);
    if ( status != TMP75_OK ) {
        return status;
    }
    status = read_register(dev, (uint8_t)limit, data, sizeof(data));
    if ( status != TMP75_OK ) {
        return status;
    }
    *celsius = decode_celsius(data);
    return TMP75_OK;
}

/*----------------------------------------------------------------------------*/
/* One-shot conversion                                                        */
/*----------------------------------------------------------------------------*/

tmp75_status_t tmp75_one_shot(tmp75_t* dev)
{
    uint8_t value;
    uint8_t read_back;
    tmp75_status_t status;

    if ( dev == NULL ) {
        return TMP75_ERR_ARG;
    }
    status = read_register(dev, TMP75_POINTER_CONFIG, &read_back, 1U);
    if ( status != TMP75_OK ) {
        return status;
    }
    /* A one-shot starts only from shutdown mode, so SD=0 is reported. A
     * device still finishing its previous conversion cannot be detected. */
    if ( (read_back & TMP75_CONFIG_SD) == 0U ) {
        return TMP75_ERR_STATE;
    }

    value  = (uint8_t)(read_back | TMP75_CONFIG_OS);
    status = write_register(dev, TMP75_POINTER_CONFIG, &value, 1U);
    /* No read-back: OS reads as 0 by design, so it cannot be verified. */
    return status;
}

/*----------------------------------------------------------------------------*/
/* Conversion timing                                                          */
/*----------------------------------------------------------------------------*/

/** @brief Worst-case conversion time per resolution, in whole milliseconds. */
static const uint32_t conversion_time_ms[] = {38U, 75U, 150U, 300U};

uint32_t tmp75_conversion_time_ms(tmp75_resolution_t resolution)
{
    if ( (unsigned int)resolution > (unsigned int)TMP75_RESOLUTION_12_BIT ) {
        return 0U;
    }
    return conversion_time_ms[(unsigned int)resolution];
}
