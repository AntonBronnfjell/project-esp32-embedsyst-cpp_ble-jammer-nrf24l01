# ESP32 BLE Jammer – Documentation

## Disclaimer

**Legal warning:** This project is for **educational and research purposes only**. Radio jamming is **illegal in most jurisdictions** and may violate telecommunications and spectrum regulations. Use only on **your own equipment** in controlled, lawful environments (e.g. anechoic chamber, lab). The authors assume **no liability** for misuse or legal consequences. You are responsible for complying with local laws.

---

## Overview

This project implements a multi-path 2.4 GHz jammer using three simultaneous transmitters:

- **Two NRF24L01+PA+LNA modules** (SPI to ESP32) — continuous wave (CW) carrier with a 150 us PLL dwell per hop (configurable via `JAMMER_NRF24_PLL_SETTLE_US`), providing 100% RF duty cycle across the 2400-2480 MHz band.
- **ESP32-S3 WiFi raw TX** — 20 MHz wideband beacon frames on WiFi channels 1, 6, and 11, each covering approximately 20 Bluetooth channels simultaneously.

**Target board:** Lonely Binary ESP32-S3 2520V5 N16R8. The firmware drives two NRF24 radios over HSPI and VSPI and uses the built-in WiFi radio for wideband TX; the on-board WS2812 LED on GPIO 48 indicates status (rainbow = jamming active, off = idle).

---

## Requirements

- **ESP-IDF 5.x** (build and flash toolchain).
- **ESP32-S3** (e.g. Lonely Binary 2520V5 N16R8).
- **2× NRF24L01+PA+LNA** modules.
- **1x HW-131 breadboard power supply** (5 V source) + **2x AMS1117-3.3V regulators** (one per NRF24 module).
- **2x 100 µF electrolytic capacitors** (one per NRF24 module, at VCC/GND). See [Capacitor placement](capacitor-placement.md).

---

## Hardware – ESP32 pinout

Used GPIOs and functions:

| GPIO | Function        | Notes        |
|------|-----------------|--------------|
| 12   | MISO (RF1)      | HSPI         |
| 13   | MOSI (RF1)      | HSPI         |
| 14   | SCK (RF1)       | HSPI         |
| 15   | CSN (RF1)       | Chip select  |
| 16   | CE (RF1)        | RF1 module   |
| 17   | CE (RF2)        | RF2 module   |
| 18   | CSN (RF2)       | VSPI         |
| 21   | SCK (RF2)       | VSPI         |
| 39   | MISO (RF2)      | VSPI         |
| 47   | MOSI (RF2)      | VSPI         |
| 35   | Button          | Active low to GND |
| 48   | LED (WS2812)    | On-board RGB |

![ESP32-S3 used pins](images/esp32s3-pinout.png)

---

## Hardware – NRF24L01 pinout

| Pin  | Name | Description     |
|------|------|-----------------|
| 1    | GND  | Ground          |
| 2    | VCC  | 3.3 V supply    |
| 3    | CE   | Chip enable     |
| 4    | CSN  | SPI chip select |
| 5    | SCK  | SPI clock       |
| 6    | MOSI | SPI data in     |
| 7    | MISO | SPI data out    |
| 8    | IRQ  | Optional        |

![NRF24L01 module pinout](images/nrf24l01-pinout.png)

---

## Hardware – Full wiring

### Power supply (1x HW-131 + 2x AMS1117-3.3)

Each NRF24L01+PA+LNA module draws up to ~150 mA at max TX power. The ESP32's on-board 3.3 V regulator cannot reliably supply both modules. Use one **HW-131 breadboard power supply** as the 5 V source, feeding **two separate AMS1117-3.3V regulators** — one dedicated to each RF module:

- **HW-131** provides 5 V (via barrel jack or USB).
- **AMS1117-3.3V #1** takes 5 V from HW-131 → outputs 3.3 V to **RF1 Pin 2 (VCC) only**.
- **AMS1117-3.3V #2** takes 5 V from HW-131 → outputs 3.3 V to **RF2 Pin 2 (VCC) only**.
- **Common GND** between HW-131, both AMS1117s, ESP32, RF1, and RF2 (all grounds must be connected together).
- **100 µF electrolytic capacitor** across VCC/GND at each RF module, as close to the module pins as possible.

Using separate regulators per module eliminates cross-module voltage sag during simultaneous TX bursts.

