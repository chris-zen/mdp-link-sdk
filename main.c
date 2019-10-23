#include <stdbool.h>
#include <stdint.h>
#include "sdk_common.h"
#include "nrf.h"
#include "nrf_esb.h"
#include "nrf_esb_error_codes.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_error.h"
#include "nrf_drv_clock.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "boards.h"
#include "app_timer.h"

#include "serial_number.h"

#define ESB_PRX 0
#define ESB_PTX 1

static char state = 'p';

static volatile bool rx_ready = false;
static volatile bool tx_busy = false;

static volatile bool timeout = false;
APP_TIMER_DEF(timeout_id);

static nrf_esb_config_t esb_config = NRF_ESB_DEFAULT_CONFIG;

static nrf_esb_payload_t rx_payload;
static nrf_esb_payload_t tx_payload;

#define RX_BUFFER_SIZE 64
static nrf_esb_payload_t rx_buffer[RX_BUFFER_SIZE];
static uint32_t rx_buffer_index = 0;


uint32_t esb_init(uint32_t mode);


void gpio_init(void)
{
    bsp_board_init(BSP_INIT_LEDS);
    for (int i = 0; i < 3; i++)
        bsp_board_led_on(i);
}

/**@brief Function starting the internal LFCLK oscillator.
 *
 * @details This is needed by RTC1 which is used by the Application Timer
 *          (When SoftDevice is enabled the LFCLK is always running and this is not needed).
 */
static void lfclk_request(void)
{
    ret_code_t err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);
    nrf_drv_clock_lfclk_request(NULL);
}

void clocks_start(void)
{
    lfclk_request();

    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;

    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0);
}

static void timeout_handler(void * p_context)
{
    timeout = true;
}

inline uint32_t reverse(uint32_t value)
{
    return ((value & 0xff) << 24) |
           (((value >> 8) & 0xff) << 16) |
           (((value >> 16) & 0xff) << 8) |
           ((value >> 24) & 0xff);
}

void log_payload(nrf_esb_payload_t* payload)
{
    static uint32_t count = 0;

    uint32_t* d = (uint32_t*) payload->data;

    NRF_LOG_INFO("C: %d L: %d P: %d N: %d I: %d", count++, payload->length, payload->pipe, payload->noack, payload->pid);
    NRF_LOG_INFO("00: %08x %08x %08x %08x", reverse(d[0]), reverse(d[1]), reverse(d[2]), reverse(d[3]));
    NRF_LOG_INFO("16: %08x %08x %08x %08x", reverse(d[4]), reverse(d[5]), reverse(d[6]), reverse(d[7]));
}

void rx_handler(nrf_esb_payload_t* payload)
{
    // static uint8_t last_d[8][32];

    // if (count == 0) {
    //     for (int i=0; i<8; i++) {
    //         for (int j=0; j<32; j++) {
    //             last_d[i][j] = 0;
    //         }
    //     }
    // }

    // uint32_t cnt = count++;
    // uint8_t* d = payload->data;

    // if (1 || (payload->pipe < 8 && d[6] != 2)) {

    // size_t i = 0;
    // while (i < 32 && d[i] == last_d[payload->pipe][i]) {
    //     i++;
    // }
    // if (1 || i < 32) {
    //     log_payload(cnt, payload);
    //     memcpy(last_d[payload->pipe], d, 32);
    // }

    // log_payload(payload);
    rx_ready = true;

        // uint32_t subaddr0 = (d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3])
        // uint32_t subaddr1 = (d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3])
        // uint8_t output_state = d[6];
        // float temp = (d[4] * 10.0) + (d[5] >> 4) + ((d[5] & 0x0f) / 10.0);
        // float v_input = (d[7] * 10.0) + (d[8] >> 4) + ((d[8] & 0x0f) / 10.0) + ((d[9] >> 4) / 100.0) + ((d[9] & 0x0f) / 1000.0);

        // NRF_LOG_INFO("Output state = %d", output_state);
        // NRF_LOG_INFO("Temp         = %03d", (uint32_t)(temp * 1000.0));
        // NRF_LOG_INFO("Vinput       = %05d", (uint32_t)(v_input * 100000.0));
    // }
}

uint32_t tx_handler(bool success)
{
    uint32_t err_code = NRF_SUCCESS;

    tx_busy = false;

    err_code = esb_init(ESB_PRX);
    VERIFY_SUCCESS(err_code);

    return err_code;
}

