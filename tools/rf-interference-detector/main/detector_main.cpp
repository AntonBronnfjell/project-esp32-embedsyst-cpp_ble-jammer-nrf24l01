/**
 * 2.4 GHz Interference / Jamming DETECTOR — ESP32-S3 + nRF24L01+
 *
 * DEFENSIVE / RECEIVE-ONLY. This firmware never transmits. It uses the
 * nRF24L01+ Received Power Detector (RPD) to passively sample energy across
 * the 2.4 GHz band, build an occupancy spectrogram, and flag the signature of
 * wideband interference (a jammer, a microwave oven, a misbehaving device).
 *
 * How it works:
 *   The RPD latch on the nRF24L01+ goes high while the radio is listening and
 *   sees carrier power above roughly -64 dBm on the tuned channel. By hopping
 *   the receiver across all 126 channels and sampling the latch many times per
 *   channel, we estimate per-channel occupancy (0-100%). Normal traffic (BLE
 *   adverts, Wi-Fi, a mouse dongle) is sparse and bursty; a jammer lights up a
 *   wide, contiguous block of channels continuously — that contrast is the
 *   detection.
 *
 * Reuses nRF24 module #1 from the companion project (SPI2_HOST).
 *   CE=16 CSN=15 SCK=14 MOSI=13 MISO=12
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "RF24.h"
#include "nRF24L01.h"

static const char* TAG = "RFDET";

// ---- ANSI colors (match the house style of the companion firmware) ----
#define C_OFF  "\033[0m"
#define C_RED  "\033[1;31m"
#define C_GRN  "\033[1;32m"
#define C_YEL  "\033[1;33m"
#define C_CYN  "\033[1;36m"
#define C_DIM  "\033[90m"

// ---- nRF24 #1 wiring (HSPI / SPI2_HOST) ----
#define CE_PIN    16
#define CSN_PIN   15
#define SCK_PIN   14
#define MOSI_PIN  13
#define MISO_PIN  12
#define NRF24_SPI_HZ 10000000

// ---- Sweep parameters ----
#define NUM_CHANNELS      126   // nRF24 tunes 2400..2525 MHz (ch 0..125)
#define SAMPLES_PER_CH    128   // RPD samples averaged per channel per sweep
#define RPD_SETTLE_US     130   // PLL settle after a channel change (datasheet ~130 us)

// A channel counts as "hot" when occupancy is at/above this percentage.
#define HOT_THRESHOLD_PCT 50
// Wideband-interference heuristic: this many hot channels inside one 25-ch
// window (one sweep) looks like deliberate/broadband jamming rather than
// ordinary bursty traffic.
#define WIDEBAND_WINDOW       25
#define WIDEBAND_HOT_IN_WIN   18

SPI spi0(SPI2_HOST);
RF24 radio(CE_PIN, CSN_PIN);

static uint8_t occupancy[NUM_CHANNELS];  // 0..100 per channel, last sweep

// Configure the radio as a passive, maximally-sensitive listener.
static void configure_receiver(void)
{
    radio.setAutoAck(false);         // we are only sensing energy, never ACKing
    radio.disableCRC();              // accept anything; we only read the RPD latch
    radio.setDataRate(RF24_250KBPS); // narrowest bandwidth => most sensitive RPD
    radio.setPALevel(RF24_PA_MIN);   // irrelevant for RX, keep the PLL quiet
    // A "noise" address so we never match real payloads and get stuck in RX FIFO.
    const uint8_t noise_addr[5] = {0x55, 0x55, 0x55, 0x55, 0x55};
    radio.openReadingPipe(0, noise_addr);
    radio.setChannel(0);
}

// Sample one channel: returns occupancy 0..100 (% of samples with RPD high).
static uint8_t sample_channel(uint8_t ch)
{
    radio.setChannel(ch);
    uint16_t hits = 0;
    for (uint16_t i = 0; i < SAMPLES_PER_CH; i++) {
        radio.startListening();
        esp_rom_delay_us(RPD_SETTLE_US);
        radio.stopListening();
        if (radio.testRPD()) hits++;
    }
    return (uint8_t)((hits * 100UL) / SAMPLES_PER_CH);
}

// Render one channel's occupancy as a short colored bar.
static const char* bar_for(uint8_t pct, char* buf, size_t n)
{
    static const char* blocks = "_.:-=+*#%@";
    int level = (pct * 9) / 100;            // 0..9
    const char* color = pct >= HOT_THRESHOLD_PCT ? C_RED
                      : pct >= 20              ? C_YEL
                      : pct > 0                ? C_GRN : C_DIM;
    snprintf(buf, n, "%s%c%s", color, blocks[level], C_OFF);
    return buf;
}

// Return true if this sweep shows a wideband-interference signature.
static bool detect_wideband(const uint8_t* occ, int* worst_window_start)
{
    int best_hot = 0, best_start = 0;
    for (int start = 0; start + WIDEBAND_WINDOW <= NUM_CHANNELS; start++) {
        int hot = 0;
        for (int i = 0; i < WIDEBAND_WINDOW; i++)
            if (occ[start + i] >= HOT_THRESHOLD_PCT) hot++;
        if (hot > best_hot) { best_hot = hot; best_start = start; }
    }
    if (worst_window_start) *worst_window_start = best_start;
    return best_hot >= WIDEBAND_HOT_IN_WIN;
}

static void print_sweep(uint32_t sweep_no)
{
    char cell[24];
    // Header row: tens digit of channel number, every 10 channels.
    printf(C_CYN "[sweep %lu] " C_OFF "2.400 GHz ", (unsigned long)sweep_no);
    for (int ch = 0; ch < NUM_CHANNELS; ch++)
        printf("%s", bar_for(occupancy[ch], cell, sizeof(cell)));
    printf(" 2.525 GHz\n");

    int hot_total = 0, peak_ch = 0;
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        if (occupancy[ch] >= HOT_THRESHOLD_PCT) hot_total++;
        if (occupancy[ch] > occupancy[peak_ch]) peak_ch = ch;
    }

    int win_start = 0;
    bool wideband = detect_wideband(occupancy, &win_start);
    if (wideband) {
        ESP_LOGW(TAG, C_RED "[ALERT]" C_OFF
                 " wideband interference signature: ch %d-%d saturated "
                 "(%d/%d hot, peak ch%d @ %u%%, ~%u MHz)",
                 win_start, win_start + WIDEBAND_WINDOW - 1, hot_total,
                 NUM_CHANNELS, peak_ch, occupancy[peak_ch], 2400 + peak_ch);
    } else {
        ESP_LOGI(TAG, C_GRN "[ok]" C_OFF
                 " %d/%d channels busy, peak ch%d @ %u%% (~%u MHz)",
                 hot_total, NUM_CHANNELS, peak_ch, occupancy[peak_ch],
                 2400 + peak_ch);
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, C_CYN "[init]" C_OFF
             " 2.4 GHz interference detector (RECEIVE-ONLY, never transmits)");

    spi0.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CSN_PIN, NRF24_SPI_HZ);
    if (!radio.begin(&spi0)) {
        ESP_LOGE(TAG, C_RED "[err]" C_OFF
                 " nRF24 not responding on SPI2 — check wiring/power (3.3V, "
                 "decoupling cap) and that CE=%d CSN=%d are correct",
                 CE_PIN, CSN_PIN);
        return;
    }
    ESP_LOGI(TAG, C_GRN "[ok]" C_OFF " nRF24 connected=%s",
             radio.isChipConnected() ? "yes" : "no");

    configure_receiver();
    ESP_LOGI(TAG, "scanning %d channels, %d samples each, hot>=%d%% ...",
             NUM_CHANNELS, SAMPLES_PER_CH, HOT_THRESHOLD_PCT);

    uint32_t sweep = 0;
    for (;;) {
        for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
            occupancy[ch] = sample_channel(ch);
        print_sweep(++sweep);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
