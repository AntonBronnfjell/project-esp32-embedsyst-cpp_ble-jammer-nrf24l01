/**
 * ESP32-S3 Bluetooth Jammer with Dual NRF24L01+PA+LNA + built-in BLE
 * Based on: ESPnRF24-Jammer, RF-Clown, and MyJammer projects
 * FOR EDUCATIONAL AND RESEARCH PURPOSES ONLY
 *
 * WARNING: Use of RF jammers is illegal in most countries.
 * This code is for understanding RF communications and
 * testing your own equipment's vulnerability ONLY.
 *
 * Three transmitters: NRF24 #1, NRF24 #2, and ESP32-S3 built-in BLE (advertising).
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "led_strip_encoder.h"
#include "RF24.h"

// NimBLE (C API) - built-in radio as third transmitter
extern "C" {
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
}

static const char* TAG = "JAMMER";

// ANSI colors for serial monitor (works in most terminals; disable if not supported)
#define C_OFF   "\033[0m"
#define C_RED   "\033[1;31m"
#define C_GRN   "\033[1;32m"
#define C_YEL   "\033[1;33m"
#define C_BLU   "\033[1;34m"
#define C_MAG   "\033[1;35m"
#define C_CYN   "\033[1;36m"
#define C_DIM   "\033[90m"
#define C_BOLD  "\033[1m"

// Category log macros: [INIT] [RF1] [RF2] [BTN] [MODE] [JAM] [BLE] [LED]
#define LOG_INIT(fmt, ...)  ESP_LOGI(TAG, C_CYN "[INIT]" C_OFF " " fmt, ##__VA_ARGS__)
#define LOG_RF1(fmt, ...)   ESP_LOGI(TAG, C_GRN "[RF1]" C_OFF " " fmt, ##__VA_ARGS__)
#define LOG_RF2(fmt, ...)   ESP_LOGI(TAG, C_BLU "[RF2]" C_OFF " " fmt, ##__VA_ARGS__)
#define LOG_BTN(fmt, ...)   ESP_LOGI(TAG, C_YEL "[BTN]" C_OFF " " fmt, ##__VA_ARGS__)
#define LOG_MODE(fmt, ...)  ESP_LOGI(TAG, C_MAG "[MODE]" C_OFF " " fmt, ##__VA_ARGS__)
#define LOG_JAM(fmt, ...)   ESP_LOGI(TAG, C_GRN "[JAM]" C_OFF " " fmt, ##__VA_ARGS__)
#define LOG_JAM_OFF(fmt, ...) ESP_LOGI(TAG, C_RED "[JAM]" C_OFF " " fmt, ##__VA_ARGS__)
#define LOG_BLE(fmt, ...)   ESP_LOGI(TAG, C_CYN "[BLE]" C_OFF " " fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   ESP_LOGE(TAG, C_RED "[ERR]" C_OFF " " fmt, ##__VA_ARGS__)
#define LOG_WRN(fmt, ...)   ESP_LOGW(TAG, C_YEL "[WRN]" C_OFF " " fmt, ##__VA_ARGS__)

// Last button press duration (ms), set by check_button_event; 0 if not a press
static uint32_t s_last_press_duration_ms = 0;

// ============== ESP32-S3 PIN CONFIGURATION ==============
// NOTE: ESP32-S3 has NO GPIO 22, 23, 24, 25 (only 0-21 and 26-48).
// Pins below are valid for ESP32-S3 and typical Lonely Binary N16R8 / DevKitC headers.
//
// First NRF24L01 Module (HSPI)
#define CE_PIN_1    16
#define CSN_PIN_1   15
#define SCK_PIN_1   14
#define MOSI_PIN_1  13
#define MISO_PIN_1  12

// Second NRF24L01 Module (VSPI) - uses GPIOs that exist on ESP32-S3
#define CE_PIN_2    17
#define CSN_PIN_2    18
#define SCK_PIN_2    21
#define MOSI_PIN_2   47
#define MISO_PIN_2   39

// Mode button (change if your board doesn't break out this pin; e.g. 34, 35, 36, 38, 48)
#define BUTTON_PIN  35

// Lonely Binary ESP32-S3 2520V5 N16R8: built-in WS2812 RGB LED on GPIO 48
#define WS2812_GPIO     48
#define RMT_RESOLUTION  10000000
#define RAINBOW_MS_SLOW  80   // slow rainbow when jamming (e.g. constant carrier)
#define RAINBOW_MS_FAST  18   // faster rainbow in cycle/sweep modes

// ============== JAMMER CONFIGURATION ==============
#define BLUETOOTH_CHANNEL_COUNT  79
#define BLE_ADVERTISING_CHANNELS  3
#define SWEEP_DELAY_MS            1
#define MIN_CHANNEL                0
#define MAX_CHANNEL               79

// ============== GLOBAL VARIABLES ==============
// ESP-IDF backend: two SPI instances (HSPI and VSPI)
SPI spi0(SPI2_HOST);  // HSPI
SPI spi1(SPI3_HOST);  // VSPI

RF24 radio1(CE_PIN_1, CSN_PIN_1);
RF24 radio2(CE_PIN_2, CSN_PIN_2);

enum JammerMode {
    MODE_BLUETOOTH_CLASSIC,
    MODE_BLUETOOTH_BLE,
    MODE_DUAL_SWEEP,
    MODE_CONSTANT_CARRIER,
    MODE_COUNT
};

static JammerMode currentMode = MODE_BLUETOOTH_CLASSIC;
static uint8_t currentChannel = 45;
static bool directionUp = true;
static uint8_t dual_ch1 = 0;
static uint8_t dual_ch2 = 40;
static bool dual_dir1 = true;
static bool dual_dir2 = true;

// Jamming on/off: LED on when active, off when stopped. Button short-press toggles; long-press cycles mode.
static volatile bool s_jamming_active = false;

// Button state: short press = toggle jamming, long press = cycle mode
static int lastButtonState = 1;
static uint32_t pressStartTime = 0;  // 0 = not in a press
static rmt_channel_handle_t s_rmt_chan = nullptr;
static rmt_encoder_handle_t s_led_encoder = nullptr;
#define DEBOUNCE_MS   50
#define LONG_PRESS_MS 800

// ============== FUNCTION PROTOTYPES ==============
static void init_nrf24_modules(void);
static void disable_esp_wifi_only(void);  // WiFi only; BLE stays on for 3rd transmitter
static void init_ble_third_transmitter(void);
static void set_channel_and_apply(RF24& radio, uint8_t channel);
static void set_channel_sweep(RF24& radio, uint8_t& channel, bool& dir);
static void set_constant_carrier(RF24& radio, uint8_t channel);
/** Apply current mode once (use when turning jamming ON so RF starts immediately). */
static void apply_jamming_mode_once(void);
static void print_mode(void);
static void switch_mode(void);
/** 0 = no event, 1 = short press (toggle jamming), 2 = long press (cycle mode) */
static int check_button_event(void);
static void stop_jamming_radios(void);
static void init_rgb_led(void);
static void rainbow_led_task(void* arg);

