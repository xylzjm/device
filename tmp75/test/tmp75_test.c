#include <stdio.h>
#include <string.h>

#include "tmp75.h"

/*----------------------------------------------------------------------------*/
/* Test harness                                                               */
/*----------------------------------------------------------------------------*/

static int checks;
static int failures;

/** @brief Record one boolean check and report the expression on failure. */
#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if ( !(cond) ) {                                                     \
            failures++;                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while ( 0 )

/** @brief Widest frame: one pointer byte plus the two bytes of a register. */
#define FRAME_MAX 3U

/**
 * @brief Fake device behind the port.
 *
 * Every pointer owns a two-byte slot instead of one flat byte array: a
 * 2-byte read at pointer 0 must not run into the configuration byte that a
 * flat layout would place at index 1.
 */
typedef struct {
    uint8_t reg[4][2];       /**< Two bytes per pointer. */
    uint8_t fail;            /**< When set, every transfer fails with ERR_IO. */
    uint8_t corrupt_enable;  /**< Flip bits after a write. */
    uint8_t corrupt_reg;     /**< Pointer whose slot is corrupted. */
    uint8_t corrupt_byte;    /**< Byte index inside the slot. */
    uint8_t corrupt_mask;    /**< Bits to toggle. */
    int calls;
    int reads;
    int writes;
    uint8_t last_address;
    uint8_t last_write[FRAME_MAX];
    uint8_t last_write_len;
    uint8_t last_write_rx_len;
    uint8_t last_read_ptr;
    uint8_t last_read_tx_len;
    uint8_t last_read_rx_len;
} fake_t;

static fake_t fake;
static tmp75_t dev;

static tmp75_status_t fake_transfer(void* ctx,
                                    uint8_t address,
                                    const uint8_t* tx,
                                    size_t tx_len,
                                    uint8_t* rx,
                                    size_t rx_len)
{
    fake_t* f = (fake_t*)ctx;
    uint8_t pointer;
    size_t i;

    f->calls++;
    if ( tx_len == 0U || tx_len > FRAME_MAX ) {
        return TMP75_ERR_ARG;
    }
    f->last_address = address;
    pointer         = (uint8_t)(tx[0] & 0x03U);

    if ( rx_len == 0U ) {
        f->writes++;
        f->last_write_len    = (uint8_t)tx_len;
        f->last_write_rx_len = (uint8_t)rx_len;
        for ( i = 0U; i < tx_len; ++i ) {
            f->last_write[i] = tx[i];
        }
        if ( f->fail != 0U ) {
            return TMP75_ERR_IO;
        }
        for ( i = 1U; i < tx_len; ++i ) {
            f->reg[pointer][i - 1U] = tx[i];
        }
        if ( pointer == 1U ) {
            f->reg[pointer][0] &= 0x7FU; /* OS always reads back as 0. */
        }
        if ( f->corrupt_enable != 0U && f->corrupt_reg == pointer ) {
            f->reg[pointer][f->corrupt_byte] ^= f->corrupt_mask;
        }
        return TMP75_OK;
    }

    f->reads++;
    f->last_read_ptr    = pointer;
    f->last_read_tx_len = (uint8_t)tx_len;
    f->last_read_rx_len = (uint8_t)rx_len;
    if ( f->fail != 0U ) {
        return TMP75_ERR_IO;
    }
    if ( rx_len > (FRAME_MAX - 1U) ) {
        return TMP75_ERR_ARG;
    }
    for ( i = 0U; i < rx_len; ++i ) {
        rx[i] = f->reg[pointer][i];
    }
    return TMP75_OK;
}

static const tmp75_port_t port = {&fake, fake_transfer};

/** @brief Load the power-up values and construct the test object. */
static void setup(void)
{
    memset(&fake, 0, sizeof fake);
    fake.reg[2][0] = 0x4BU;
    fake.reg[3][0] = 0x50U;
    (void)tmp75_ctor(&dev, &port, TMP75_ADDRESS_0);
}

static float make_nan(void)
{
    volatile float zero = 0.0f;

    return zero / zero;
}

static int approx(float actual, float expected)
{
    float diff = actual - expected;

    if ( diff < 0.0f ) {
        diff = -diff;
    }
    return diff <= 0.0001f;
}

