# NA Frame Counter

A Geometry Dash Geode mod (`v5.11.2` / GD `2.2081`) that measures live 240Hz click and release frame windows, renders NaN GD-style circle outline markers with pitch-scaled audio feedback, displays a live tally + NaNDL precision (`L*`) HUD, and ships a 4-tab in-game settings menu.

## Features

- **Live 240Hz Click & Release Window Probing**
  - Captures lightweight physics snapshots in a 4096-frame history buffer during live classic gameplay and runs isolated counterfactual replays (`PlayerObject` + `GJGroundLayer`) across early and late frame offsets using exponential + binary search.
  - Supports **click (press) windows**, **release windows** (dashes, waves, ships, UFOs, swings, spiders, cube holds), **maximum window presets** (`12`, `17`, `30`, `60`, `120`, `240`), **non-fatal click filtering**, and **over-limit badges** (`>Max`).
  - Automatically pauses live probing in platformer levels, Practice checkpoints / StartPos resets clamp safely at attempt start, and probe runs suppress live triggers, particles, camera shakes, and game SFX.

- **NaN GD-Style Visual Markers**
  - Renders world-tracked `CCDrawNode` outlined rings + numeric frame labels (`1`, `2`, `R1`, etc.) at the exact click/release activation position.
  - Configurable **label placement** (`Left`, `Center`, `Top`, `Right`), **5 color palettes** (`NaN GD Classic`, `NaN GD Fire`, `Pastel`, `Monochrome`, `Custom 1-5+` with per-tier color pickers), **circle radius**, **outline thickness**, **marker scale**, **fade duration**, **zoom scaling**, and **release differentiation** (`R` prefix + inner ring).

- **Pitch-Scaled Ding Audio**
  - Plays bundled 44.1 kHz PCM WAV dings (`Classic Bell`, `Soft Chime`, `Crystal Ping`, `Retro Blip`) or a user-selected custom audio file (`.wav`, `.mp3`, `.ogg`).
  - Maps frame window width monotonically from lowest pitch at `1F` up to highest pitch at wider windows via `Musical`, `Linear`, or `Semitones` curves, with GD SFX volume sync and an 8-voice concurrency limit. (Uses FMOD `ChannelGroup` pitch control on Windows/macOS/Android and `FMODAudioEngine::playEffectAdvanced` on iOS.)

- **Corner Tally HUD & Live NaNDL Precision (`L*`) HUD**
  - **Tally HUD**: Displays bucket counts (`1`, `2`, `3`, `4`, `5-6`, `7-9`, `10-12`, or expanded ranges for wider max windows) in `Top Left`, `Top Right`, or `Bottom Right`, with `1 at Bottom` / `1 at Top` ordering, radial `glow.png` pulse animations, and an optional `Last:` badge.
  - **Live NaNDL Precision (`L*`) HUD**: Solves the expected-time-to-clear (`E[T_C] = Target Time`) precision rating `L*` ($\sigma/s$) and single-attempt pass chance `P(C)` incrementally as you play, supporting `NFC (Full)`, `Base`, `Nerve`, `Fatigue`, `CPS`, and `All Models` with configurable FPS, respawn time, target clear hours, and $k_t$, $k_u$, $k_c$ constants.

- **4-Tab In-Game Mod Menu (`F7` / Pause Menu Button)**
  - Open from the Pause Menu button (`resources/button.png`), the `F7` hotkey (`open-menu-keybind`), or Geode Mod Settings (`open-mod-menu`).
  - Includes **Engine**, **Visuals** (with live ring preview & 1–5+ custom color pickers), **Audio** (with 1F/3F/8F/12F pitch test buttons), and **HUD & NaNDL** tabs, plus per-section and full reset actions.

## Project Structure

```text
├── mod.json                         # Geode v5.11.2 manifest & SettingV3 configuration
├── CMakeLists.txt                   # C++23 CMake + Geode build setup
├── logo.png                         # 256x256 mod icon
├── resources/
│   ├── button.png                   # 64x64 pause-menu icon
│   ├── glow.png                     # 64x64 radial HUD pulse texture
│   └── sounds/ding_*.wav            # Bundled 44.1 kHz mono PCM ding variants
├── src/
│   ├── main.cpp                     # Mod entry point, setting listeners, F7 keybind
│   ├── math/NandlCalculator.{hpp,cpp}   # NaNDL E[T_C] & L* solver
│   ├── core/
│   │   ├── FrameConfig.{hpp,cpp}    # Window bands, color palettes, pitch curves
│   │   └── SettingsBridge.{hpp,cpp} # Geode SettingV3 snapshot & writers
│   ├── audio/SoundPlayer.{hpp,cpp}  # Cross-platform FMOD pitch-shifted ding player
│   ├── engine/
│   │   ├── ProbeTypes.hpp           # Simulation structs & constants
│   │   ├── ProbeController.{hpp,cpp}# Level lifecycle & input tracking
│   │   ├── ProbeSimulation.cpp      # 240Hz snapshot replay & binary-search scanner
│   │   ├── ProbeHUD.cpp             # World ring markers, tally HUD, NaNDL HUD
│   │   └── Hooks.cpp                # Geode $modify hooks
│   └── ui/ModMenuPopup.{hpp,cpp}    # 4-tab in-game settings modal
└── tests/
    └── test_core.cpp                # Standalone pure-C++ unit tests (NaNDL + FrameConfig)
```

## Building & Testing

### Core Unit Tests (Standalone C++23)

The pure math and configuration modules (`NandlCalculator` and `FrameConfig`) have zero Cocos2d/Geode dependencies and can be compiled and tested directly on Linux/macOS/Windows:

```bash
g++ -std=c++23 -O2 -Wall -Wextra -Werror -Isrc \
  src/math/NandlCalculator.cpp \
  src/core/FrameConfig.cpp \
  tests/test_core.cpp \
  -o test_core
./test_core
```

### Geode Mod Build

Requires the [Geode SDK](https://github.com/geode-sdk/geode) (`v5.11.2`) and `GEODE_SDK` set in your environment, plus the Geode CLI:

```bash
geode build
```

### Multi-Platform CI Builds

For GitHub Actions builds across `Win64`, `MacOS`, `iOS`, `Android32`, and `Android64` using [`geode-sdk/build-geode-mod`](https://github.com/geode-sdk/build-geode-mod), add `.github/workflows/build.yml` directly via GitHub (requires `workflows` permission).
