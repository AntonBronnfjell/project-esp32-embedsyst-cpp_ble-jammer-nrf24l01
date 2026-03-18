---
name: Gitignore README GitHub RF24
overview: Add project .gitignore, improve README with usage and clone instructions, publish the jammer repo to GitHub (folder name as repo name), fork RF24 and apply ESP32 adaptation into project-esp32-embedsyst-cpp_rf24, then switch the jammer to use that repo via a git submodule.
todos: []
isProject: false
built: false
---

# .gitignore, README, GitHub upload, and RF24 as separate repo

## 1. Add project .gitignore

Add a **root** [.gitignore](.gitignore) (there is none today; only [components/RF24/.gitignore](components/RF24/.gitignore) and IDE folders exist). Use standard ESP-IDF entries so build artifacts and local config are not committed:

- `build/`
- `sdkconfig`
- `sdkconfig.old`
- `managed_components/`
- `dependencies.lock`
- `.DS_Store`
- IDE/project files (e.g. `.idea/`, `cmake-build-*/`) if desired

This keeps the repo clean when pushing to GitHub.

---

## 2. Improve README for usage

Enhance the root [README.md](README.md) so it doubles as a quick usage entry point:

- **Keep:** Title, link to full docs, and the wiring image.
- **Add:**
  - **Prerequisites:** ESP-IDF 5.x, ESP32-S3, 2× NRF24L01+PA+LNA, 3.3 V supply.
  - **Clone:** Instruct to clone with submodules (required once RF24 is a submodule):  
  `git clone --recurse-submodules <repo-url>`  
  and, if already cloned, `git submodule update --init --recursive`.
  - **Quick start:** One block with `idf.py set-target esp32s3`, `idf.py build`, `idf.py -p PORT flash`, `idf.py monitor`.
  - **Usage (short):** Short press = toggle jamming; long press = cycle mode; link to [docs/README.md](docs/README.md) for full usage, modes, power, troubleshooting.
- Optionally add a one-line disclaimer (e.g. "Educational use only; check local laws") and link to the full disclaimer in docs.

No need to duplicate the full doc; keep the root README scannable and point to [docs/README.md](docs/README.md) for details.

---

## 3. RF24: fork from original, then apply ESP32 adaptation

**Goal:** Produce a **standalone GitHub repo** named **project-esp32-embedsyst-cpp_rf24** that is a fork of the original RF24 with ESP32 (ESP-IDF) changes applied.

**Steps:**