/*----------------------------------------------------------------------------*/
/* Test cases                                                                 */
/*----------------------------------------------------------------------------*/

static void test_ctor(void)
{
    tmp75_t device;
    tmp75_port_t no_transfer;
    int address;

    memset(&no_transfer, 0, sizeof no_transfer);

    CHECK(tmp75_ctor(NULL, &port, TMP75_ADDRESS_0) == TMP75_ERR_ARG);
    CHECK(tmp75_ctor(&device, NULL, TMP75_ADDRESS_0) == TMP75_ERR_ARG);
    CHECK(tmp75_ctor(&device, &no_transfer, TMP75_ADDRESS_0) == TMP75_ERR_ARG);
    CHECK(tmp75_ctor(&device, &port, (tmp75_address_t)0x47) == TMP75_ERR_ARG);
    CHECK(tmp75_ctor(&device, &port, (tmp75_address_t)0x50) == TMP75_ERR_ARG);

    setup();
    for ( address = 0x48; address <= 0x4F; address++ ) {
        CHECK(tmp75_ctor(&device, &port,
                         (tmp75_address_t)address) == TMP75_OK);
        CHECK(device.address == (uint8_t)address);
        CHECK(device.port.transfer == fake_transfer);
        CHECK(device.port.ctx == &fake);
    }
    CHECK(fake.calls == 0); /* construction never touches the bus */
}

static void test_default_config(void)
{
    tmp75_config_t config;

    setup();
    tmp75_default_config(NULL); /* NULL is ignored, not a crash */

    tmp75_default_config(&config);
    CHECK(config.resolution == TMP75_RESOLUTION_9_BIT);
    CHECK(config.fault_queue == TMP75_FAULT_1);
    CHECK(config.thermostat_mode == TMP75_THERMOSTAT_COMPARATOR);
    CHECK(config.alert_polarity == TMP75_ALERT_ACTIVE_LOW);
    CHECK(config.shutdown == 0U);

    /* The encoded default must equal the register reset value. */
    CHECK(tmp75_write_config(&dev, &config) == TMP75_OK);
    CHECK(fake.reg[1][0] == 0x00U);
}

static void test_config_roundtrip(void)
{
    tmp75_config_t config;
    tmp75_config_t read_back;
    unsigned int resolution;
    unsigned int fault;
    unsigned int mode;
    unsigned int polarity;
    unsigned int shutdown;
    uint8_t expected;

    setup();
    for ( resolution = 0U; resolution < 4U; ++resolution ) {
        for ( fault = 0U; fault < 4U; ++fault ) {
            for ( mode = 0U; mode < 2U; ++mode ) {
                for ( polarity = 0U; polarity < 2U; ++polarity ) {
                    for ( shutdown = 0U; shutdown < 2U; ++shutdown ) {
                        config.resolution =
                            (tmp75_resolution_t)resolution;
                        config.fault_queue =
                            (tmp75_fault_queue_t)fault;
                        config.thermostat_mode =
                            (tmp75_thermostat_mode_t)mode;
                        config.alert_polarity =
                            (tmp75_alert_polarity_t)polarity;
                        config.shutdown = (uint8_t)shutdown;

                        expected = (uint8_t)((resolution << 5)
                                             | (fault << 3) | (polarity << 2)
                                             | (mode << 1) | shutdown);

                        CHECK(tmp75_write_config(&dev, &config)
                              == TMP75_OK);
                        CHECK(fake.reg[1][0] == expected);
                        CHECK(tmp75_read_config(&dev, &read_back)
                              == TMP75_OK);
                        CHECK(read_back.resolution == config.resolution);
                        CHECK(read_back.fault_queue == config.fault_queue);
                        CHECK(read_back.thermostat_mode
                              == config.thermostat_mode);
                        CHECK(read_back.alert_polarity
                              == config.alert_polarity);
                        CHECK(read_back.shutdown == config.shutdown);
                    }
                }
            }
        }
    }
}

static void test_config_verify(void)
{
    tmp75_config_t config;

    setup();
    tmp75_default_config(&config);
    config.resolution = TMP75_RESOLUTION_12_BIT;

    fake.corrupt_enable = 1U;
    fake.corrupt_reg     = 1U;
    fake.corrupt_byte    = 0U;
    fake.corrupt_mask    = 0x01U; /* SD, which the driver wrote as 0 */
    CHECK(tmp75_write_config(&dev, &config) == TMP75_ERR_VERIFY);
    fake.corrupt_enable = 0U;
}

