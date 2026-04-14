# ESP32-S3 Bluetooth Jammer (Dual NRF24L01 + WiFi TX)

**Educational and research use only.** Jamming may be illegal in your jurisdiction. See [Disclaimer](docs/README.md#disclaimer) in the full docs.

**Full documentation:** [docs/README.md](docs/README.md) — hardware pinouts, wiring, build, usage, power, and troubleshooting.

![Full system wiring](docs/images/wiring-full.png)

---

## Prerequisites

- **ESP-IDF 5.x** (sourced in your shell)
- **ESP32-S3** board (e.g. Lonely Binary 2520V5 N16R8)
- **2x NRF24L01+PA+LNA** modules
- **3.3 V supply** (~200 mA capable for both radios)

---

## Clone

This project uses the [RF24](https://github.com/nRF24/RF24) driver as an **ESP32-adapted fork** (included as a submodule). Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/AntonBronnfjell/project-esp32-embedsyst-cpp_ble-jammer-nrf24l01
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

RF24 driver: [ESP32-adapted fork](https://github.com/AntonBronnfjell/project-esp32-embedsyst-cpp_rf24.component).

---

## Quick start

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with your serial port (e.g. `/dev/cu.usbmodem101` or `COM3`).

---

## Controls

- **Short press** (button on GPIO 35): toggle jamming on/off. LED reflects state (rainbow = active, off = idle).
- **Long press** (~800 ms): cycle to the next jamming mode.

---

## Jamming modes

The jammer boots in **BARRAGE** mode and cycles through the following modes on each long press:

| # | Mode | Radios | Best for | How it works |
|---|------|--------|----------|-------------|
| 0 | **BARRAGE** | R1 + R2 random hop | BLE devices (mice, keyboards) | Both radios independently hop to random channels (0-79) with 130 us PLL dwell. 100% RF duty cycle via continuous carrier wave. ~6600 hops/s per radio. |
| 1 | **ADV+BARRAGE** | R1 adv channels, R2 random | Audio headphones/speakers (hybrid) | Radio 1 rapid-cycles the 3 BLE advertising channels (2, 26, 80) attacking the control plane. Radio 2 does random barrage across all channels attacking the data plane. Combines both attack vectors. |
| 2 | **BT CLASSIC** | R1 + R2 sweep | BT Classic data connections | Sequential sweep across all 79 BT Classic channels (nRF24 ch 2-80). Radios sweep in opposite directions for maximum coverage. |
| 3 | **BLE ALL** | R1 + R2 cycle | All BLE connections | Cycles through all 40 BLE data + advertising channels (even nRF24 channels 2-80). Radios offset by 20 channels. |
| 4 | **BLE ADV** | R1 + R2 rapid cycle | BLE control disruption | Both radios rapid-cycle the 3 BLE advertising channels (2402/2426/2480 MHz). Each radio hits a different channel per iteration. Most effective at disrupting BLE device discovery and control connections. |
| 5 | **CONSTANT CARRIER** | R1 + R2 on ch 45 | Baseline testing | Both radios output continuous carrier on channel 45 (2447 MHz). Used to verify RF output and as a single-channel reference test. |

All modes except CONSTANT CARRIER use **continuous wave (CW) with 130 us PLL dwell** — the carrier never stops transmitting, it just retunes frequency each hop. WiFi raw TX runs independently on all modes, adding 20 MHz wideband interference on WiFi channels 1, 6, and 11.

---

## Architecture

Three independent transmitters operate simultaneously:

| Transmitter | Bandwidth | Antenna | Coverage |
|-------------|-----------|---------|----------|
| NRF24 Radio 1 (PA+LNA) | ~2 MHz CW per channel | External SMA | Any of 80 channels (2400-2480 MHz) |
| NRF24 Radio 2 (PA+LNA) | ~2 MHz CW per channel | External SMA | Any of 80 channels (2400-2480 MHz) |
| ESP32 WiFi TX (built-in) | 20 MHz wideband | PCB antenna | WiFi ch 1/6/11 (~20 BT channels each) |

### Why CW mode

The NRF24L01+ `startConstCarrier()` function outputs a continuous carrier wave with CE held HIGH. When `setChannel()` is called, the PLL retunes to the new frequency — the carrier never stops. This gives **100% RF duty cycle** (always transmitting). The 130 us delay after each `setChannel()` ensures the PLL locks on the correct channel before hopping again.

### How WiFi TX helps

Each WiFi channel is 20 MHz wide — covering approximately 20 Bluetooth channels simultaneously. The ESP32 sends raw beacon frames that create wideband energy across the target band. WiFi channels 1, 6, and 11 together cover most of the 2.4 GHz Bluetooth spectrum.

### BLE vs BT Classic

- **BLE devices** (mice, keyboards, low-energy sensors): 40 channels, slower hopping, simpler protocol. The BARRAGE mode is highly effective.
- **BT Classic devices** (A2DP headphones, speakers): 79 channels, 1600 hops/second, Adaptive Frequency Hopping (AFH) actively avoids jammed channels. With only 2 NRF24 radios covering 2 channels at a time, AFH can route around the interference. The ADV+BARRAGE and BLE ADV modes attack the BLE control connection instead, which can cause disconnects.

---

## Full documentation

See [docs/README.md](docs/README.md) for hardware pinouts, wiring diagrams, build instructions, power supply recommendations, and troubleshooting.