### SPI and control wiring

- **RF1 (HSPI):** GPIO 12→MISO, GPIO 13→MOSI, GPIO 14→SCK, GPIO 15→CSN, GPIO 16→CE.
- **RF2 (VSPI):** GPIO 39→MISO, GPIO 47→MOSI, GPIO 21→SCK, GPIO 18→CSN, GPIO 17→CE.
- **Button:** one side to **GPIO 35**, other side to **GND** (internal pull-up enabled in firmware).
- **LED:** on-board WS2812 on **GPIO 48** (no external wiring needed).

![Full system wiring](images/wiring-full.png)

---

## Pin mapping table

| Function     | Module 1 (HSPI) | Module 2 (VSPI) |
|-------------|-----------------|-----------------|
| MISO        | 12              | 39              |
| MOSI        | 13              | 47              |
| SCK         | 14              | 21              |
| CSN         | 15              | 18              |
| CE          | 16              | 17              |
| **Button**  | —               | **35** (to GND) |
| **LED**     | —               | **48** (on-board) |

---

## Build and flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash
idf.py monitor
```

Replace `PORT` with your serial port (e.g. `/dev/tty.usbserial-*` or `COM3`).

> [!NOTE]
> The build is verified on every push by the [CI workflow](../.github/workflows/build.yml),
> which compiles the jammer **and** both receive-only tools for `esp32s3`.

---

## Build-time tunables

Every tunable lives at the top of [`main/jammer_main.cpp`](../main/jammer_main.cpp) and can
be overridden from the build (`idf.py build -DJAMMER_NRF24_PLL_SETTLE_US=200`) or by editing
the `#ifndef` defaults.

| Macro | Default | Meaning |
|---|---:|---|
| `JAMMER_NRF24_PLL_SETTLE_US` | `150` | us to wait after `setChannel()` so the PLL locks. Lower -> faster hops; higher -> cleaner carrier. |
| `JAMMER_NRF24_PA_LEVEL` | `RF24_PA_MAX` | Output level before the board's PA+LNA. Try `RF24_PA_HIGH` on clone boards whose PA compresses. |
| `JAMMER_NRF24_PALEVEL_LNA_BIT` | `1` | Si24R1 "extra power" LSB - leave at 1 for typical AliExpress PA+LNA boards. |
| `JAMMER_NRF24_SPI_HZ` | `10000000` | SPI clock. Drop to `1000000` if the SPI probe reads garbage on long breadboard wires. |
| `JAMMER_WIFI_FRAMES_PER_CH` | `50` | Raw beacon frames sent on a WiFi channel before rotating to the next one. |

---

## Usage

- **Short press** (button on GPIO 35): toggle jamming on/off.
- **Long press** (~800 ms): cycle to the next jamming mode.
- Jammer starts in BARRAGE mode on boot with jamming active.
- At startup, a 3-second BLE discovery scan logs nearby devices (MAC, name, RSSI) to serial.
- When jamming stops, the log prints a session summary with the run time and the number
  of channel hops performed by each radio - useful when correlating a lab session with a
  spectrum-analyser capture.

### LED indicator

Each mode has a distinct LED color for at-a-glance identification:

| Mode | LED Color | Animation |
|------|-----------|-----------|
| BARRAGE | Rainbow | Slow hue cycle |
| STORM | Purple | Breathing (fade in/out) |
| ADV+BARRAGE | Cyan | Breathing |
| TRACKING | White | Breathing |
| BT CLASSIC | Blue | Breathing |
| BLE ALL | Green | Breathing |
| BLE ADV | Yellow | Breathing |
| BLE SIEGE | Orange | Breathing |
| CONSTANT CARRIER | Red | Breathing |
| Jamming OFF | Off | — |

---

## Jamming modes — detailed reference

The jammer cycles through nine modes on each long press. All hopping modes use continuous wave (CW) transmission from the NRF24 modules (100% RF duty cycle — the carrier never stops) combined with ESP32 WiFi raw TX (20 MHz wideband on channels 1/6/11).

### Mode 0: BARRAGE

**Best for:** BLE devices (mice, keyboards, trackpads, low-energy sensors).

Both NRF24 radios independently hop to random channels (0-79) with a 150 us PLL dwell after each hop. The randomness makes it unpredictable for Adaptive Frequency Hopping (AFH) algorithms. Each radio achieves approximately 6,600 hops per second (1 / 150 us), for a combined ~13,200 hops/s across the 80-channel band.

