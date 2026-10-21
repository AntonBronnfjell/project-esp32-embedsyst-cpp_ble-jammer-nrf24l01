<div align="center">

# 📡 2.4 GHz Interference / Jamming Detector

**Receive-only** spectrum-occupancy sensor for ESP32-S3 + nRF24L01+

<!-- badges: swap the values for your real CI/release once you have them -->
![platform](https://img.shields.io/badge/platform-ESP32--S3-E7352C?logo=espressif&logoColor=white)
![framework](https://img.shields.io/badge/framework-ESP--IDF-blue)
![radio](https://img.shields.io/badge/radio-nRF24L01%2B-00843D)
![transmits](https://img.shields.io/badge/transmits-never-success)
![license](https://img.shields.io/badge/license-educational-lightgrey)

</div>

> [!NOTE]
> This tool **never transmits**. It only *listens*. It is the defensive
> counterpart to an RF jammer: instead of producing interference, it detects
> and localizes it. Building and running it is legal in normal use.

---

## What it does

It hops the nRF24L01+ receiver across all 126 channels (2.400–2.525 GHz) and
reads the chip's **Received Power Detector (RPD)** latch many times per channel.
That yields a per-channel *occupancy* estimate, printed as a live ASCII
spectrogram over the serial monitor.

Ordinary 2.4 GHz traffic is **sparse and bursty**. A jammer (or a microwave
oven, or a stuck transmitter) lights up a **wide, contiguous, continuous**
block of channels — and that contrast is what gets flagged.

<div align="center">

```
[sweep 42] 2.400 GHz _..:=*#%@@@%#*=:._..___...___..._.___ ... 2.525 GHz
[watch] wideband signature (1/3 sweeps): ch 10-34, peak ch22 @ 96%
[ALERT] sustained wideband interference: ch 10-34 saturated for 3 sweeps (19/126 hot, peak ch22 @ 96%, ~2422 MHz)
```

</div>

## How detection works

<table>
<tr><th>Signal</th><th>Occupancy pattern</th><th>Flagged?</th></tr>
<tr><td>BLE adverts</td><td>brief spikes on 3 channels</td><td>no</td></tr>
<tr><td>Wi-Fi AP</td><td>busy but bounded ~20 MHz block</td><td>usually no</td></tr>
<tr><td>Wideband jammer</td><td>most of the band hot, continuously</td><td><b>yes</b></td></tr>
<tr><td>CW / stuck carrier</td><td>one channel pinned at ~100%</td><td>reported as peak</td></tr>
</table>

The heuristic is deliberately simple and honest: if **≥18 of any 25 adjacent
channels** read at or above the hot threshold, that is the wideband signature.
To avoid false alarms, it's **debounced** — the signature must persist for
`WIDEBAND_CONFIRM_SWEEPS` sweeps in a row before escalating from `[watch]` to
`[ALERT]`. A transient burst clears before then. Tune the constants at the top
of [`main/detector_main.cpp`](main/detector_main.cpp):

| Constant | Default | Meaning |
| --- | ---: | --- |
| `SAMPLES_PER_CH` | `128` | RPD samples averaged per channel |
| `HOT_THRESHOLD_PCT` | `50` | occupancy % at which a channel is "hot" |
| `WIDEBAND_WINDOW` | `25` | sliding window width (channels) |
| `WIDEBAND_HOT_IN_WIN` | `18` | hot channels in a window for the signature |
| `WIDEBAND_CONFIRM_SWEEPS` | `3` | consecutive sweeps before `[watch]` → `[ALERT]` |

**Severity states:** `[ok]` normal/bursty traffic · `[watch]` signature seen
but not yet confirmed · `[ALERT]` sustained wideband interference.

## Wiring

Reuses **nRF24 module #1** from the companion firmware (SPI2 / HSPI):

| nRF24 pin | ESP32-S3 GPIO |
| --- | ---: |
| CE  | 16 |
| CSN | 15 |
| SCK | 14 |
| MOSI | 13 |
| MISO | 12 |
| VCC | 3V3 (add a 10 µF cap across VCC/GND) |
| GND | GND |

## Build & flash

```bash
cd tools/rf-interference-detector
idf.py set-target esp32s3
idf.py build flash monitor
```

<details>
<summary><b>Troubleshooting: "nRF24 not responding on SPI2"</b></summary>

<br>

- Confirm the module is on **3.3 V**, never 5 V.
- Add a **10 µF** (plus a 100 nF) decoupling cap directly across the module's
  VCC/GND — brown-outs on TX-capable PA+LNA boards are the #1 cause of flaky SPI.
- Double-check `CE`/`CSN` against the table above.
- Lower `NRF24_SPI_HZ` to `1000000` if you're on long breadboard jumpers.

</details>

> [!TIP]
> Point it at *your own* devices. Wave a known transmitter near it and watch the
> corresponding channels climb — that's the fastest way to learn to read the
> spectrogram before you try to spot an unknown source.