static void test_config_frame(void)
{
    tmp75_config_t config;

    setup();
    tmp75_default_config(&config);
    CHECK(tmp75_write_config(&dev, &config) == TMP75_OK);
    CHECK(fake.last_address == 0x48U);
    CHECK(fake.last_write_len == 2U);
    CHECK(fake.last_write_rx_len == 0U);
    CHECK(fake.last_write[0] == 0x01U);
    CHECK(fake.last_write[1] == 0x00U);
    CHECK(fake.writes == 1);
    CHECK(fake.reads == 1); /* the verification read back */

    CHECK(tmp75_read_config(&dev, &config) == TMP75_OK);
    CHECK(fake.last_read_ptr == 0x01U);
    CHECK(fake.last_read_tx_len == 1U);
    CHECK(fake.last_read_rx_len == 1U);
}

static void test_temperature(void)
{
    static const struct {
        uint16_t code;
        float celsius;
    } vectors[] = {
        {0x7FFU, 127.9375f},
        {0x640U, 100.0f},
        {0x500U, 80.0f},
        {0x4B0U, 75.0f},
        {0x190U, 25.0f},
        {0x004U, 0.25f},
        {0x000U, 0.0f},
        {0xFFCU, -0.25f},
        {0xE70U, -25.0f},
        {0xC90U, -55.0f},
        {0x800U, -128.0f}
    };
    uint16_t raw;
    float celsius;
    size_t i;

    setup();
    for ( i = 0U; i < (sizeof vectors / sizeof vectors[0]); ++i ) {
        raw = (uint16_t)(vectors[i].code << 4);
        fake.reg[0][0] = (uint8_t)(raw >> 8);
        fake.reg[0][1] = (uint8_t)raw;

        celsius = 12345.0f;
        CHECK(tmp75_read_temperature(&dev, &celsius) == TMP75_OK);
        CHECK(approx(celsius, vectors[i].celsius));
    }
}

static void test_temperature_frame(void)
{
    float celsius;

    setup();
    CHECK(tmp75_read_temperature(&dev, &celsius) == TMP75_OK);
    CHECK(fake.last_address == 0x48U);
    CHECK(fake.last_read_ptr == 0x00U);
    CHECK(fake.last_read_tx_len == 1U);
    CHECK(fake.last_read_rx_len == 2U);
    CHECK(fake.writes == 0); /* a read never writes */
}

static void test_limit_roundtrip(void)
{
    static const struct {
        float celsius;
        uint16_t code;
    } vectors[] = {
        {80.0f, 0x500U},
        {75.0f, 0x4B0U},
        {-25.0f, 0xE70U},
        {0.25f, 0x004U},
        {-0.25f, 0xFFCU}
    };
    uint16_t code;
    float read_back;
    size_t i;

    setup();
    for ( i = 0U; i < (sizeof vectors / sizeof vectors[0]); ++i ) {
        CHECK(tmp75_write_limit(&dev, TMP75_LIMIT_HIGH,
                                vectors[i].celsius) == TMP75_OK);
        code = (uint16_t)(((uint16_t)fake.reg[3][0] << 4)
                          | (uint16_t)(fake.reg[3][1] >> 4));
        CHECK(code == vectors[i].code);

        CHECK(tmp75_read_limit(&dev, TMP75_LIMIT_HIGH,
                               &read_back) == TMP75_OK);
        CHECK(approx(read_back, vectors[i].celsius));
    }

    /* The low limit lives behind its own pointer. */
    CHECK(tmp75_write_limit(&dev, TMP75_LIMIT_LOW, -40.0f) == TMP75_OK);
    CHECK(fake.last_write[0] == 0x02U);
    code = (uint16_t)(((uint16_t)fake.reg[2][0] << 4)
                      | (uint16_t)(fake.reg[2][1] >> 4));
    CHECK(code == 0xD80U); /* -40 C = -640 = 0xD80 */
    CHECK(tmp75_read_limit(&dev, TMP75_LIMIT_LOW, &read_back) == TMP75_OK);
    CHECK(approx(read_back, -40.0f));
}

