# Contributing to ESP32-DIV

Thank you for your interest in contributing to **ESP32-DIV**! This document outlines the process for reporting bugs, requesting features, and submitting code or documentation improvements.

---

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [How to Contribute](#how-to-contribute)
  - [Reporting Bugs](#reporting-bugs)
  - [Suggesting Features](#suggesting-features)
  - [Submitting Pull Requests](#submitting-pull-requests)
- [Development Setup](#development-setup)
- [Coding Standards](#coding-standards)
- [Commit Message Guidelines](#commit-message-guidelines)
- [Branch Naming](#branch-naming)

---

## Code of Conduct

By participating in this project you agree to be respectful and constructive. Harassment, discrimination, or abusive behaviour of any kind will not be tolerated.

---

## Getting Started

1. **Fork** the repository using the Fork button at the top of the page.
2. **Clone** your fork locally:
   ```bash
   git clone https://github.com/<your-username>/ESP32-DIV.git
   cd ESP32-DIV
   ```
3. Create a new **branch** for your change:
   ```bash
   git checkout -b feat/your-descriptive-branch-name
   ```
4. Make your changes, then **commit** and **push** to your fork.
5. Open a **Pull Request** against the `dev` branch of `cifertech/ESP32-DIV`.

> All PRs target `dev` first. Once tested and verified on real hardware, changes are merged into `main` as part of a release.

---

## How to Contribute

### Reporting Bugs

Before opening a bug report, please:

- Search [existing issues](https://github.com/cifertech/ESP32-DIV/issues) to avoid duplicates.
- Check the [Troubleshooting & FAQ](README.md#troubleshooting--faq) section of the README.

When creating a bug report, include:

| Field | Details |
|---|---|
| **Firmware version** | e.g. v1.7.2 |
| **Hardware revision** | v1 / v2 / CYD / with Shield |
| **Arduino IDE version** | e.g. 2.3.2 |
| **ESP32 board package version** | 2.0.10 (Espressif) |
| **Steps to reproduce** | Numbered, minimal steps |
| **Expected behaviour** | What should happen |
| **Actual behaviour** | What actually happens |
| **Serial output / logs** | Paste relevant output in a code block |

### Suggesting Features

- Open a [Discussion](https://github.com/cifertech/ESP32-DIV/discussions) first before writing any code this ensures the feature fits the project direction.
- Describe the use case, not just the implementation idea.
- If the feature requires hardware changes, note which modules or pins are involved.
- Once discussed and approved, open an [issue](https://github.com/cifertech/ESP32-DIV/issues/new) with the label `enhancement`.

### Submitting Pull Requests

- Keep PRs **focused** one feature or fix per PR.
- **Test on real hardware** before submitting compile-only checks are not enough. Note which board version you tested on (v1, v2, or CYD).
- Reference the related issue in the PR description: `Fixes #123` or `Closes #456`.
- Ensure your branch is up to date with `dev` before opening a PR:
  ```bash
  git fetch upstream
  git rebase upstream/dev
  ```
- Fill in the PR template completely.
- All PRs require review from a maintainer before merging.

---

## Development Setup

### Requirements

| Tool | Version |
|---|---|
| Arduino IDE | 2.x (recommended) |
| ESP32 board package (Espressif) | **2.0.10 exactly** |
| Python + esptool | For manual flashing |

### Installing the ESP32 Board Package

1. Open Arduino IDE → **File → Preferences**.
2. Add this URL to *Additional Boards Manager URLs*:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools → Board → Boards Manager**, search for `esp32`, and install version **2.0.10** by Espressif Systems. Do not use a newer version it may cause compatibility issues.

### Replacing platform.txt

After installing the board package, you must replace the default `platform.txt` with the one included in the repo:

- Find the file in the repo at `Flash File/platform.txt`
- Replace the file in your Arduino15 ESP32 package directory:
  - **Windows:** `C:\Users\<user>\AppData\Local\Arduino15\packages\esp32\hardware\esp32\2.0.10\platform.txt`
  - **macOS:** `~/Library/Arduino15/packages/esp32/hardware/esp32/2.0.10/platform.txt`
  - **Linux:** `~/.arduino15/packages/esp32/hardware/esp32/2.0.10/platform.txt`

### Installing Libraries

Copy every folder inside `Libraries/` from the repo into your Arduino `libraries` directory. **Do not use the Library Manager versions** the repo includes customized versions required for correct pin mapping and display configuration.

Key libraries included:

- `TFT_eSPI`
- `PCF8574`
- `XPT2046_Touchscreen`
- `NimBLE-Arduino`
- `RCSwitch`
- `ELECHOUSE_CC1101_SRC_DRV`
- `arduinoFFT`

### Arduino IDE Settings

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module (v2) / ESP32 Dev Module (v1, CYD) |
| Flash Size | 16MB |
| Partition Scheme | Minimal SPIFFS |
| PSRAM | OPI PSRAM |
| Upload Speed | 921600 |

### Entering Download Mode

If the board does not enter download mode automatically during upload, hold **BOOT**, press **RESET**, then release **BOOT** before clicking Upload.

---

## Coding Standards

- Follow the existing **namespace-per-feature** pattern (`namespace WifiScan { ... }`).
- Every feature module must expose at minimum a `Setup()` and `Loop()` function.
- Use `feature_exit_requested = true` to exit a feature loop rather than calling `return` or `break` directly inside the loop.
- Avoid blocking `delay()` calls longer than **200 ms** inside feature loops use non-blocking timing with `millis()`.
- All display writes should respect the active theme via `UI_BG`, `UI_FG`, `UI_TEXT`, `UI_ICON`, and `UI_ACCENT` constants defined in `shared.h`.
- Hardware-specific pin assignments belong in `shared.h` using `#if defined(CONFIG_IDF_TARGET_ESP32S3)` / `#elif defined(CONFIG_IDF_TARGET_ESP32)` guards never hardcode pins directly in feature files.
- Do **not** use `Serial.print` in production code paths use the built-in Serial Monitor feature or TFT debug overlays.
- Use `#pragma once` instead of header guards for new header files.

---

## Commit Message Guidelines

Follow the **Conventional Commits** specification:

```
<type>(<scope>): <short summary>

[optional body]
[optional footer]
```

| Type | When to use |
|---|---|
| `feat` | New feature or module |
| `fix` | Bug fix |
| `docs` | Documentation only changes |
| `refactor` | Code change with no feature or fix |
| `chore` | Build, CI, dependency updates |
| `style` | Formatting, whitespace (no logic change) |

**Examples:**
```
feat(wifi): add Karma Attack to WiFi menu
fix(rfid): resolve crash when no card is present on startup
fix(v1): correct battery ADC pin assignment in shared.h
docs: update CONTRIBUTING with dev branch workflow
chore(ci): add Arduino compile check workflow
```

---

## Branch Naming

| Pattern | Example |
|---|---|
| `feat/<description>` | `feat/karma-attack` |
| `fix/<description>` | `fix/wifi-scanner-nvs-crash` |
| `docs/<description>` | `docs/add-contributing-guide` |
| `chore/<description>` | `chore/update-libraries` |

---

> Questions? Start a [Discussion](https://github.com/cifertech/ESP32-DIV/discussions) or open an [Issue](https://github.com/cifertech/ESP32-DIV/issues). We appreciate every contribution, no matter how small!