**How it works technically:** `startConstCarrier()` puts each radio into continuous wave mode with CE held HIGH. On each iteration, `setChannel()` retunes the PLL to a new random frequency, followed by a 150 us delay for the PLL to lock. The carrier output never stops — it transitions smoothly between frequencies.

**Why it works against BLE:** BLE uses only 40 channels with relatively slow connection events (1.25 ms minimum interval). Two radios randomly hopping across the band create frequent collisions with BLE packets. BLE devices like the Apple Magic Mouse are highly susceptible because their connection parameters are optimized for low power, not resilience.

---

### Mode 1: STORM (dual-strategy interference)

**Best for:** Enhanced barrage with scientifically optimized interference diversity.

STORM splits the two radios into complementary roles based on IEEE research showing that interference diversity outperforms single-strategy attacks (JamRF, VTT Finland studies on WLAN jamming).

**Radio 1 "NOISE"** — hops with NO PLL dwell (~50,000 hops/s). The PLL never locks between channel changes, causing the carrier to continuously sweep through intermediate frequencies. This creates broadband noise wider than the stable 2 kHz CW tone, filling more of the BLE receiver's 2 MHz channel bandwidth.

**Radio 2 "STRIKE"** — hops with 150 us PLL dwell (~6,600 hops/s). The PLL locks on each target channel, producing a stable CW tone at the exact center frequency. This precision interference corrupts the GFSK demodulator's bit decision threshold.

**Why this works:** The nRF24L01+ CW mode produces a tone only ~2 kHz wide (measured +-1 kHz at -20dB). BLE channels are 2 MHz wide — a single CW tone covers just 0.1% of the channel bandwidth. Radio 1's PLL sweep noise covers significantly more bandwidth, while Radio 2's precision CW targets the critical center frequency. The receiver cannot optimize its filtering for both interference types simultaneously.

| Metric | BARRAGE | STORM |
|--------|---------|-------|
| R1 hop rate | ~6,600/s | ~50,000/s |
| R1 interference bandwidth | ~2 kHz (CW) | broadband (PLL sweep) |
| R2 interference bandwidth | ~2 kHz (CW) | ~2 kHz (CW) |
| Interference diversity | none | two complementary types |

---

### Mode 2: ADV+BARRAGE (hybrid)

**Best for:** BT Classic audio devices (headphones, speakers) connected via iPhone or Android.

This is a dual-attack mode. Radio 1 rapid-cycles the three BLE advertising channels (nRF24 ch 2 = 2402 MHz, ch 26 = 2426 MHz, ch 80 = 2480 MHz), attacking the BLE control plane. Radio 2 simultaneously does random barrage across all 80 channels, creating data-plane interference.

**How it works technically:** Modern Bluetooth audio devices (like Sony LinkBuds, AirPods, etc.) maintain both a BT Classic A2DP connection for audio streaming AND a BLE connection for control signaling (battery status, firmware updates, app features). By disrupting the BLE control connection, the device may disconnect and attempt to reconnect, causing audio interruptions even though the A2DP data stream uses BT Classic's 79-channel AFH.

**Why this mode exists:** With only 2 NRF24 radios covering 2 channels at a time out of 79, BT Classic AFH can trivially route around the interference on the data plane. Attacking the BLE control plane is a more effective strategy with limited hardware.

---

### Mode 3: TRACKING

**Best for:** Aggressive dual-vector attacks on BLE — control plane *and* data plane at once.

Both radios alternate between the BLE advertising channels and random barrage in a **1 : 1 ratio**. On every hop one radio sits on an advertising channel while the other sweeps randomly; the roles swap on the next hop.

**How it works technically:** `startConstCarrier()` keeps both radios transmitting while `setChannel()` retunes them between a BLE advertising channel (2402 / 2426 / 2480 MHz) and a random channel across the full 80-channel band. The pattern repeats as:

```
R1 = adv,  R2 = random   ->   R1 = random, R2 = adv   ->   repeat
```

**Why it exists:** ADV+BARRAGE dedicates only radio 1 to the advertising channels, so half of the advertising-channel pressure is unused. TRACKING puts **both** radios on advertising channels on alternating hops, which roughly **doubles the advertising-plane pressure** while still keeping data-plane coverage on the other half of the hops. This is the mode to reach for when a device survives the data plane but keeps reconnecting through the control plane.