// ============== ENTRY POINT ==============
extern "C" void app_main(void)
{
    LOG_INIT("=== ESP32-S3 Bluetooth Jammer ===");
    LOG_INIT("FOR EDUCATIONAL USE ONLY");

    LOG_INIT("NVS flash...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    LOG_INIT("Disabling WiFi (BLE kept for 3rd TX)...");
    disable_esp_wifi_only();

    LOG_INIT("BLE (3rd transmitter)...");
    init_ble_third_transmitter();

    LOG_INIT("Button GPIO %d (short=toggle jam, long=cycle mode)", BUTTON_PIN);
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << BUTTON_PIN);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);

    LOG_INIT("NRF24 modules (HSPI + VSPI)...");
    init_nrf24_modules();

    LOG_INIT("RGB LED (WS2812 GPIO %d)", WS2812_GPIO);
    init_rgb_led();
    xTaskCreate(rainbow_led_task, "rainbow", 2048, NULL, 3, NULL);

    LOG_INIT("Setup complete. Short-press = start/stop jamming. Long-press = change mode.");
    print_mode();

    static uint32_t s_jam_status_ticks = 0;
    while (1) {
        int ev = check_button_event();
        if (ev == 1) {
            LOG_BTN("Short press (%u ms) -> toggle jamming", (unsigned)s_last_press_duration_ms);
            s_jamming_active = !s_jamming_active;
            if (s_jamming_active) {
                LOG_JAM("Powering up radios...");
                radio1.powerUp();
                radio2.powerUp();
                vTaskDelay(pdMS_TO_TICKS(8));   // Tpd2stby ~5ms
                apply_jamming_mode_once();
                vTaskDelay(pdMS_TO_TICKS(15));  // PLL/PA settle
                bool r1 = radio1.isChipConnected();
                bool r2 = radio2.isChipConnected();
                uint8_t ch1 = radio1.getChannel(), ch2 = radio2.getChannel();
                LOG_JAM("ON | R1=%s R2=%s | ch=%d (R1:%d R2:%d)", r1 ? "ok" : "FAIL", r2 ? "ok" : "FAIL", (int)currentChannel, (int)ch1, (int)ch2);
                s_jam_status_ticks = 0;
            } else {
                stop_jamming_radios();
                LOG_JAM_OFF("OFF (radios in power-down)");
            }
        } else if (ev == 2) {
            LOG_BTN("Long press (%u ms) -> cycle mode", (unsigned)s_last_press_duration_ms);
            switch_mode();
        }

        if (s_jamming_active && (++s_jam_status_ticks >= 1500)) {  // ~15 s
            s_jam_status_ticks = 0;
            LOG_JAM("Status: active | mode=%d ch=%d R1=%s R2=%s", (int)currentMode, (int)currentChannel,
                    radio1.isChipConnected() ? "ok" : "FAIL", radio2.isChipConnected() ? "ok" : "FAIL");
        }

        if (s_jamming_active) {
            switch (currentMode) {
                case MODE_BLUETOOTH_CLASSIC:
                    set_channel_sweep(radio1, currentChannel, directionUp);
                    set_channel_sweep(radio2, currentChannel, directionUp);
                    vTaskDelay(pdMS_TO_TICKS(SWEEP_DELAY_MS));
                    break;

                case MODE_BLUETOOTH_BLE: {
                    const uint8_t bleChannels[] = { 2, 26, 80 };
                    for (int i = 0; i < 3; i++) {
                        set_channel_and_apply(radio1, bleChannels[i]);
                        set_channel_and_apply(radio2, bleChannels[i]);
                        vTaskDelay(pdMS_TO_TICKS(2));
                    }
                    break;
                }

                case MODE_DUAL_SWEEP:
                    set_channel_sweep(radio1, dual_ch1, dual_dir1);
                    set_channel_sweep(radio2, dual_ch2, dual_dir2);
                    vTaskDelay(pdMS_TO_TICKS(SWEEP_DELAY_MS));
                    break;

                case MODE_CONSTANT_CARRIER:
                    set_constant_carrier(radio1, 45);
                    set_constant_carrier(radio2, 45);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    break;

                default:
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));  /* yield so IDLE can run and task WDT is fed */
    }
}

