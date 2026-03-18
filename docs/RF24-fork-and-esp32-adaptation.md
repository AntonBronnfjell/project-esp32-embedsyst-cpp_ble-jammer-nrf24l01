# RF24: fork and ESP32 adaptation

This project uses an **ESP32-adapted** version of the [nRF24/RF24](https://github.com/nRF24/RF24) library. You can either use the in-tree copy under `components/RF24` or follow this guide to create your own fork and point the jammer at it via a submodule.

---

## 1. Fork the original repo

1. Go to [https://github.com/nRF24/RF24](https://github.com/nRF24/RF24) and click **Fork**. Create the fork under your GitHub account.
2. Optionally rename the fork to **project-esp32-embedsyst-cpp_rf24.component**: **Settings → General → Repository name**. The clone URL will be `https://github.com/AntonBronnfjell/project-esp32-embedsyst-cpp_rf24.component`.

---

## 2. Clone your fork and apply ESP32 files

Clone your fork:

```bash
git clone https://github.com/AntonBronnfjell/project-esp32-embedsyst-cpp_rf24.component
cd project-esp32-embedsyst-cpp_rf24.component
```

From this jammer repo, copy the ESP-IDF–specific files into the clone:

- **Root CMakeLists.txt**  
  Copy from this repo’s `components/RF24/CMakeLists.txt` (the one that calls `idf_component_register` with `utility/esp_idf` sources) over the root `CMakeLists.txt` of the RF24 repo. If the original RF24 has a different root layout, merge so that the ESP-IDF component registration is used when built as an ESP-IDF component.

- **utility/esp_idf/**  
  Copy the entire `utility/esp_idf/` directory from this repo’s `components/RF24/utility/esp_idf/` into the clone’s `utility/esp_idf/`. It contains:
  - `spi.cpp`, `spi.h`
  - `gpio.cpp`, `gpio.h`
  - `compatibility.cpp`, `compatibility.h`
  - `interrupt.cpp`, `interrupt.h`
  - `RF24_arch_config.h`, `includes.h`

Ensure the clone builds as an ESP-IDF component (e.g. use it as `components/RF24` in an ESP-IDF app and run `idf.py build`).

---

## 3. Add README for the ESP32 fork

In the root of your RF24 fork, add or update **README.md** (or a top-level section) with something like:

```markdown
# RF24 – ESP32 (ESP-IDF) adaptation

This repository is the [RF24](https://github.com/nRF24/RF24) library **adapted for ESP32 (ESP-IDF)**. It provides the NRF24L01 driver as an ESP-IDF component.

- **Used by:** [project-esp32-embedsyst-cpp_ble-jammer-nrf24l01](https://github.com/AntonBronnfjell/project-esp32-embedsyst-cpp_ble-jammer-nrf24l01) (included as a submodule).
- **Original:** [nRF24/RF24](https://github.com/nRF24/RF24).

## ESP-IDF integration

- Component name: **RF24** (use `REQUIRES RF24` in your app’s `CMakeLists.txt`).
- ESP-IDF-specific code lives in **utility/esp_idf/** (SPI, GPIO, compatibility, interrupts). The root `CMakeLists.txt` registers the component with `idf_component_register`.
```

Replace `AntonBronnfjell` with your GitHub username. Commit and push to your fork.

---

## 4. Use your fork in the jammer (submodule)

After your fork is pushed:

1. In the **jammer** repo root, remove the current in-tree RF24 (delete contents of `components/RF24` or remove the directory).
2. Add your fork as a submodule (recreates `components/RF24`):

   ```bash
   git submodule add https://github.com/AntonBronnfjell/project-esp32-embedsyst-cpp_rf24.component components/RF24
   ```

3. Commit `.gitmodules` and the `components/RF24` submodule entry.
4. In the root README and [docs/README.md](README.md), set the clone URL and References to your fork (replace `AntonBronnfjell`).

New clones of the jammer should use:

```bash
git clone --recurse-submodules https://github.com/AntonBronnfjell/project-esp32-embedsyst-cpp_ble-jammer-nrf24l01
```