**Tradeoff:** each advertising channel is only hit on every other hop, so the *per-channel* duty cycle is lower than a dedicated advertising sweep — but the total number of BLE-channel collisions per second is higher, and the pattern is harder for a device to learn around.

---

### Mode 4: BT CLASSIC

**Best for:** Testing BT Classic vulnerability, sequential spectrum coverage analysis.

Both radios perform a sequential sweep across all 79 BT Classic FHSS data channels (nRF24 ch 2-80). Radio 1 sweeps forward (2 -> 80 -> 2) while Radio 2 sweeps backward (41 -> 2 -> 80), bouncing at the edges. This ensures every channel gets regular interference from both directions.

**How it works technically:** BT Classic uses Adaptive Frequency Hopping (AFH) across 79 channels at 1,600 hops per second (625 us per slot). The sequential sweep hits every channel in order. With 150 us PLL dwell per hop, each radio covers all 79 channels in approximately 12 ms. AFH scans once per second and can detect and blacklist interfered channels, but requires a minimum of ~20 usable channels to maintain a connection.

**Limitation:** AFH adapts within 1-2 seconds by blacklisting jammed channels and routing traffic to clean ones. With 2 radios on 2 channels at any instant, 77 channels remain clean — more than enough for AFH to maintain the connection.

---

### Mode 5: BLE ALL

**Best for:** Comprehensive BLE disruption, testing BLE data channel resilience.

Both radios cycle through all 40 BLE data and advertising channels (even nRF24 channels 2-80). Radio 1 and Radio 2 are offset by 20 channels, so they cover different parts of the BLE spectrum simultaneously.

**How it works technically:** BLE uses 40 channels: 37 data channels + 3 advertising channels. The data channels use even-numbered nRF24 channels (2, 4, 6, ... 78, 80) because BLE channels are 2 MHz wide with 2 MHz spacing. Both radios cycle through this list sequentially, offset by half the array length to avoid redundant coverage.

---

### Mode 6: BLE ADV

**Best for:** BLE device discovery disruption, preventing new BLE connections, control channel attacks.

Both radios rapid-cycle across the three BLE advertising channels: channel 37 (2402 MHz / nRF24 ch 2), channel 38 (2426 MHz / nRF24 ch 26), and channel 39 (2480 MHz / nRF24 ch 80). Each radio hits a different advertising channel per iteration, cycling fast.

**How it works technically:** All BLE device discovery, connection initiation, and broadcast traffic occurs on these three advertising channels. By continuously jamming all three, no new BLE connections can be established, existing BLE control connections are disrupted, and BLE beacons/advertisements from other devices are blocked.

**Why it is effective against audio devices:** Many Bluetooth audio devices maintain a BLE GATT connection alongside the BT Classic audio stream. Disrupting the BLE advertising channels can cause the BLE control connection to fail, triggering reconnection attempts that interrupt audio playback.

---

### Mode 7: BLE SIEGE

**Best for:** Attacking established BLE GATT control connections on data channels.

After pairing, BLE connections use data channels 0-36 — NOT advertising channels. BLE ADV mode only hits 3 advertising channels, missing the active GATT connection entirely. BLE SIEGE targets ALL 40 BLE-specific channels (37 data + 3 advertising), mapped to exact nRF24 frequencies (even channels 2-80).

