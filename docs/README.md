# ESP32 BLE Jammer – Documentation

## Disclaimer

**Legal warning:** This project is for **educational and research purposes only**. Radio jamming is **illegal in most jurisdictions** and may violate telecommunications and spectrum regulations. Use only on **your own equipment** in controlled, lawful environments (e.g. anechoic chamber, lab). The authors assume **no liability** for misuse or legal consequences. You are responsible for complying with local laws.

---

## Overview

This project implements a multi-path 2.4 GHz jammer using:

- **Two NRF24L01+PA+LNA modules** (SPI to ESP32) for two independent transmit paths.
- **ESP32-S3 built-in BLE** as a third transmit path.

**Target board:** Lonely Binary ESP32-S3 2520V5 N16R8. The firmware drives two NRF24 radios over HSPI and VSPI and uses the on-chip BLE radio; the on-board WS2812 LED on GPIO 48 indicates status.

---

## Requirements

- **ESP-IDF 5.x** (build and flash toolchain).
- **ESP32-S3** (e.g. Lonely Binary 2520V5 N16R8).
- **2× NRF24L01+PA+LNA** modules.
- **3.3 V supply** capable of ~200 mA for both NRF24 modules (or separate supplies). Optional: **2× 100 µF** electrolytic capacitors for each module (see [Capacitor placement](capacitor-placement.md)).

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

- **ESP32 3.3 V / GND** to both NRF24 modules (and optional 100 µF cap per module at VCC/GND).
- **RF1 (HSPI):** 12→MISO, 13→MOSI, 14→SCK, 15→CSN, 16→CE.
- **RF2 (VSPI):** 39→MISO, 47→MOSI, 21→SCK, 18→CSN, 17→CE.
- **Button:** one side to **GPIO 35**, other side to **GND**.
- **LED:** on-board WS2812 on **GPIO 48** (no external wiring).

Optional: 100 µF capacitors at each module; external 3.3 V regulator (e.g. AMS1117-3.3) from 5 V for both modules.

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

Replace `PORT` with your serial port (e.g. `/dev/tty.usbserial-*` or `COM3`). After renaming the main source to `jammer_main.cpp`, the build steps are unchanged.

---

## Usage

- **Short press** (button): toggle jamming on/off (LED reflects state).
- **Long press** (~800 ms): cycle jamming mode.

**Modes:**

- Bluetooth Classic sweep
- BLE (channels 2, 26, 80)
- Dual sweep
- Constant carrier

**LED:** Rainbow pattern when jamming (slower in constant carrier, faster in sweep modes); off when not jamming.

---

## Power

- **3.3 V for NRF24s:** A dedicated 3.3 V regulator (e.g. AMS1117-3.3) from 5 V for both modules is recommended; or one module from ESP32 3.3 V and one from the regulator.
- **100 µF cap** per module at VCC/GND, as close as possible to the module.
- **Low resistance** from supply to module VCC (e.g. &lt; 1 Ω): use short, thick wires or parallel jumpers to avoid voltage sag during TX.

---

## Troubleshooting

- **No jamming:** Check 3.3 V at each module (≥ ~3.0 V), antennas connected, and serial output for “Module 1/2 OK”.
- **Voltage sag:** Add a dedicated regulator or second supply, 100 µF caps per module, and lower resistance (thick/short wires, parallel jumpers).

---

## References

- [Capacitor placement for NRF24 modules](capacitor-placement.md)
- [RF24 fork and ESP32 adaptation](RF24-fork-and-esp32-adaptation.md) — how this project uses an ESP32-adapted RF24 (fork + submodule)
- [RF24 library](https://github.com/nRF24/RF24) (original); this project uses [AntonBronnfjell/project-esp32-embedsyst-cpp_rf24](https://github.com/AntonBronnfjell/project-esp32-embedsyst-cpp_rf24) (ESP32-adapted fork) as submodule
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/) (ESP32-S3)