static void test_limit_range(void)
{
    int calls;

    setup();
    calls = fake.calls;

    CHECK(tmp75_write_limit(&dev, TMP75_LIMIT_HIGH,
                            -128.0625f) == TMP75_ERR_ARG);
    CHECK(tmp75_write_limit(&dev, TMP75_LIMIT_HIGH,
                            127.96875f) == TMP75_ERR_ARG);
    CHECK(tmp75_write_limit(&dev, TMP75_LIMIT_HIGH, make_nan())
          == TMP75_ERR_ARG);
    CHECK(tmp75_write_limit(&dev, TMP75_LIMIT_HIGH, 128.0f) == TMP75_ERR_ARG);
    CHECK(tmp75_read_limit(&dev, TMP75_LIMIT_HIGH, NULL) == TMP75_ERR_ARG);
    CHECK(fake.calls == calls); /* rejected before any bus traffic */

    /* Both ends of the 12-bit range are accepted. */
    CHECK(tmp75_write_limit(&dev, TMP75_LIMIT_HIGH, -128.0f) == TMP75_OK);
    CHECK(tmp75_write_limit(&dev, TMP75_LIMIT_HIGH, 127.9375f) == TMP75_OK);
}

static void test_limit_enum(void)
{
    float celsius;
    int calls;

    setup();
    calls = fake.calls;

    CHECK(tmp75_write_limit(&dev, (tmp75_limit_t)0x00,
                            25.0f) == TMP75_ERR_ARG);
    CHECK(tmp75_write_limit(&dev, (tmp75_limit_t)0x01,
                            25.0f) == TMP75_ERR_ARG);
    CHECK(tmp75_write_limit(&dev, (tmp75_limit_t)0x04,
                            25.0f) == TMP75_ERR_ARG);
    CHECK(tmp75_read_limit(&dev, (tmp75_limit_t)0x04,
                           &celsius) == TMP75_ERR_ARG);
    CHECK(fake.calls == calls);
}

static void test_limit_verify(void)
{
    float celsius;

    setup();
    fake.corrupt_enable = 1U;
    fake.corrupt_reg     = 3U;     /* T_HIGH */
    fake.corrupt_byte    = 0U;
    fake.corrupt_mask    = 0x10U;  /* a bit inside the 12-bit value */
    CHECK(tmp75_write_limit(&dev, TMP75_LIMIT_HIGH,
                            80.0f) == TMP75_ERR_VERIFY);

    /* The low nibble of byte 2 carries no value, so it is not compared. */
    fake.corrupt_byte = 1U;
    fake.corrupt_mask = 0x0FU;
    CHECK(tmp75_write_limit(&dev, TMP75_LIMIT_HIGH, 80.0f) == TMP75_OK);
    CHECK(tmp75_read_limit(&dev, TMP75_LIMIT_HIGH, &celsius) == TMP75_OK);
    CHECK(approx(celsius, 80.0f));
    fake.corrupt_enable = 0U;
}

static void test_one_shot(void)
{
    tmp75_config_t config;
    int writes;

    setup();
    /* The reset value has SD=0, so a one-shot must be refused. */
    CHECK(tmp75_one_shot(&dev) == TMP75_ERR_STATE);
    CHECK(fake.writes == 0);
    CHECK(fake.reads == 1); /* the configuration was read first */

    tmp75_default_config(&config);
    config.shutdown = 1U;
    CHECK(tmp75_write_config(&dev, &config) == TMP75_OK);
    writes = fake.writes;

    CHECK(tmp75_one_shot(&dev) == TMP75_OK);
    CHECK(fake.writes == writes + 1);
    CHECK(fake.last_write[0] == 0x01U);        /* pointer = configuration */
    CHECK((fake.last_write[1] & 0x80U) != 0U); /* OS = 1 */
    CHECK((fake.last_write[1] & 0x01U) != 0U); /* SD kept */
    CHECK(fake.reg[1][0] == 0x01U);            /* OS reads back as 0 */
}

