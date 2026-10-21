<div align="center">

# 🔵 BLE Scanner / Advertising Explorer

**Read-only** Bluetooth Low Energy observer for ESP32-S3 (NimBLE host)

![platform](https://img.shields.io/badge/platform-ESP32--S3-E7352C?logo=espressif&logoColor=white)
![stack](https://img.shields.io/badge/BLE-NimBLE-0082FC?logo=bluetooth&logoColor=white)
![role](https://img.shields.io/badge/role-observer-success)
![connects](https://img.shields.io/badge/connects-to%20nothing-success)

</div>

> [!NOTE]
> This uses the ESP32-S3's **real BLE radio and stack** (not the nRF24). It
> performs a standard GAP scan, decodes advertisements, and connects to
> nothing. It's a learning tool for seeing what BLE actually looks like on air.

---

## What it shows

For every advertisement it hears, it decodes and prints:

- **Address** + address type (`public` / `random` / resolvable identity)
- **RSSI** in dBm, with a `near` / `mid` / `far` hint
- Advertised **name** (complete or shortened)
- **TX power**, advertised **service UUIDs** (16-bit)
- **Manufacturer data** company ID + length

<div align="center">

```
a4:c1:38:9f:2b:0e (public)  -57 dBm near name="Govee_H5075" svc=0x1809 mfg=0x004c(23b)
7c:64:56:12:ab:cd (random)  -83 dBm far  svc=0xfe9f
```

</div>

## Why NimBLE here (and nRF24 in the detector)?

<table>
<tr><th>Tool</th><th>Radio</th><th>Good for</th></tr>
<tr><td>This scanner</td><td>ESP32-S3 built-in BLE</td><td>decoding real BLE packets, names, UUIDs, GATT later</td></tr>
<tr><td>Interference detector</td><td>nRF24L01+</td><td>raw wideband energy / occupancy across 2.4 GHz</td></tr>
</table>

The nRF24 can only see *energy*; it can't demodulate BLE. The ESP32 radio
speaks BLE natively, so it's the right tool for actually reading adverts.

## Build & flash

```bash
cd tools/ble-scanner
idf.py set-target esp32s3
idf.py build flash monitor
```

The included [`sdkconfig.defaults`](sdkconfig.defaults) enables NimBLE in
observer/central roles only (peripheral + broadcaster are off — this device
never advertises).

<details>
<summary><b>Where to take it next</b></summary>

<br>

- Add `filter_duplicates = 1` in `start_scan()` for a quieter, de-duplicated list.
- Decode iBeacon / Eddystone from the manufacturer / service data.
- Connect to a peripheral and walk its GATT services (`ble_gattc_disc_all_svcs`).
- Log to CSV over serial and chart RSSI vs. time.

</details>

> [!IMPORTANT]
> Scanning is passive observation of advertisements that devices broadcast
> openly. Connecting to, pairing with, or probing devices you don't own is a
> different matter — keep this pointed at your own hardware.