**How it works technically:** Both radios randomly hop across the 40 BLE channel frequencies. This is 2x more concentrated on BLE than barrage (which wastes hops on odd nRF24 channels that don't correspond to BLE frequencies). Hit rate: 2 radios / 40 channels = 5% per hop vs barrage's 2 radios / 80 channels = 2.5%.

**Why this matters for audio devices:** Many BT Classic audio devices (Sony, Apple, etc.) maintain a BLE GATT connection for control signaling (battery, codec negotiation, noise cancellation settings). If the GATT connection drops (supervision timeout ~720ms on iPhone), the device may attempt to reconnect, causing audio interruption.

---

### Mode 8: CONSTANT CARRIER

**Best for:** Baseline RF output testing, antenna verification, single-channel interference analysis.

Both radios output a continuous carrier wave on channel 45 (2447 MHz). No hopping — the carrier stays on one frequency indefinitely.

**How it works technically:** This mode is primarily used for hardware verification. If a spectrum analyzer or SDR shows a strong signal at 2447 MHz, the NRF24 modules and antennas are functioning correctly. The self-test at boot also uses this mechanism (on channel 50) to verify each radio can transmit and the other can detect it via RPD (Received Power Detector, threshold -64 dBm).

---

## WiFi TX (3rd transmitter)

The ESP32's built-in WiFi radio operates as an independent wideband transmitter on all modes. It sends raw 802.11 beacon frames using `esp_wifi_80211_tx()` on WiFi channels 1, 6, and 11 in rotation (50 frames per channel before switching).

Each WiFi channel is 20 MHz wide:
- **WiFi ch 1** (2412 MHz): covers BT channels ~2-22
- **WiFi ch 6** (2437 MHz): covers BT channels ~27-47
- **WiFi ch 11** (2462 MHz): covers BT channels ~52-72

Together, these three channels cover approximately 60 of the 79 BT Classic channels with 20 MHz wideband energy — far wider than the NRF24's ~2 MHz per channel. The WiFi TX runs independently on CPU core 0, while the NRF24 jam task runs on CPU core 1.

---

## Power

- **1x HW-131 + 2x AMS1117-3.3** is the recommended power supply. The HW-131 provides 5 V, and each AMS1117-3.3V regulator independently powers one NRF24 module with clean 3.3 V. Having separate regulators ensures one module's TX current spikes don't affect the other.
- **Do NOT power both NRF24 modules from the ESP32's on-board 3.3 V regulator.** The ESP32's regulator is typically rated for ~500 mA total (ESP32 + peripherals), and two PA+LNA modules at max TX can cause voltage sag that resets the ESP32 or causes SPI errors.
- **100 µF electrolytic capacitor** per module at VCC/GND, as close as possible to the module pins. These smooth out the current spikes during TX bursts.
- **Common ground** is critical: HW-131 GND, ESP32 GND, RF1 GND, and RF2 GND must all be connected together. A floating ground causes SPI failures and erratic behavior.
- **Short, thick wires** from the HW-131 3.3 V output to each module VCC to minimize voltage drop.

---

## Troubleshooting

- **`begin FAILED` / “No NRF24 modules” in the log:** Re-check wiring against the pin table and diagram above: **CE and CSN are different pins** (easy to swap), **MOSI and MISO are often swapped** (ESP MOSI → module MOSI, ESP MISO → module MISO), **common GND** between ESP32 and radios, and **3.3 V only** on module VCC (never 5 V). Continuity beeps help, but wrong pin order still fails SPI.
- **SPI probe lines (after a failed `begin()`):** The firmware prints raw reads of **SETUP_AW** and **CONFIG**. On a **powered, working** nRF24 after reset, **SETUP_AW** is often **0x03** and **CONFIG** often **0x08** (exact values can vary slightly). **0xFF** or **0x00** on both usually means **no real SPI response** (wrong SCK/MOSI/MISO/CSN, floating MISO, missing GND, or module not on 3.3 V). If the probe looks good but `begin()` still fails, try lowering **`JAMMER_NRF24_SPI_HZ`** in `main/jammer_main.cpp` (e.g. to `1000000`) and reflash.
- **Confirm the module itself:** The only definitive check without this project is a **second setup** (e.g. Arduino + RF24 “GettingStarted” / scanner sketch) with **known-good** 3.3 V wiring. If it fails there too, suspect a bad module or power.
- **No jamming:** Check 3.3 V at each module (≥ ~3.0 V), antennas connected, and serial output for “Module 1/2 OK”.
- **Only one module detected:** The firmware keeps running with the module that *did* answer and logs `RF1 missing — running with radio 2 only` (or the RF2 equivalent). The missing module is then left untouched instead of being written to over SPI on every hop.
- **Voltage sag:** Add a dedicated regulator or second supply, 100 µF caps per module, and lower resistance (thick/short wires, parallel jumpers).

---

## References

- [Capacitor placement for NRF24 modules](capacitor-placement.md)
- [RF24 fork and ESP32 adaptation](RF24-fork-and-esp32-adaptation.md) — how this project uses an ESP32-adapted RF24 (fork + submodule)
- [RF24 library](https://github.com/nRF24/RF24) (original); this project uses [AntonBronnfjell/project-esp32-embedsyst-cpp_rf24.component](https://github.com/AntonBronnfjell/project-esp32-embedsyst-cpp_rf24.component) (ESP32-adapted fork) as submodule
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/) (ESP32-S3)