static void test_null_args(void)
{
    tmp75_config_t config;
    float celsius;
    int calls;

    setup();
    calls = fake.calls;

    CHECK(tmp75_read_config(&dev, NULL) == TMP75_ERR_ARG);
    CHECK(tmp75_read_config(NULL, &config) == TMP75_ERR_ARG);
    CHECK(tmp75_read_temperature(&dev, NULL) == TMP75_ERR_ARG);
    CHECK(tmp75_read_temperature(NULL, &celsius) == TMP75_ERR_ARG);
    CHECK(tmp75_write_limit(NULL, TMP75_LIMIT_HIGH, 25.0f) == TMP75_ERR_ARG);
    CHECK(tmp75_read_limit(NULL, TMP75_LIMIT_HIGH,
                           &celsius) == TMP75_ERR_ARG);
    CHECK(tmp75_one_shot(NULL) == TMP75_ERR_ARG);
    CHECK(fake.calls == calls);

    /* Out-of-range fields are rejected before any transfer. */
    tmp75_default_config(&config);
    config.resolution = (tmp75_resolution_t)4;
    CHECK(tmp75_write_config(&dev, &config) == TMP75_ERR_ARG);
    config.resolution = (tmp75_resolution_t)-1;
    CHECK(tmp75_write_config(&dev, &config) == TMP75_ERR_ARG);
    config.resolution  = TMP75_RESOLUTION_12_BIT;
    config.fault_queue = (tmp75_fault_queue_t)4;
    CHECK(tmp75_write_config(&dev, &config) == TMP75_ERR_ARG);
    config.fault_queue = TMP75_FAULT_1;
    config.shutdown    = 2U;
    CHECK(tmp75_write_config(&dev, &config) == TMP75_ERR_ARG);
    config.shutdown        = 0U;
    config.thermostat_mode = (tmp75_thermostat_mode_t)2;
    CHECK(tmp75_write_config(&dev, &config) == TMP75_ERR_ARG);
    config.thermostat_mode = TMP75_THERMOSTAT_COMPARATOR;
    config.alert_polarity  = (tmp75_alert_polarity_t)2;
    CHECK(tmp75_write_config(&dev, &config) == TMP75_ERR_ARG);
    config.alert_polarity = TMP75_ALERT_ACTIVE_LOW;
    CHECK(tmp75_write_config(NULL, &config) == TMP75_ERR_ARG);
    CHECK(fake.calls == calls);
}

static void test_conversion_time(void)
{
    CHECK(tmp75_conversion_time_ms(TMP75_RESOLUTION_9_BIT) == 38U);
    CHECK(tmp75_conversion_time_ms(TMP75_RESOLUTION_10_BIT) == 75U);
    CHECK(tmp75_conversion_time_ms(TMP75_RESOLUTION_11_BIT) == 150U);
    CHECK(tmp75_conversion_time_ms(TMP75_RESOLUTION_12_BIT) == 300U);
    CHECK(tmp75_conversion_time_ms((tmp75_resolution_t)4) == 0U);
    CHECK(tmp75_conversion_time_ms((tmp75_resolution_t)9) == 0U);
    CHECK(tmp75_conversion_time_ms((tmp75_resolution_t)-1) == 0U);
}

static void test_io_error(void)
{
    tmp75_config_t config;
    float celsius;

    setup();
    fake.fail = 1U;

    tmp75_default_config(&config);
    CHECK(tmp75_read_temperature(&dev, &celsius) == TMP75_ERR_IO);
    CHECK(tmp75_read_config(&dev, &config) == TMP75_ERR_IO);
    CHECK(tmp75_write_config(&dev, &config) == TMP75_ERR_IO);
    CHECK(tmp75_write_limit(&dev, TMP75_LIMIT_HIGH,
                            80.0f) == TMP75_ERR_IO);
    CHECK(tmp75_one_shot(&dev) == TMP75_ERR_IO);
}

/*----------------------------------------------------------------------------*/
/* Test runner                                                                */
/*----------------------------------------------------------------------------*/

int main(void)
{
    test_ctor();
    test_default_config();
    test_config_roundtrip();
    test_config_verify();
    test_config_frame();
    test_temperature();
    test_temperature_frame();
    test_limit_roundtrip();
    test_limit_range();
    test_limit_enum();
    test_limit_verify();
    test_one_shot();
    test_null_args();
    test_conversion_time();
    test_io_error();

    printf("%d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
