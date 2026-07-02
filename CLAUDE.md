# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Two halves of one product for controlling the lighting of a physical scale-model **diorama** over MQTT:

1. **Firmware** ([src/main.cpp](src/main.cpp)) — ESP32 that drives shift-register LED outputs, subscribing to MQTT for its light state.
2. **Controller app** ([app/www/index.html](app/www/index.html)) — a self-contained HTML/JS page (wrapped as a Capacitor Android app) that publishes light state to MQTT.

Both talk to the same EMQX cloud broker. There is no backend of our own — the broker relays retained state between the app and the boards.

## Build / run commands

PlatformIO is not on `PATH`; the CLI lives at `~/.platformio/penv/Scripts/pio.exe` (Windows). All commands run from the repo root.

- **Build firmware:** `pio run -e esp32dev`
- **Flash + build:** `pio run -e esp32dev -t upload`
- **Serial monitor:** `pio device monitor` (115200 baud, already set via `monitor_speed`)
- **Run the app locally:** open [app/www/index.html](app/www/index.html) directly in a browser — it is fully static; `mqtt.min.js` is bundled locally next to it. No build/bundle step.
- **Sync app into the Android project:** `cd app && npx cap sync android`, then build via Gradle / Android Studio (`app/android/`).

There is **no test suite or linter**. `app/package.json`'s `test` script is a stub, and there are no PlatformIO unit tests. Verification is done on real hardware via the serial monitor and by watching the app/broker.

## Firmware architecture ([src/main.cpp](src/main.cpp))

Single-file Arduino sketch. `setup()` provisions and connects; `loop()` is a non-blocking state machine (no `delay()` in the main loop) that services the watchdog, heartbeat, WiFi, and MQTT each pass.

**Bit → house mapping (the core invariant).** Each board shifts out **40 bits = 5 bytes** over VSPI, MSB first, into a shift-register chain; 1 bit = 1 house's lights. A 5-bit DIP switch selects the board address; **only addresses 0 and 1 are valid** (anything else → fatal halt loop). The 58 houses + 1 landscape split across the two boards:

| Board (DIP addr) | MQTT topic | Contents | Bits |
|---|---|---|---|
| 0 | `diorama/imodel/house_0` | houses 1..40 | bits 0..39 |
| 1 | `diorama/imodel/house_1` | houses 41..58 | bits 0..17 |
| 1 | (same topic) | landscape | bit 18 |

Besides the two bitmask topics there are: `diorama/imodel/random` (retained `'1'`/`'0'` — firmware runs a per-house PWM fade animation, "city at night"; houses ignore the bitmask while on, landscape bit still applies) and `diorama/imodel/status_<addr>` (retained `'1'` published on connect, LWT `'0'` from the broker when a board drops — the app shows this per-board online state in its settings popover).

**Soft PWM.** LED outputs are refreshed by a 64-level soft-PWM engine: a hardware-timer ISR (~130 µs tick, ~120 Hz frame) compares each channel's duty (`bri[]`) against a phase-staggered counter and bit-bangs all 40 bits to the shift registers every tick. Plain on/off uses duty 0/64 only (outputs static); intermediate duties exist only during the random animation. Everything the ISR touches is `IRAM_ATTR` + DRAM.

**State model.** MQTT payload is a **decimal `uint64` bitmask** (validated `<= 0xFFFFFFFFFF`). The board stores it in `houseState` and pushes the whole bitmask to the shift registers on every message — it is absolute state, never a delta. This is why retained messages + reconnects resync automatically.

**Connectivity & resilience** (constants at the top of the file):
- WiFi provisioning via **WiFiManager** captive portal: AP `Imodel_Controller_<addr>` / pass `68686868`, 180 s timeout, else restart.
- **MQTT over TLS** (port 8883) using `WiFiClientSecure` with `setInsecure()` (no cert validation). Retries every 5 s.
- Auto-restart guards: WiFi lost > 30 s → `ESP.restart()`; MQTT lost > 5 min → restart.
- **Task watchdog** (30 s) fed from `loop()` and from every busy-wait; **heartbeat LED** on GPIO27 toggles every 500 ms so a hung board is visible.
- **Factory test:** hold `FACTORY_PIN` (GPIO26, active LOW) at boot → blink-all then sequential single-LED scan, looping until released.

**Pins** are defined at the top of the file; the authoritative hardware map is [docs/ESP32_PIN_ASSIGNMENTS_20260428.md](docs/ESP32_PIN_ASSIGNMENTS_20260428.md). Note GPIO34/35 are input-only with **no internal pull-up** (need external pull-ups; DIP bits 0/1 use plain `INPUT`), while DIP bits 2/3/4 use `INPUT_PULLUP`. VSPI: CLK=18, MOSI=23, LATCH=5 (manual, not SPI SS).

## App architecture ([app/www/index.html](app/www/index.html))

One HTML file: inline CSS + one IIFE of JS, Vietnamese UI, Capacitor Android wrapper (appId `com.luattruong2908.ledcontroller`, name "Monrei LED" — the Capacitor branding differs from the diorama UI it currently ships).

- Connects to the **same broker over WSS** (port 8084) via `mqtt.js`.
- Mirrors the firmware bit layout exactly: keeps a per-board **`BigInt` bitmask** (`board[0]`, `board[1]`), maps house number → `{board, bit}`, and derives master toggles (whole project / all houses / landscape) from the masks.
- **Publishes absolute state** to `diorama/imodel/house_0|1` with `qos:1, retain:true`, **debounced 150 ms** (safe precisely because it publishes accumulated state, not commands). Subscribes to both topics so multiple app instances stay in sync.

## The one rule that spans files

The **bit layout, house↔bit↔board mapping, topic names, and broker credentials are duplicated** across [src/main.cpp](src/main.cpp), [app/www/index.html](app/www/index.html), and [tools/clear-retained.html](tools/clear-retained.html). Any change to how houses map to bits, the topic scheme, or the broker/credentials must be made in **all** of them or the app and boards silently stop agreeing.

## Other files

- [tools/clear-retained.html](tools/clear-retained.html) — one-off utility page that clears all retained messages under `diorama/imodel/#` (or `#`) on the broker.
- [test/backup.cpp](test/backup.cpp) — a **stashed alternate firmware** (3-bit DIP / block A·B·C scheme, a different broker). It is not compiled by `pio run` (only `src/` is) and is not a PlatformIO test; treat it as a reference backup, not live code.
- `.claude/plans/monrei-*.md` — plans for a **"Monrei apartment" refactor** (9 on/off group topics under `imodel/monrei/...`, hardcoded board addr 3, DIP removed). This is **not** reflected in the current code. The current [src/main.cpp](src/main.cpp) and [app/www/index.html](app/www/index.html) are the diorama model described above — treat them as the source of truth, not the plans.