// ============== HELPERS ==============
// Returns 0 = no event, 1 = short press (toggle jamming), 2 = long press (cycle mode)
static int check_button_event(void)
{
    int reading = gpio_get_level((gpio_num_t)BUTTON_PIN);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    if (reading == 0) {  // pressed
        if (lastButtonState == 1 && pressStartTime == 0)
            pressStartTime = now;
        lastButtonState = 0;
        return 0;
    }
    // released
    if (lastButtonState == 0 && pressStartTime != 0) {
        uint32_t duration = now - pressStartTime;
        s_last_press_duration_ms = duration;
        pressStartTime = 0;
        lastButtonState = 1;
        if (duration >= DEBOUNCE_MS)
            return duration >= LONG_PRESS_MS ? 2 : 1;
    }
    s_last_press_duration_ms = 0;
    lastButtonState = 1;
    pressStartTime = 0;
    return 0;
}

static void stop_jamming_radios(void)
{
    radio1.powerDown();
    radio2.powerDown();
}

// ---------- Built-in RGB LED: rainbow only while jamming is active ----------
static void hsv_to_rgb(uint32_t h, uint8_t s, uint8_t v, uint8_t* r, uint8_t* g, uint8_t* b)
{
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
    uint32_t region = h / 60;
    uint32_t remainder = (h % 60) * 256 / 60;
    uint32_t p = (v * (255 - s)) / 255;
    uint32_t q = (v * (255 - (s * remainder) / 256)) / 255;
    uint32_t t = (v * (255 - (s * (256 - remainder)) / 256)) / 255;
    switch (region) {
        case 0:  *r = (uint8_t)v; *g = (uint8_t)t; *b = (uint8_t)p; break;
        case 1:  *r = (uint8_t)q; *g = (uint8_t)v; *b = (uint8_t)p; break;
        case 2:  *r = (uint8_t)p; *g = (uint8_t)v; *b = (uint8_t)t; break;
        case 3:  *r = (uint8_t)p; *g = (uint8_t)q; *b = (uint8_t)v; break;
        case 4:  *r = (uint8_t)t; *g = (uint8_t)p; *b = (uint8_t)v; break;
        default: *r = (uint8_t)v; *g = (uint8_t)p; *b = (uint8_t)q; break;
    }
}