void nrf_esb_event_handler(nrf_esb_evt_t const * p_event)
{
    switch (p_event->evt_id)
    {
        case NRF_ESB_EVENT_TX_SUCCESS:
            // NRF_LOG_INFO("TX SUCCESS EVENT");
            bsp_board_led_off(BSP_BOARD_LED_1);
            tx_handler(true);
            break;

        case NRF_ESB_EVENT_TX_FAILED:
            NRF_LOG_INFO("TX FAILED EVENT");
            bsp_board_led_on(BSP_BOARD_LED_1);
            tx_handler(false);
            break;

        case NRF_ESB_EVENT_RX_RECEIVED:
            // NRF_LOG_INFO("RX RECEIVED EVENT");
            bsp_board_led_invert(BSP_BOARD_LED_2);
            if (rx_ready) {
                NRF_LOG_ERROR("Packet Lost");
            }
            else if (nrf_esb_read_rx_payload(&rx_payload) == NRF_SUCCESS)
            {
                rx_handler(&rx_payload);
            }
            break;
    }
}

uint32_t esb_init(uint32_t mode)
{
    uint32_t err_code;
    
    switch (mode) {
        case ESB_PRX:
            esb_config.protocol   = NRF_ESB_PROTOCOL_ESB;
            esb_config.mode       = NRF_ESB_MODE_PRX;
        break;

        case ESB_PTX:
            esb_config.protocol   = NRF_ESB_PROTOCOL_ESB_DPL;
            esb_config.mode       = NRF_ESB_MODE_PTX;
        break;
    }

    esb_config.bitrate            = NRF_ESB_BITRATE_2MBPS;
    esb_config.crc                = NRF_ESB_CRC_16BIT;
    esb_config.event_handler      = nrf_esb_event_handler;
    esb_config.selective_auto_ack = true;

    err_code = nrf_esb_init(&esb_config);
    VERIFY_SUCCESS(err_code);

    uint8_t base_addr_0[4] = {0xD3, 0xC2, 0xB1, 0xA0};
    uint8_t base_addr_1[4] = {0xD3, 0xC2, 0xB1, 0xA0};
    uint8_t addr_prefix[8] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7};

    nrf_esb_set_address_length(5);

    err_code = nrf_esb_set_base_address_0(base_addr_0);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_base_address_1(base_addr_1);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_prefixes(addr_prefix, 8);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_rf_channel(78);
    VERIFY_SUCCESS(err_code);

    switch (mode) {
        case ESB_PRX:
            err_code = nrf_esb_start_rx();
        break;

        case ESB_PTX:
            err_code = nrf_esb_start_tx();
            if (err_code == NRF_ERROR_BUFFER_EMPTY)
            {
                err_code = NRF_SUCCESS;
            }
        break;
    }

    return err_code;
}


uint32_t esb_send(uint8_t *packet, uint32_t packet_length)
{
    while (tx_busy);

    uint32_t err_code;

    err_code = nrf_esb_stop_rx();
    VERIFY_SUCCESS(err_code);

    err_code = esb_init(ESB_PTX);
    VERIFY_SUCCESS(err_code);

    memcpy(tx_payload.data, packet, packet_length);

    tx_payload.length = packet_length;
    tx_payload.pipe = 0;
    tx_payload.pid = 0;
    tx_payload.noack = true;

    log_payload(&tx_payload);
    err_code = nrf_esb_write_payload(&tx_payload);
    tx_busy = (err_code == NRF_SUCCESS);

    return err_code;
}