1. **Fork the original repo on GitHub:**
  - Go to [nRF24/RF24](https://github.com/nRF24/RF24) and click **Fork**. Create the fork under your account (GitHub will name it `RF24` in your namespace).
  - Rename the forked repo to **project-esp32-embedsyst-cpp_rf24** in the repo **Settings → General → Repository name**, so the clone URL is `https://github.com/<YOUR_GITHUB_USERNAME>/project-esp32-embedsyst-cpp_rf24`. (If you keep the name `RF24`, use that URL for the submodule; the plan steps below assume the repo name project-esp32-embedsyst-cpp_rf24.)
2. **Clone your fork locally and apply the ESP32 adaptation:**
  - Clone your fork: `git clone https://github.com/<YOUR_GITHUB_USERNAME>/project-esp32-embedsyst-cpp_rf24` (or your fork's URL if not renamed).
  - Copy or apply the **ESP-IDF-specific changes** from the jammer's in-tree [components/RF24](components/RF24) into this clone:
    - Root [components/RF24/CMakeLists.txt](components/RF24/CMakeLists.txt) that uses `idf_component_register` and the `utility/esp_idf` sources.
    - The entire **utility/esp_idf/** directory (spi.cpp, gpio.cpp, compatibility.cpp, interrupt.cpp and headers, RF24_arch_config.h, includes.h).
  - Ensure the fork builds as an ESP-IDF component (e.g. by copying the jammer's components/RF24 over the clone and keeping git history, or by applying the above files and fixing paths if the fork's layout differs).
  - Add or update **README.md** to state that this repo is **RF24 adapted for ESP32 (ESP-IDF)**, is used by **project-esp32-embedsyst-cpp_ble-jammer-nrf24l01** (with link once public), and credits the original [nRF24/RF24](https://github.com/nRF24/RF24).
  - Commit the changes and push to your GitHub fork.

The jammer will later depend on this repo via a submodule, so the fork (with ESP32 changes applied) must exist and be pushed first.

---

## 4. Jammer: use your personal RF24 repo (submodule)

**Goal:** The jammer must use your RF24 repo instead of an in-tree copy.

**Steps:**

1. **Remove the current in-tree RF24:**
  - Delete the contents of [components/RF24](components/RF24) (or remove the directory).  
  - Do **not** delete the **path** `components/RF24` — the submodule will live there so that [main/CMakeLists.txt](main/CMakeLists.txt) `REQUIRES RF24` keeps working (ESP-IDF resolves component name from the folder name under `components/`).
2. **Add your RF24 repo as a submodule:**
  - From the jammer repo root:  
   `git submodule add https://github.com/<YOUR_GITHUB_USERNAME>/project-esp32-embedsyst-cpp_rf24 components/RF24`
  - This creates `components/RF24` as a submodule pointing at your repo. Commit the new `.gitmodules` and the `components/RF24` submodule entry.
3. **Document in README:**
  - In the root [README.md](README.md): state that the **RF24 driver** is the ESP32-adapted fork from this repo: `https://github.com/<YOUR_GITHUB_USERNAME>/project-esp32-embedsyst-cpp_rf24`, and that users must clone with `--recurse-submodules` (or run `git submodule update --init --recursive` after clone).  
  - In [docs/README.md](docs/README.md), update the **References** section: replace or add the link to your RF24 repo and note "ESP32-adapted fork used by this project."

After this, the jammer has no in-tree RF24; it uses only your GitHub repo via the submodule.

---

## 5. Upload jammer repo to GitHub (comprehensive description)

**Repo name:** Use the **folder name** as the repo name: **project-esp32-embedsyst-cpp_ble-jammer-nrf24l01**.

**Steps:**

1. If the project is not yet a git repo: `git init` in the project root, then add and commit all files (with the new .gitignore and README).
2. On GitHub, create a new repository named **project-esp32-embedsyst-cpp_ble-jammer-nrf24l01** (no need to add a README if you already have one locally).
3. Add remote and push:
  `git remote add origin https://github.com/<YOUR_GITHUB_USERNAME>/project-esp32-embedsyst-cpp_ble-jammer-nrf24l01.git`  
   `git push -u origin main` (or `master`, depending on default branch).
4. In GitHub: set the **Description** and **About** (and optional topics) as below.

**Suggested GitHub description (short):**  
"ESP32-S3 BLE + dual NRF24L01 jammer (educational). Three TX paths: 2× NRF24, 1× BLE. ESP-IDF 5.x."

**Suggested "About" / long description (for repo description or README intro):**  

- **What it is:** Multi-path 2.4 GHz jammer firmware for ESP32-S3: two NRF24L01+PA+LNA over SPI plus the built-in BLE radio. Target board: Lonely Binary ESP32-S3 2520V5 N16R8.  
- **Purpose:** Educational and research only; for testing your own equipment. Jamming may be illegal in your jurisdiction.  
- **Features:** Short press = toggle jamming; long press = cycle mode (Bluetooth Classic sweep, BLE channels, dual sweep, constant carrier). On-board WS2812 status LED.  
- **Docs:** Full hardware pinouts, wiring diagrams, build, flash, usage, power, and troubleshooting in the repo (see `docs/README.md`).  
- **Dependencies:** Uses the ESP32-adapted RF24 component from `project-esp32-embedsyst-cpp_rf24` (included as submodule; clone with `--recurse-submodules`).

You can paste the long description into the repo's "About" field (if it supports multiple lines) or leave the short one and keep details in README.

---

## 6. Implementation order (summary)


| Step | Action                                                                                                                                                                                                                                                                                                                                           |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1    | Add root [.gitignore](.gitignore) (ESP-IDF build, sdkconfig, managed_components, etc.).                                                                                                                                                                                                                                                          |
| 2    | Improve root [README.md](README.md): prerequisites, clone with submodules, quick start, short usage, link to docs.                                                                                                                                                                                                                               |
| 3    | **Fork** [nRF24/RF24](https://github.com/nRF24/RF24) on GitHub; optionally rename fork to **project-esp32-embedsyst-cpp_rf24**. Clone the fork, **apply** ESP32 adaptation from jammer's [components/RF24](components/RF24) (CMakeLists.txt, utility/esp_idf/), add README explaining ESP32 adaptation and upstream credit, push to your GitHub. |
| 4    | In jammer: remove [components/RF24](components/RF24) contents, add submodule `components/RF24` → your RF24 repo; update root and [docs/README.md](docs/README.md) (clone instructions, RF24 link).                                                                                                                                               |
| 5    | Init jammer git (if needed), add remote, push to **project-esp32-embedsyst-cpp_ble-jammer-nrf24l01**; set GitHub description and About.                                                                                                                                                                                                          |


**Placeholder:** Replace `<YOUR_GITHUB_USERNAME>` everywhere with your actual GitHub username (for submodule URL, README links, and remote URL).

---

## 7. Optional: RF24 repo description on GitHub

For **project-esp32-embedsyst-cpp_rf24**, suggested short description:  
"RF24 library adapted for ESP32 (ESP-IDF). Used by project-esp32-embedsyst-cpp_ble-jammer-nrf24l01. Based on nRF24/RF24."