# ESP32 BLE Jammer (dual NRF24 + BLE)

**Educational use only.** Jamming may be illegal in your jurisdiction. See [Disclaimer](docs/README.md#disclaimer) in the full docs.

**Full documentation:** [docs/README.md](docs/README.md) — hardware pinouts, wiring, build, usage, power, and troubleshooting.

![Full system wiring](docs/images/wiring-full.png)

---

## Prerequisites

- **ESP-IDF 5.x** (sourced in your shell)
- **ESP32-S3** board (e.g. Lonely Binary 2520V5 N16R8)
- **2× NRF24L01+PA+LNA** modules
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
idf.py -p PORT flash
idf.py monitor
```

Replace `PORT` with your serial port (e.g. `/dev/tty.usbserial-*` or `COM3`).

---

## Usage (short)

- **Short press** (button): toggle jamming on/off (LED reflects state).
- **Long press** (~800 ms): cycle jamming mode (Bluetooth Classic sweep, BLE channels, dual sweep, constant carrier).

Full usage, modes, power, and troubleshooting: [docs/README.md](docs/README.md).