static void send_pairing_request()
{
    uint8_t data[32] = {
        0x09, 0x08, SERIAL_NUMBER_ARRAY, 0x00, 0x01, 0x5A, 0x73, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    uint32_t err_code = esb_send(data, sizeof(data));
    APP_ERROR_CHECK(err_code);
}

static void send_data_request()
{
    uint8_t data[32] = {
        0x07, 0x06, SERIAL_NUMBER_ARRAY, 0x00, 0x01, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    uint32_t err_code = esb_send(data, sizeof(data));
    APP_ERROR_CHECK(err_code);
}

static void start_timeout(uint32_t ms)
{
    uint32_t err_code = app_timer_start(timeout_id, APP_TIMER_TICKS(ms), NULL);
    APP_ERROR_CHECK(err_code);
    timeout = false;
}

static void stop_timeout()
{
    uint32_t err_code = app_timer_stop(timeout_id);
    APP_ERROR_CHECK(err_code);
    timeout = false;
}

/* State machine representing the communication protocol between the M01 and the P905 */
static void p905_protocol(void)
{
    static char last_state = 'p';
    static const uint32_t max_count = 7000;
    static uint32_t counter = max_count;

    uint32_t err_code;

    switch (state) {
        case 'p':
            NRF_LOG_INFO("Sending pairing request ...");
            if (counter-- == 0) {
                counter = max_count;
                bsp_board_led_invert(BSP_BOARD_LED_0);
            }
            send_pairing_request();
            start_timeout(1000);
            state = 'P';
            __WFE();
        break;
        case 'P':
            if (rx_ready) {
                uint16_t code = *((uint16_t*)rx_payload.data);
                if (code == 0x0D09) {
                    NRF_LOG_INFO("Response to pairing:");
                    bsp_board_led_off(BSP_BOARD_LED_0);
                    bsp_board_led_off(BSP_BOARD_LED_1);
                    state = 'd';
                }
                else {
                    NRF_LOG_WARNING("Unknown answer to pairing");
                    state = 'p';
                }
                log_payload(&rx_payload);
                stop_timeout();
                rx_ready = false;
            }
            else if (timeout) {
                NRF_LOG_INFO("Timeout");
                bsp_board_led_on(BSP_BOARD_LED_1);
                err_code = esb_init(ESB_PRX);
                APP_ERROR_CHECK(err_code);
                timeout = false;
                state = 'p';
            }
        break;
        case 'd':
            NRF_LOG_INFO("Sending data request ...");
            if (counter-- == 0) {
                counter = max_count;
                bsp_board_led_invert(BSP_BOARD_LED_0);
            }
            send_data_request();
            start_timeout(1000);
            state = 'D';
            __WFE();
        break;
        case 'D':
            if (rx_ready) {
                uint16_t code = *((uint16_t*)rx_payload.data);
                if (code == 0x1B07) {
                    NRF_LOG_INFO("Response to data:");
                    bsp_board_led_off(BSP_BOARD_LED_0);
                    bsp_board_led_off(BSP_BOARD_LED_1);
                    state = 'd';
                    // TODO log data
                }
                else {
                    NRF_LOG_WARNING("Unknown answer to data");
                    state = 'd';
                }
                log_payload(&rx_payload);
                stop_timeout();
                rx_ready = false;
            }
            else if (timeout) {
                NRF_LOG_INFO("Timeout");
                bsp_board_led_on(BSP_BOARD_LED_1);
                err_code = esb_init(ESB_PRX);
                APP_ERROR_CHECK(err_code);
                timeout = false;
                state = 'p';
            }
        break;
    }

    if (state != last_state) {
        NRF_LOG_INFO("%c -> %c", last_state, state);
        last_state = state;
    }
}

#define NUM_SKIP_CODES 2

/* Used when I need to sniff the communications between the two modules */
void sniffer(void)
{
    // static const uint16_t skip_codes[NUM_SKIP_CODES] = {0x1B07, 0x0607};
    static const uint16_t skip_codes[0] = {};

    uint32_t err_code;

    if (rx_ready) {
        uint16_t code = *((uint16_t*)rx_payload.data);
        uint32_t i = 0;
        bool skip = false;
        while (!skip && i < NUM_SKIP_CODES) {
            skip = code == skip_codes[i];
            i++;
        }
        if (!skip) {
            rx_buffer[rx_buffer_index++] = rx_payload;
        }
        if (rx_buffer_index == RX_BUFFER_SIZE) {
            err_code = nrf_esb_disable();
            APP_ERROR_CHECK(err_code);
            NRF_LOG_INFO("Flushing ...");
            for (int i = 0; i < RX_BUFFER_SIZE; i++) {
                log_payload(&rx_buffer[i]);
                NRF_LOG_FLUSH();
                while (NRF_LOG_PROCESS());
            }
            rx_buffer_index = 0;
            bsp_board_led_invert(BSP_BOARD_LED_1);
            err_code = esb_init(ESB_PRX);
            APP_ERROR_CHECK(err_code);
        }
        rx_ready = false;
    }
}

int main(void)
{
    uint32_t err_code;

    gpio_init();

    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();

    clocks_start();

    NRF_LOG_INFO("Initialising MDP Link ...");

    app_timer_init();

    err_code = app_timer_create(&timeout_id, APP_TIMER_MODE_SINGLE_SHOT, timeout_handler);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_FLUSH();

    nrf_delay_us(1000000);

    for (int i = 0; i < 3; i++)
        bsp_board_led_off(i);

    NRF_LOG_INFO("Running ...");
    NRF_LOG_FLUSH();
    while (NRF_LOG_PROCESS());

    err_code = esb_init(ESB_PRX);
    APP_ERROR_CHECK(err_code);
    
    while (true)
    {
        // bsp_board_led_invert(BSP_BOARD_LED_0);

        sniffer();
        
        if (NRF_LOG_PROCESS() == false)
        {
            // bsp_board_led_off(BSP_BOARD_LED_0);
            // p905_protocol();
            // __WFE();
        }
        else {
            // bsp_board_led_on(BSP_BOARD_LED_0);
        }
    }
}
