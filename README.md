# TRMNL Client for LilyGo T5 4.7" E-Paper

PlatformIO firmware for the **LilyGo T5 4.7" E-Paper V2.0** (ESP32-WROVER) that acts as a [TRMNL](https://usetrmnl.com) BYOD/BYOS client. The device wakes from deep sleep, connects to WiFi, fetches a display image from the TRMNL API, renders it on the 960x540 e-ink panel, then returns to deep sleep.

## Hardware

- **Board:** LilyGo T5 4.7" E-Paper V2.0 (NOT the S3 variant)
- **MCU:** ESP32-WROVER — dual-core Xtensa, 16MB flash, 8MB PSRAM
- **Display:** ED047TC1 — 4.7", 960x540, 16-level grayscale, parallel interface via [epdiy](https://github.com/vroland/epdiy)
- **Power:** USB-C or 18650/LiPo

## Features

- **Captive portal** on first boot — configure WiFi and server URL from your phone
- **TRMNL BYOD** (hosted cloud, $74 licence) or **BYOS** (self-hosted, free)
- **BMP and PNG** image decoding with auto-detection
- **4-bit grayscale** rendering — 16 shades on the e-ink panel
- **Deep sleep** with configurable refresh rate (sent by the server)
- **Button wake** on GPIO39 — long press (3s) for factory reset
- **Battery monitoring** via GPIO14 ADC with voltage divider correction
- **Error handling** on every path — WiFi failure, API errors, image decode issues all show informative messages on screen before sleeping

## Boot Flow

```
Power on → Init display → Read NVS config
  ├── No WiFi config → Captive portal (AP mode, DNS redirect, web UI)
  ├── Long press GPIO39 → Factory reset → Reboot
  └── WiFi config exists → Connect WiFi
        ├── No API key → GET /api/setup → Register device
        └── Has API key → GET /api/display → Download image → Render → Deep sleep
```

## Project Structure

```
src/
├── main.cpp           # Boot flow orchestrator
├── config.h           # Pin definitions, constants
├── config_store.*     # NVS (Preferences) wrapper
├── display.*          # epdiy display wrapper
├── wifi_manager.*     # WiFi connect + captive portal
├── trmnl_api.*        # TRMNL API client (/api/setup, /api/display)
├── image_decode.*     # BMP/PNG download and decode to 4bpp framebuffer
├── battery.*          # ADC voltage reading
└── portal_html.h      # Captive portal HTML (PROGMEM)
```

## Building & Flashing

```bash
# Install PlatformIO
brew install platformio

# Build and upload (auto-detects USB port)
pio run -t upload

# Or specify port
pio run -t upload --upload-port /dev/cu.usbserial-XXXXX

# Monitor serial output
pio device monitor
```

## Configuration

On first boot, the device creates a WiFi access point named `TRMNL-XXXXXX`. Connect to it and open `http://192.168.4.1` to configure:

- **WiFi Network** — scanned automatically
- **WiFi Password**
- **Server URL** — defaults to `https://usetrmnl.com`, change for BYOS

## BYOS (Bring Your Own Server)

Point the Server URL at any server implementing the TRMNL API:

- `GET /api/setup` — device registration (MAC in `ID` header)
- `GET /api/display` — returns image URL and refresh rate

See the companion [trmnl-byos](https://github.com/rolly46/trmnl-byos) server for a ready-to-deploy example.

## Display Driver Notes

This firmware uses the [LilyGo-EPD47](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47) library (`master` branch for ESP32, NOT `esp32s3`). Key API:

- `epd_init()` — no params
- Framebuffer: `heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM)` — 4-bit packed, 2px/byte
- Grayscale: 0x0 = black, 0xF = white
- Draw: `epd_poweron()` → `epd_clear()` → `epd_draw_grayscale_image()` → `epd_poweroff()`

## API Headers

### GET /api/setup
| Header | Value |
|--------|-------|
| `ID` | Device MAC address |
| `FW-Version` | Firmware version |
| `Model` | `lilygo_t5_47` |

### GET /api/display
| Header | Value |
|--------|-------|
| `ID` | Device MAC address |
| `Access-Token` | API key from setup |
| `Refresh-Rate` | Current refresh rate (seconds) |
| `Battery-Voltage` | Battery voltage |
| `RSSI` | WiFi signal strength |
| `Width` | `960` |
| `Height` | `540` |

## Licence

MIT
