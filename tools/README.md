<div align="center">

# 🧰 Defensive & Educational Tools

Standalone, **receive-only** firmware that reuses this project's ESP32-S3 +
nRF24L01+ hardware for learning and defense — not interference.

</div>

> [!NOTE]
> Each tool below is its own self-contained ESP-IDF project. Neither emits
> interference: one only listens to the band, the other only observes BLE
> advertisements. Build and run them with the standard `idf.py` flow.

| Tool | Radio | Transmits? | What it's for |
| --- | --- | :---: | --- |
| [**rf-interference-detector**](rf-interference-detector/) | nRF24L01+ | ❌ never | Passive 2.4 GHz occupancy spectrogram + wideband-interference alert |
| [**ble-scanner**](ble-scanner/) | ESP32-S3 BLE | ❌ observer only | Decode BLE advertisements: address, RSSI, name, UUIDs, mfg data |

```bash
# Build either one the same way:
cd tools/<tool-name>
idf.py set-target esp32s3
idf.py build flash monitor
```

The RF24 component is shared from the repo root (`../../components/RF24`); see
each tool's own `README.md` for wiring and tunables, and
[`docs/github-readme-formatting.md`](../docs/github-readme-formatting.md) for the
GitHub Markdown/HTML techniques these pages demonstrate.
