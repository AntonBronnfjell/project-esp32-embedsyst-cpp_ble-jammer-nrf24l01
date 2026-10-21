/**
 * BLE Scanner / Advertising Explorer — ESP32-S3 (NimBLE host)
 *
 * A read-only learning tool: performs a passive/active GAP scan and decodes
 * every advertisement it hears — address (+type), RSSI, flags, advertised
 * name, TX power, service UUIDs, and manufacturer data. Nothing is transmitted
 * beyond the standard active-scan SCAN_REQ; it connects to nothing.
 *
 * Use it to understand what BLE advertising actually looks like on the air:
 * how beacons, wearables, and peripherals present themselves.
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"

static const char* TAG = "BLESCAN";

#define C_OFF  "\033[0m"
#define C_GRN  "\033[1;32m"
#define C_YEL  "\033[1;33m"
#define C_CYN  "\033[1;36m"
#define C_DIM  "\033[90m"

static const char* addr_type_str(uint8_t t)
{
    switch (t) {
        case BLE_ADDR_PUBLIC:        return "public";
        case BLE_ADDR_RANDOM:        return "random";
        case BLE_ADDR_PUBLIC_ID:     return "public-id";
        case BLE_ADDR_RANDOM_ID:     return "random-id";
        default:                     return "?";
    }
}

// Rough signal-quality hint from RSSI (dBm).
static const char* rssi_hint(int8_t rssi)
{
    if (rssi >= -60) return C_GRN "near"  C_OFF;
    if (rssi >= -80) return C_YEL "mid"   C_OFF;
    return                   C_DIM "far"   C_OFF;
}

static void print_adv(const struct ble_gap_disc_desc* d)
{
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) != 0)
        return;

    char line[256];
    int n = snprintf(line, sizeof(line),
                     "%02x:%02x:%02x:%02x:%02x:%02x (%s) %4d dBm [%s]",
                     d->addr.val[5], d->addr.val[4], d->addr.val[3],
                     d->addr.val[2], d->addr.val[1], d->addr.val[0],
                     addr_type_str(d->addr.type), d->rssi, "");
    // splice the colored hint in place of the empty [] above
    n = snprintf(line, sizeof(line),
                 "%02x:%02x:%02x:%02x:%02x:%02x (%s) %4d dBm %s",
                 d->addr.val[5], d->addr.val[4], d->addr.val[3],
                 d->addr.val[2], d->addr.val[1], d->addr.val[0],
                 addr_type_str(d->addr.type), d->rssi, rssi_hint(d->rssi));

    if (f.name != NULL && f.name_len > 0) {
        n += snprintf(line + n, sizeof(line) - n, " name=\"%.*s\"%s",
                      f.name_len, f.name,
                      f.name_is_complete ? "" : "(short)");
    }
    if (f.tx_pwr_lvl_is_present)
        n += snprintf(line + n, sizeof(line) - n, " txpwr=%d", f.tx_pwr_lvl);

    // 16-bit service UUIDs
    for (int i = 0; i < f.num_uuids16 && n < (int)sizeof(line) - 8; i++)
        n += snprintf(line + n, sizeof(line) - n, " svc=0x%04x",
                      ble_uuid_u16(&f.uuids16[i].u));

    if (f.mfg_data != NULL && f.mfg_data_len >= 2) {
        uint16_t company = f.mfg_data[0] | (f.mfg_data[1] << 8);
        n += snprintf(line + n, sizeof(line) - n, " mfg=0x%04x(%db)",
                      company, f.mfg_data_len);
    }

    ESP_LOGI(TAG, "%s", line);
}

static int gap_event(struct ble_gap_event* event, void* arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            print_adv(&event->disc);
            return 0;
        case BLE_GAP_EVENT_DISC_COMPLETE:
            ESP_LOGW(TAG, "scan window complete (reason=%d) — restarting",
                     event->disc_complete.reason);
            return 0;
        default:
            return 0;
    }
}

static void start_scan(void)
{
    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        ESP_LOGE(TAG, "no usable BLE address");
        return;
    }

    struct ble_gap_disc_params p = {};
    p.itvl = 0;              // use stack default scan interval
    p.window = 0;            // use stack default scan window
    p.filter_duplicates = 0; // show every advert so RSSI updates are visible
    p.passive = 0;           // active scan (also requests SCAN_RSP data)
    p.limited = 0;

    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0)
        ESP_LOGE(TAG, "ble_gap_disc failed rc=%d", rc);
    else
        ESP_LOGI(TAG, C_GRN "[ok]" C_OFF " scanning (active, forever)...");
}

static void on_sync(void) { start_scan(); }
static void on_reset(int reason) { ESP_LOGW(TAG, "BLE host reset, reason=%d", reason); }

static void host_task(void* param)
{
    nimble_port_run();         // returns only on nimble_port_stop()
    nimble_port_freertos_deinit();
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, C_CYN "[init]" C_OFF " BLE scanner (read-only, connects to nothing)");

    if (nimble_port_init() != 0) {
        ESP_LOGE(TAG, "nimble_port_init failed");
        return;
    }
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    // Observer-only: no GAP device-name service needed (we never advertise).

    nimble_port_freertos_init(host_task);
}
