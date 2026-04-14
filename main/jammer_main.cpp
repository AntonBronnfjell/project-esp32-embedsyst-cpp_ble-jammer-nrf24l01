/**
 * ESP32-S3 Bluetooth Jammer — Dual NRF24L01+PA+LNA + ESP32 WiFi TX
 * Based on: RF-Clown, ESPnRF24-Jammer, Bruce firmware, and MyJammer
 * FOR EDUCATIONAL AND RESEARCH PURPOSES ONLY
 *
 * WARNING: Use of RF jammers is illegal in most countries.
 * This code is for understanding RF communications and
 * testing your own equipment's vulnerability ONLY.
 *
 * Three transmitters:
 *   NRF24 #1 — CW (continuous wave) with 130 µs PLL dwell per hop
 *   NRF24 #2 — CW (continuous wave) with 130 µs PLL dwell per hop
 *   ESP32 WiFi TX — 20 MHz wideband raw beacon frames on ch 1/6/11
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
#include "esp_wifi_types.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "led_strip_encoder.h"
#include "RF24.h"
#include "nRF24L01.h"

// NimBLE — used for BLE device discovery scan at startup
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

// WiFi raw TX as 3rd wideband transmitter (20 MHz bandwidth >> nRF24's 2 MHz)
static void init_wifi_jammer(void);
static void wifi_jam_task(void* arg);

// BLE device discovery (runs once at startup, before jamming)
static void ble_discovery_scan(void);

// Beacon frame: 20 MHz wideband energy on the selected WiFi channel
static const uint8_t s_beacon_frame[] = {
    0x80, 0x00,                                     // Frame Control: Beacon
    0x00, 0x00,                                     // Duration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,             // DA: Broadcast
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,             // SA
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,             // BSSID
    0x00, 0x00,                                     // Sequence
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
    0x64, 0x00,                                     // Beacon interval
    0x31, 0x04,                                     // Capabilities
    0x00, 0x01, 'X',                                // SSID (1 byte — minimal)
};

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

// Lower this (e.g. 1_000_000) if SPI probe shows garbage but wiring looks correct (long wires / breadboard).
#define JAMMER_NRF24_SPI_HZ 10000000

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
#define LED_BREATH_STEP_MS  15  // delay per brightness step in breathing animation

// ============== JAMMER CONFIGURATION ==============
#define MIN_CHANNEL   0
#define MAX_CHANNEL  79
// ============== GLOBAL VARIABLES ==============
SPI spi0(SPI2_HOST);
SPI spi1(SPI3_HOST);

RF24 radio1(CE_PIN_1, CSN_PIN_1);
RF24 radio2(CE_PIN_2, CSN_PIN_2);

static uint32_t xor_state = 0xDEADBEEF;

static inline uint32_t xorshift32(void) {
    xor_state ^= xor_state << 13;
    xor_state ^= xor_state >> 17;
    xor_state ^= xor_state << 5;
    return xor_state;
}

static inline uint8_t rand_channel(void) {
    return (uint8_t)(xorshift32() % (MAX_CHANNEL + 1));
}

enum JammerMode {
    MODE_BARRAGE,           // LED: slow rainbow    | random hop all 80 ch
    MODE_BLE_ADV_BARRAGE,   // LED: cyan breath     | R1=adv ctrl + R2=random data
    MODE_PERCEPTIVE,        // LED: magenta breath   | R1=adv+random interleave, R2=barrage
    MODE_BT_CLASSIC,        // LED: blue breath     | sequential sweep ch 2-80
    MODE_BLE_ALL,           // LED: green breath    | all 40 BLE channels
    MODE_BLE_ADV,           // LED: yellow breath   | ch 2,26,80 rapid cycle
    MODE_CONSTANT_CARRIER,  // LED: red breath      | CW on ch 45 (baseline test)
    MODE_COUNT
};

static volatile JammerMode currentMode = MODE_BARRAGE;
static volatile bool s_mode_changed = false;
static uint8_t sweep_ch1 = 2;
static uint8_t sweep_ch2 = 41;
static bool sweep_dir1 = true;
static bool sweep_dir2 = true;
static uint8_t ble_idx1 = 0;
static uint8_t ble_idx2 = 20;
static uint8_t ble_adv_idx = 0;
static uint8_t perceptive_cycle = 0;

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
static void log_nrf24_spi_probe(const char* bus_name, SPI& spi);
static void configure_radio_for_tx(RF24& radio);
static void hop_carrier(RF24& radio, uint8_t channel);
static void jam_barrage(void);
static void jam_ble_adv_barrage(void);
static void jam_perceptive(void);
static void jam_bt_classic(void);
static void jam_ble_all(void);
static void jam_ble_adv(void);
static void jam_constant_carrier(void);
static void print_mode(void);
static void switch_mode(void);
static int check_button_event(void);
static void stop_jamming_radios(void);
static void init_rgb_led(void);
static void rainbow_led_task(void* arg);
static void jam_task(void* arg);

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

    LOG_INIT("WiFi raw TX (3rd transmitter, 20 MHz wideband)...");
    init_wifi_jammer();

    LOG_INIT("BLE device discovery (startup scan)...");
    ble_discovery_scan();

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

    xTaskCreatePinnedToCore(jam_task, "jam", 4096, NULL, configMAX_PRIORITIES - 1, NULL, 1);

    while (1) {
        int ev = check_button_event();
        if (ev == 1) {
            LOG_BTN("Short press (%u ms) -> toggle jamming", (unsigned)s_last_press_duration_ms);
            s_jamming_active = !s_jamming_active;
            if (s_jamming_active) LOG_JAM("ON");
            else                  LOG_JAM_OFF("OFF");
        } else if (ev == 2) {
            LOG_BTN("Long press (%u ms) -> cycle mode", (unsigned)s_last_press_duration_ms);
            switch_mode();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
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

// Per-mode LED color (R, G, B at full brightness).
// BARRAGE uses rainbow (hue cycling), all others use a fixed color with breathing.
static void get_mode_color(JammerMode mode, uint8_t *r, uint8_t *g, uint8_t *b)
{
    switch (mode) {
        case MODE_BLE_ADV_BARRAGE:  *r =   0; *g = 200; *b = 200; break; // cyan
        case MODE_PERCEPTIVE:       *r = 200; *g =   0; *b = 200; break; // magenta
        case MODE_BT_CLASSIC:       *r =   0; *g =   0; *b = 200; break; // blue
        case MODE_BLE_ALL:          *r =   0; *g = 200; *b =   0; break; // green
        case MODE_BLE_ADV:          *r = 200; *g = 200; *b =   0; break; // yellow
        case MODE_CONSTANT_CARRIER: *r = 200; *g =   0; *b =   0; break; // red
        default:                    *r = 200; *g = 200; *b = 200; break; // white fallback
    }
}

static void rainbow_led_task(void* arg)
{
    (void)arg;
    uint32_t hue = 0;
    uint8_t breath = 0;
    bool breath_up = true;

    for (;;) {
        if (!s_jamming_active) {
            set_rgb_led(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(50));
            breath = 0;
            breath_up = true;
            continue;
        }

        if (currentMode == MODE_BARRAGE) {
            // Slow rainbow cycle for barrage
            uint8_t r, g, b;
            hsv_to_rgb(hue % 360, 255, 180, &r, &g, &b);
            set_rgb_led(r, g, b);
            hue += 2;
            vTaskDelay(pdMS_TO_TICKS(60));
        } else {
            // Breathing animation: brightness ramps 0 → 200 → 0
            uint8_t base_r, base_g, base_b;
            get_mode_color(currentMode, &base_r, &base_g, &base_b);

            uint8_t r = (uint8_t)((uint16_t)base_r * breath / 200);
            uint8_t g = (uint8_t)((uint16_t)base_g * breath / 200);
            uint8_t b = (uint8_t)((uint16_t)base_b * breath / 200);
            set_rgb_led(r, g, b);

            if (breath_up) {
                if (breath >= 200) breath_up = false;
                else               breath += 2;
            } else {
                if (breath <= 2)   breath_up = true;
                else               breath -= 2;
            }
            vTaskDelay(pdMS_TO_TICKS(LED_BREATH_STEP_MS));
        }
    }
}

// ---------- WiFi raw TX: 3rd transmitter using ESP32's built-in 2.4 GHz radio ----------
// Each WiFi channel is 20 MHz wide — covers ~20 Bluetooth channels at once.
// WiFi ch 1 = 2412 MHz (BT ch 12), ch 6 = 2437 MHz (BT ch 37), ch 11 = 2462 MHz (BT ch 62)

static void init_wifi_jammer(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
    esp_wifi_set_max_tx_power(80);

    xTaskCreatePinnedToCore(wifi_jam_task, "wifi_jam", 4096, NULL, 5, NULL, 0);
    LOG_INIT("WiFi raw TX started (3rd transmitter, built-in antenna)");
}

static void wifi_jam_task(void* arg)
{
    (void)arg;
    static const uint8_t wifi_channels[] = { 1, 6, 11 };
    uint8_t ch_idx = 0;
    uint32_t pkt_count = 0;

    for (;;) {
        if (!s_jamming_active) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        esp_wifi_80211_tx(WIFI_IF_STA, s_beacon_frame, sizeof(s_beacon_frame), false);

        if (++pkt_count >= 50) {
            pkt_count = 0;
            ch_idx = (ch_idx + 1) % 3;
            esp_wifi_set_channel(wifi_channels[ch_idx], WIFI_SECOND_CHAN_NONE);
        }

        vTaskDelay(1);
    }
}

// ---------- BLE device discovery: one-shot scan at startup ----------

static volatile bool s_ble_scan_done = false;
static volatile bool s_ble_synced = false;

static int ble_disc_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc *desc = &event->disc;
        char addr_str[18];
        snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 desc->addr.val[5], desc->addr.val[4], desc->addr.val[3],
                 desc->addr.val[2], desc->addr.val[1], desc->addr.val[0]);

        const char *name = "(unknown)";
        char name_buf[32] = {0};
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, desc->data, desc->length_data) == 0
            && fields.name != NULL && fields.name_len > 0) {
            uint8_t copy_len = fields.name_len < sizeof(name_buf) - 1
                             ? fields.name_len : (uint8_t)(sizeof(name_buf) - 1);
            memcpy(name_buf, fields.name, copy_len);
            name_buf[copy_len] = '\0';
            name = name_buf;
        }

        LOG_BLE("FOUND  %s  rssi=%d  name=\"%s\"", addr_str, (int)desc->rssi, name);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        s_ble_scan_done = true;
    }
    return 0;
}

static void ble_disc_on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    s_ble_synced = true;
}

static void ble_disc_on_reset(int reason)
{
    (void)reason;
    s_ble_synced = false;
}

static void ble_disc_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    vTaskDelete(NULL);
}

static void ble_discovery_scan(void)
{
    esp_log_level_set("NimBLE", ESP_LOG_WARN);

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        LOG_WRN("NimBLE init failed (%s) — skipping BLE discovery", esp_err_to_name(ret));
        return;
    }

    ble_hs_cfg.reset_cb = ble_disc_on_reset;
    ble_hs_cfg.sync_cb  = ble_disc_on_sync;
    ble_svc_gap_init();
    ble_svc_gap_device_name_set("scanner");

    nimble_port_freertos_init(ble_disc_host_task);

    for (int i = 0; i < 40 && !s_ble_synced; i++)
        vTaskDelay(pdMS_TO_TICKS(50));

    if (!s_ble_synced) {
        LOG_WRN("NimBLE did not sync — skipping BLE discovery");
        nimble_port_stop();
        nimble_port_deinit();
        return;
    }

    LOG_BLE("Scanning for nearby BLE devices (3 seconds)...");

    struct ble_gap_disc_params scan_params = {};
    scan_params.passive = 1;
    scan_params.itvl = 0x0010;
    scan_params.window = 0x0010;
    scan_params.filter_duplicates = 1;

    s_ble_scan_done = false;
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 3000, &scan_params,
                          ble_disc_gap_event, NULL);
    if (rc != 0) {
        LOG_WRN("BLE scan start failed (rc=%d)", rc);
    } else {
        while (!s_ble_scan_done)
            vTaskDelay(pdMS_TO_TICKS(100));
    }

    LOG_BLE("Discovery complete.");
    nimble_port_stop();
    nimble_port_deinit();
}

static void log_nrf24_spi_probe(const char* bus_name, SPI& spi)
{
    uint8_t st_aw = 0, aw = 0;
    uint8_t st_cfg = 0, cfg = 0;
    if (!spi.readNrf24Register(SETUP_AW, &st_aw, &aw)) {
        LOG_WRN("%s SPI probe: bus not initialized", bus_name);
        return;
    }
    (void)spi.readNrf24Register(NRF_CONFIG, &st_cfg, &cfg);
    LOG_WRN("%s SPI probe: SETUP_AW=0x%02x (status=0x%02x) CONFIG=0x%02x (status=0x%02x)",
            bus_name, (unsigned)aw, (unsigned)st_aw, (unsigned)cfg, (unsigned)st_cfg);
    LOG_WRN("%s If SETUP_AW is 0x03 and CONFIG ~0x08: chip likely OK (check CE/SPI speed). 0xFF/0x00: MISO floating or MOSI/SCK/CSN wrong, or no 3.3V/GND.",
            bus_name);
}

static void init_nrf24_modules(void)
{
    LOG_INIT("SPI0 (HSPI): SCK=%d MISO=%d MOSI=%d CSN=%d", SCK_PIN_1, MISO_PIN_1, MOSI_PIN_1, CSN_PIN_1);
    spi0.begin(SCK_PIN_1, MISO_PIN_1, MOSI_PIN_1, CSN_PIN_1, JAMMER_NRF24_SPI_HZ);
    LOG_INIT("SPI1 (VSPI): SCK=%d MISO=%d MOSI=%d CSN=%d", SCK_PIN_2, MISO_PIN_2, MOSI_PIN_2, CSN_PIN_2);
    spi1.begin(SCK_PIN_2, MISO_PIN_2, MOSI_PIN_2, CSN_PIN_2, JAMMER_NRF24_SPI_HZ);

    bool module1_ok = false;
    bool module2_ok = false;

    LOG_RF1("begin() CE=%d CSN=%d...", CE_PIN_1, CSN_PIN_1);
    if (radio1.begin(&spi0)) {
        vTaskDelay(pdMS_TO_TICKS(100));
        LOG_RF1("OK | connected=%s", radio1.isChipConnected() ? "yes" : "no");
        module1_ok = true;
    } else {
        LOG_ERR("RF1 (HSPI) begin FAILED - check wiring CE=%d CSN=%d", CE_PIN_1, CSN_PIN_1);
        log_nrf24_spi_probe("RF1", spi0);
    }

    LOG_RF2("begin() CE=%d CSN=%d...", CE_PIN_2, CSN_PIN_2);
    if (radio2.begin(&spi1)) {
        vTaskDelay(pdMS_TO_TICKS(100));
        LOG_RF2("OK | connected=%s", radio2.isChipConnected() ? "yes" : "no");
        module2_ok = true;
    } else {
        LOG_ERR("RF2 (VSPI) begin FAILED - check wiring CE=%d CSN=%d", CE_PIN_2, CSN_PIN_2);
        log_nrf24_spi_probe("RF2", spi1);
    }

    if (!module1_ok && !module2_ok) {
        LOG_ERR("No NRF24 modules. Button/BLE still work.");
        LOG_WRN("Hardware check: 3.3V on VCC, common GND, swap MISO/MOSI if probe is 0xFF/0x00; test module on a known-good Arduino+nRF24 sketch.");
        return;
    }

    // ---- RF self-test: each radio transmits CW, the other scans RPD ----
    // RPD triggers at >= -64 dBm. PA+LNA modules vary in output; a FAIL here
    // may just mean the signal is marginal at the inter-module distance, not
    // that the radio is dead. If isChipConnected() returns true and registers
    // look correct, the module is almost certainly transmitting.
    if (module1_ok && module2_ok) {
        const uint8_t test_ch = 50;
        const int attempts = 3;
        const int dwell_ms = 20;
        LOG_INIT("=== RF SELF-TEST on ch %d (%d attempts, %d ms dwell) ===",
                 test_ch, attempts, dwell_ms);

        // Test 1: RF1 TX → RF2 RX
        bool rpd_r2 = false;
        for (int attempt = 0; attempt < attempts && !rpd_r2; attempt++) {
            radio1.begin(&spi0);
            radio2.begin(&spi1);
            vTaskDelay(pdMS_TO_TICKS(5));

            radio2.setChannel(test_ch);
            radio2.startListening();
            vTaskDelay(pdMS_TO_TICKS(2));

            radio1.stopListening();
            radio1.startConstCarrier(RF24_PA_MAX, test_ch);
            vTaskDelay(pdMS_TO_TICKS(dwell_ms));

            rpd_r2 = radio2.testRPD();
            radio1.stopConstCarrier();
            radio2.stopListening();

            if (!rpd_r2 && attempt < attempts - 1)
                vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (rpd_r2)
            LOG_RF1("TX -> RF2 detected signal (RPD=1). RF1 IS transmitting.");
        else
            LOG_WRN("TX -> RF2: RPD=0 after %d attempts. RF1 signal below -64 dBm at module distance. "
                     "Likely OK if isChipConnected=yes — check antenna seating / PA module quality.",
                     attempts);

        // Test 2: RF2 TX → RF1 RX
        bool rpd_r1 = false;
        for (int attempt = 0; attempt < attempts && !rpd_r1; attempt++) {
            radio1.begin(&spi0);
            radio2.begin(&spi1);
            vTaskDelay(pdMS_TO_TICKS(5));

            radio1.setChannel(test_ch);
            radio1.startListening();
            vTaskDelay(pdMS_TO_TICKS(2));

            radio2.stopListening();
            radio2.startConstCarrier(RF24_PA_MAX, test_ch);
            vTaskDelay(pdMS_TO_TICKS(dwell_ms));

            rpd_r1 = radio1.testRPD();
            radio2.stopConstCarrier();
            radio1.stopListening();

            if (!rpd_r1 && attempt < attempts - 1)
                vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (rpd_r1)
            LOG_RF2("TX -> RF1 detected signal (RPD=1). RF2 IS transmitting.");
        else
            LOG_WRN("TX -> RF1: RPD=0 after %d attempts. RF2 signal below -64 dBm at module distance. "
                     "Likely OK if isChipConnected=yes — check antenna seating / PA module quality.",
                     attempts);

        const char* r1_str = rpd_r2 ? "TX_OK" : "TX_WEAK";
        const char* r2_str = rpd_r1 ? "TX_OK" : "TX_WEAK";
        LOG_INIT("=== RF SELF-TEST done: RF1=%s RF2=%s (chip1=%s chip2=%s) ===",
                 r1_str, r2_str,
                 radio1.isChipConnected() ? "connected" : "DISCONNECTED",
                 radio2.isChipConnected() ? "connected" : "DISCONNECTED");
    }

    if (module1_ok) configure_radio_for_tx(radio1);
    if (module2_ok) configure_radio_for_tx(radio2);

    LOG_INIT("NRF24 CW config: AutoAck=OFF, PA=MAX, 2Mbps, CRC=OFF, 130us PLL dwell");
    if (module1_ok) { LOG_RF1("--- register dump ---"); radio1.printDetails(); }
    if (module2_ok) { LOG_RF2("--- register dump ---"); radio2.printDetails(); }

    s_jamming_active = true;
    LOG_JAM("ON at boot (BARRAGE). Short-press=stop, long-press=change mode.");
}

static void configure_radio_for_tx(RF24& radio)
{
    radio.powerUp();
    vTaskDelay(pdMS_TO_TICKS(5));
    radio.stopListening();
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.setPALevel(RF24_PA_MAX, true);
    radio.setDataRate(RF24_2MBPS);
    radio.setCRCLength(RF24_CRC_DISABLED);
    radio.startConstCarrier(RF24_PA_MAX, 45);
}

// CW hop: just retune the PLL. CE stays HIGH from startConstCarrier() so the
// carrier never stops — 100% RF duty cycle. The 130 µs dwell lets the PLL
// lock on the new channel before we hop again.
static void hop_carrier(RF24& radio, uint8_t channel)
{
    radio.setChannel(channel);
    esp_rom_delay_us(130);
}

static void jam_task(void* arg)
{
    (void)arg;
    uint32_t status_count = 0;
    uint32_t yield_count = 0;
    bool was_active = false;

    for (;;) {
        if (!s_jamming_active) {
            if (was_active) {
                stop_jamming_radios();
                LOG_JAM_OFF("Radios powered down");
                was_active = false;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            status_count = 0;
            yield_count = 0;
            continue;
        }

        if (!was_active || s_mode_changed) {
            s_mode_changed = false;
            configure_radio_for_tx(radio1);
            configure_radio_for_tx(radio2);
            sweep_ch1 = 2;  sweep_ch2 = 41;
            sweep_dir1 = true; sweep_dir2 = true;
            ble_idx1 = 0; ble_idx2 = 20;
            ble_adv_idx = 0;
            perceptive_cycle = 0;
            if (!was_active) {
                LOG_JAM("Radios active | R1=%s R2=%s",
                        radio1.isChipConnected() ? "ok" : "FAIL",
                        radio2.isChipConnected() ? "ok" : "FAIL");
            }
            was_active = true;
        }

        switch (currentMode) {
            case MODE_BARRAGE:          jam_barrage(); break;
            case MODE_BLE_ADV_BARRAGE:  jam_ble_adv_barrage(); break;
            case MODE_PERCEPTIVE:       jam_perceptive(); break;
            case MODE_BT_CLASSIC:       jam_bt_classic(); break;
            case MODE_BLE_ALL:          jam_ble_all(); break;
            case MODE_BLE_ADV:          jam_ble_adv(); break;
            case MODE_CONSTANT_CARRIER: jam_constant_carrier(); break;
            default: break;
        }
        if (++status_count >= 50000) {
            status_count = 0;
            LOG_JAM("Status: active | mode=%d R1=%s R2=%s", (int)currentMode,
                    radio1.isChipConnected() ? "ok" : "FAIL",
                    radio2.isChipConnected() ? "ok" : "FAIL");
        }
        if (++yield_count >= 200) {
            yield_count = 0;
            vTaskDelay(1);
        }
    }
}

// MODE 0: BARRAGE — CW random hop across all 80 channels with 130 µs PLL dwell.
// Both radios independent random = max unpredictability for AFH.
// ~6600 hops/s per radio with CW always on (100% RF duty cycle).
static void jam_barrage(void)
{
    hop_carrier(radio1, rand_channel());
    hop_carrier(radio2, rand_channel());
}

// BLE advertising channels (shared by multiple modes)
// ch37=2402→nRF ch2, ch38=2426→nRF ch26, ch39=2480→nRF ch80
static const uint8_t ble_adv_channels[] = { 2, 26, 80 };

// MODE 1: BLE ADV + BARRAGE — hybrid attack on both control and data planes.
// Radio 1 rapid-cycles the 3 BLE adv channels (same pattern that worked before).
// Radio 2 does random barrage across all 80 channels — keeps AFH busy.
static void jam_ble_adv_barrage(void)
{
    hop_carrier(radio1, ble_adv_channels[ble_adv_idx]);
    ble_adv_idx = (ble_adv_idx + 1) % 3;
    hop_carrier(radio2, rand_channel());
}

// BT Classic: all 79 FHSS data channels (nRF24 ch 2-80)
static const uint8_t bt_classic_channels[] = {
     2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
    66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80,
};
#define BT_CLASSIC_CH_COUNT (sizeof(bt_classic_channels) / sizeof(bt_classic_channels[0]))

// MODE 1: BT CLASSIC — CW sequential sweep ch 2-80 with 130 µs PLL dwell.
// Radios sweep in opposite directions for max coverage.
static void jam_bt_classic(void)
{
    hop_carrier(radio1, bt_classic_channels[sweep_ch1]);
    hop_carrier(radio2, bt_classic_channels[sweep_ch2]);

    if (sweep_dir1) { if (++sweep_ch1 >= BT_CLASSIC_CH_COUNT - 1) sweep_dir1 = false; }
    else            { if (sweep_ch1 == 0) sweep_dir1 = true; else sweep_ch1--; }

    if (sweep_dir2) { if (++sweep_ch2 >= BT_CLASSIC_CH_COUNT - 1) sweep_dir2 = false; }
    else            { if (sweep_ch2 == 0) sweep_dir2 = true; else sweep_ch2--; }
}

// BLE: all 40 data + advertising channels (even nRF24 channels 2-80)
static const uint8_t ble_all_channels[] = {
     2,  4,  6,  8, 10, 12, 14, 16, 18, 20,
    22, 24, 26, 28, 30, 32, 34, 36, 38, 40,
    42, 44, 46, 48, 50, 52, 54, 56, 58, 60,
    62, 64, 66, 68, 70, 72, 74, 76, 78, 80,
};
#define BLE_ALL_CH_COUNT (sizeof(ble_all_channels) / sizeof(ble_all_channels[0]))

// MODE 2: BLE ALL — CW cycle through all 40 BLE channels, radios offset by 20
static void jam_ble_all(void)
{
    hop_carrier(radio1, ble_all_channels[ble_idx1]);
    hop_carrier(radio2, ble_all_channels[ble_idx2]);
    ble_idx1 = (ble_idx1 + 1) % BLE_ALL_CH_COUNT;
    ble_idx2 = (ble_idx2 + 1) % BLE_ALL_CH_COUNT;
}

// MODE 4: BLE ADV — rapid-cycle both radios across the 3 BLE adv channels.
// Each radio hits a different channel per iteration, cycling fast.
static void jam_ble_adv(void)
{
    hop_carrier(radio1, ble_adv_channels[ble_adv_idx]);
    ble_adv_idx = (ble_adv_idx + 1) % 3;
    hop_carrier(radio2, ble_adv_channels[ble_adv_idx]);
    ble_adv_idx = (ble_adv_idx + 1) % 3;
}

// ---------- MODE: PERCEPTIVE — multi-vector aggressive targeting ----------
// Combines all effective attack vectors in a single mode:
//   Radio 1: interleaves BLE adv channels (control plane disruption) with
//            random barrage hops (data plane disruption) in a 1:3 ratio.
//   Radio 2: pure random barrage across all 80 channels.
// The interleaving means R1 hits an adv channel every 4th hop, keeping
// sustained pressure on the BLE control connection while also contributing
// to data-plane coverage. WiFi TX runs independently as always.
static void jam_perceptive(void)
{
    if ((perceptive_cycle & 0x03) == 0) {
        hop_carrier(radio1, ble_adv_channels[ble_adv_idx]);
        ble_adv_idx = (ble_adv_idx + 1) % 3;
    } else {
        hop_carrier(radio1, rand_channel());
    }
    perceptive_cycle++;
    hop_carrier(radio2, rand_channel());
}

// MODE 6: CONSTANT CARRIER — CW on ch 45 (baseline / single-channel test)
static void jam_constant_carrier(void)
{
    hop_carrier(radio1, 45);
    hop_carrier(radio2, 45);
}

static void print_mode(void)
{
    const char* modeStr = "UNKNOWN";
    switch (currentMode) {
        case MODE_BARRAGE:          modeStr = "BARRAGE [rainbow] random hop all 80ch"; break;
        case MODE_BLE_ADV_BARRAGE:  modeStr = "ADV+BARRAGE [cyan] R1=adv ctrl, R2=random"; break;
        case MODE_PERCEPTIVE:       modeStr = "PERCEPTIVE [magenta] R1=adv+random, R2=barrage"; break;
        case MODE_BT_CLASSIC:       modeStr = "BT CLASSIC [blue] sweep ch 2-80"; break;
        case MODE_BLE_ALL:          modeStr = "BLE ALL [green] 40 BLE channels"; break;
        case MODE_BLE_ADV:          modeStr = "BLE ADV [yellow] ch 2,26,80 rapid"; break;
        case MODE_CONSTANT_CARRIER: modeStr = "CONSTANT CARRIER [red] ch 45 test"; break;
        default: break;
    }
    LOG_MODE("Current: %s", modeStr);
}

static void switch_mode(void)
{
    int prev = (int)currentMode;
    currentMode = (JammerMode)((currentMode + 1) % MODE_COUNT);
    s_mode_changed = true;
    LOG_MODE("Switch %d -> %d", prev, (int)currentMode);
    print_mode();
}