static void set_rgb_led(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_rmt_chan || !s_led_encoder) return;
    uint8_t grb[3] = { g, r, b };  // WS2812 order
    rmt_transmit_config_t tx_config = {};
    tx_config.loop_count = 0;
    ESP_ERROR_CHECK(rmt_transmit(s_rmt_chan, s_led_encoder, grb, sizeof(grb), &tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_rmt_chan, portMAX_DELAY));
}

static void init_rgb_led(void)
{
    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_cfg.gpio_num = (gpio_num_t)WS2812_GPIO;
    tx_cfg.mem_block_symbols = 64;
    tx_cfg.resolution_hz = RMT_RESOLUTION;
    tx_cfg.trans_queue_depth = 1;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &s_rmt_chan));
    led_strip_encoder_config_t enc_cfg = {};
    enc_cfg.resolution = RMT_RESOLUTION;
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&enc_cfg, &s_led_encoder));
    ESP_ERROR_CHECK(rmt_enable(s_rmt_chan));
    set_rgb_led(0, 0, 0);
}

static void rainbow_led_task(void* arg)
{
    (void)arg;
    uint32_t hue = 0;
    const uint8_t sat = 255;
    const uint8_t val = 200;
    for (;;) {
        if (!s_jamming_active) {
            set_rgb_led(0, 0, 0);  // LED off when jamming stopped
            vTaskDelay(pdMS_TO_TICKS(RAINBOW_MS_SLOW));
            continue;
        }
        // Slow rainbow for constant carrier, faster for cycle/sweep modes
        uint32_t delay_ms = (currentMode == MODE_CONSTANT_CARRIER) ? RAINBOW_MS_SLOW : RAINBOW_MS_FAST;
        uint8_t r, g, b;
        hsv_to_rgb(hue % 360, sat, val, &r, &g, &b);
        set_rgb_led(r, g, b);
        hue += 3;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static void disable_esp_wifi_only(void)
{
    // Only disable WiFi so the built-in BLE can run as third transmitter
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
        LOG_WRN("esp_wifi_stop: %s", esp_err_to_name(err));
    }
    err = esp_wifi_deinit();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
        LOG_WRN("esp_wifi_deinit: %s", esp_err_to_name(err));
    }
    LOG_INIT("WiFi disabled (BLE kept for 3rd transmitter)");
}

// ---------- BLE third transmitter (NimBLE non-connectable advertising) ----------
static void ble_on_sync(void);
static void ble_on_reset(int reason);
static void nimble_host_task(void* param);

static void start_ble_advertising(void)
{
    uint8_t own_addr_type;
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        LOG_ERR("BLE ensure addr failed: %d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        LOG_ERR("BLE infer addr failed: %d", rc);
        return;
    }

    struct ble_hs_adv_fields adv_fields = {0};
    struct ble_gap_adv_params adv_params = {0};

    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.name = (uint8_t*)"JAM";
    adv_fields.name_len = 3;
    adv_fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        LOG_ERR("BLE set adv fields failed: %d", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
    if (rc != 0) {
        LOG_ERR("BLE adv start failed: %d", rc);
        return;
    }
    LOG_BLE("Advertising started (3rd TX on built-in antenna)");
}

static void ble_on_sync(void)
{
    ble_svc_gap_device_name_set("JAM");
    start_ble_advertising();
}

static void ble_on_reset(int reason)
{
    LOG_WRN("NimBLE reset, reason %d", reason);
}

static void nimble_host_task(void* param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
    vTaskDelete(NULL);
}

static void init_ble_third_transmitter(void)
{
    // Order per ESP-IDF NimBLE_Beacon: port init first, then host config (store), then GAP, then host task
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        LOG_ERR("NimBLE init failed: %s", esp_err_to_name(ret));
        return;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    // No bonding/store for advertiser-only; skip store to avoid init crash
    ble_hs_cfg.store_status_cb = nullptr;
    // ble_store_config_init();  // not needed for non-connectable advertising

    ble_svc_gap_init();

    xTaskCreate(nimble_host_task, "nimble_host", 4096, NULL, 5, NULL);
    LOG_BLE("Host task started (3rd transmitter)");
}

static void init_nrf24_modules(void)
{
    LOG_INIT("SPI0 (HSPI): SCK=%d MISO=%d MOSI=%d CSN=%d", SCK_PIN_1, MISO_PIN_1, MOSI_PIN_1, CSN_PIN_1);
    spi0.begin(SCK_PIN_1, MISO_PIN_1, MOSI_PIN_1, CSN_PIN_1, 10000000);
    LOG_INIT("SPI1 (VSPI): SCK=%d MISO=%d MOSI=%d CSN=%d", SCK_PIN_2, MISO_PIN_2, MOSI_PIN_2, CSN_PIN_2);
    spi1.begin(SCK_PIN_2, MISO_PIN_2, MOSI_PIN_2, CSN_PIN_2, 10000000);

    bool module1_ok = false;
    bool module2_ok = false;

    LOG_RF1("begin() CE=%d CSN=%d...", CE_PIN_1, CSN_PIN_1);
    if (radio1.begin(&spi0)) {
        vTaskDelay(pdMS_TO_TICKS(100));
        radio1.setAutoAck(false);
        radio1.stopListening();
        radio1.setRetries(0, 0);
        radio1.setPayloadSize(5);
        radio1.setAddressWidth(3);
        radio1.setPALevel(RF24_PA_MAX, true);
        radio1.setDataRate(RF24_2MBPS);
        radio1.setCRCLength(RF24_CRC_DISABLED);
        bool conn = radio1.isChipConnected();
        LOG_RF1("OK | connected=%s", conn ? "yes" : "no");
        module1_ok = true;
    } else {
        LOG_ERR("RF1 (HSPI) begin FAILED - check wiring CE=%d CSN=%d", CE_PIN_1, CSN_PIN_1);
    }

    LOG_RF2("begin() CE=%d CSN=%d...", CE_PIN_2, CSN_PIN_2);
    if (radio2.begin(&spi1)) {
        vTaskDelay(pdMS_TO_TICKS(100));
        radio2.setAutoAck(false);
        radio2.stopListening();
        radio2.setRetries(0, 0);
        radio2.setPayloadSize(5);
        radio2.setAddressWidth(3);
        radio2.setPALevel(RF24_PA_MAX, true);
        radio2.setDataRate(RF24_2MBPS);
        radio2.setCRCLength(RF24_CRC_DISABLED);
        bool conn = radio2.isChipConnected();
        LOG_RF2("OK | connected=%s", conn ? "yes" : "no");
        module2_ok = true;
    } else {
        LOG_ERR("RF2 (VSPI) begin FAILED - check wiring CE=%d CSN=%d", CE_PIN_2, CSN_PIN_2);
    }

    if (!module1_ok && !module2_ok) {
        LOG_ERR("No NRF24 modules. Button/BLE still work.");
        return;
    }

    LOG_INIT("NRF24 config: AutoAck=OFF, PA=MAX, 2Mbps, CRC=OFF");
    if (module1_ok) {
        LOG_RF1("--- register dump ---");
        radio1.printDetails();
    }
    if (module2_ok) {
        LOG_RF2("--- register dump ---");
        radio2.printDetails();
    }

    LOG_JAM("Starting constant carrier at boot (ch=%d)...", (int)currentChannel);
    if (module1_ok) {
        radio1.startConstCarrier(RF24_PA_MAX, currentChannel);
        vTaskDelay(pdMS_TO_TICKS(15));
        LOG_RF1("ConstCarrier ON ch=%d", (int)radio1.getChannel());
    }
    if (module2_ok) {
        radio2.startConstCarrier(RF24_PA_MAX, currentChannel);
        vTaskDelay(pdMS_TO_TICKS(15));
        LOG_RF2("ConstCarrier ON ch=%d", (int)radio2.getChannel());
    }
    s_jamming_active = true;
    LOG_JAM("ON at boot. Short-press=stop, long-press=change mode.");
    LOG_INIT("Check: RF_SETUP bit7=CONT_WAVE, CE=HIGH. If no jam: antennas on, 3.3V>100mA/module, target 1-2m.");
    if (module1_ok) {
        LOG_RF1("--- after carrier ON (expect RF_CH=45, RF_SETUP bit7=1) ---");
        radio1.printDetails();
    }
    if (module2_ok) {
        LOG_RF2("--- after carrier ON (expect RF_CH=45, RF_SETUP bit7=1) ---");
        radio2.printDetails();
    }
}

// Re-apply full continuous carrier so CONT_WAVE + channel are definitely active.
static void set_channel_and_apply(RF24& radio, uint8_t channel)
{
    radio.startConstCarrier(RF24_PA_MAX, channel);
    vTaskDelay(pdMS_TO_TICKS(3));  // brief settle
}

static void set_channel_sweep(RF24& radio, uint8_t& channel, bool& dir)
{
    if (dir) {
        channel++;
        if (channel >= MAX_CHANNEL) {
            channel = MAX_CHANNEL;
            dir = false;
        }
    } else {
        if (channel > MIN_CHANNEL) channel--;
        if (channel <= MIN_CHANNEL) {
            channel = MIN_CHANNEL;
            dir = true;
        }
    }
    set_channel_and_apply(radio, channel);
}

static void set_constant_carrier(RF24& radio, uint8_t channel)
{
    set_channel_and_apply(radio, channel);
}

static void apply_jamming_mode_once(void)
{
    switch (currentMode) {
        case MODE_BLUETOOTH_CLASSIC:
            set_channel_and_apply(radio1, currentChannel);
            set_channel_and_apply(radio2, currentChannel);
            break;
        case MODE_BLUETOOTH_BLE: {
            const uint8_t bleChannels[] = { 2, 26, 80 };
            set_channel_and_apply(radio1, bleChannels[0]);
            set_channel_and_apply(radio2, bleChannels[0]);
            break;
        }
        case MODE_DUAL_SWEEP:
            set_channel_sweep(radio1, dual_ch1, dual_dir1);
            set_channel_sweep(radio2, dual_ch2, dual_dir2);
            break;
        case MODE_CONSTANT_CARRIER:
            set_constant_carrier(radio1, 45);
            set_constant_carrier(radio2, 45);
            break;
        default:
            break;
    }
}

static void print_mode(void)
{
    const char* modeStr = "UNKNOWN";
    switch (currentMode) {
        case MODE_BLUETOOTH_CLASSIC: modeStr = "BLUETOOTH CLASSIC (sweep 0-79)"; break;
        case MODE_BLUETOOTH_BLE:     modeStr = "BLUETOOTH BLE (advertising ch 2,26,80)"; break;
        case MODE_DUAL_SWEEP:        modeStr = "DUAL SWEEP (low + high)"; break;
        case MODE_CONSTANT_CARRIER:  modeStr = "CONSTANT CARRIER (ch 45)"; break;
        default: break;
    }
    LOG_MODE("Current: %s", modeStr);
}

static void switch_mode(void)
{
    int prev = (int)currentMode;
    currentMode = (JammerMode)((currentMode + 1) % MODE_COUNT);
    LOG_MODE("Switch %d -> %d", prev, (int)currentMode);
    print_mode();
    currentChannel = 45;
    directionUp = true;
    dual_ch1 = 0;
    dual_ch2 = 40;
    dual_dir1 = true;
    dual_dir2 = true;
    if (s_jamming_active) {
        radio1.startConstCarrier(RF24_PA_MAX, currentChannel);
        radio2.startConstCarrier(RF24_PA_MAX, currentChannel);
    }
}
