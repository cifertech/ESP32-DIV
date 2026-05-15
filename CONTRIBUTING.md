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
   git checkout -b fix/your-descriptive-branch-name
   ```
4. Make your changes, then **commit** and **push** to your fork.
5. Open a **Pull Request** against the `main` branch of `cifertech/ESP32-DIV`.

---

## How to Contribute

### Reporting Bugs

Before opening a bug report, please:

- Search [existing issues](https://github.com/cifertech/ESP32-DIV/issues) to avoid duplicates.
- Check the [Troubleshooting & FAQ](README.md#troubleshooting--faq) section of the README.

When creating a bug report, include:

| Field | Details |
|---|---|
| **Firmware version** | e.g. v1.6.0 |
| **Hardware revision** | Main board only / with Shield / DIY |
| **Arduino IDE version** | e.g. 2.3.2 |
| **ESP32 board package version** | e.g. 3.0.5 (Espressif) |
| **Steps to reproduce** | Numbered, minimal steps |
| **Expected behaviour** | What should happen |
| **Actual behaviour** | What actually happens |
| **Serial output / logs** | Paste relevant output in a code block |

### Suggesting Features

- Open an [issue](https://github.com/cifertech/ESP32-DIV/issues/new) with the label `enhancement`.
- Describe the use case, not just the implementation idea.
- If the feature requires hardware changes, note which modules or pins are involved.

### Submitting Pull Requests

- Keep PRs **focused** — one feature or fix per PR.
- Reference the related issue in the PR description: `Fixes #123` or `Closes #456`.
- Ensure your branch is up to date with `main` before opening a PR:
  ```bash
  git fetch upstream
  git rebase upstream/main
  ```
- Fill in the PR template completely.
- All PRs require at least one approval from a maintainer before merging.

---

## Development Setup

### Requirements

| Tool | Version |
|---|---|
| Arduino IDE | 2.x (recommended) |
| ESP32 board package (Espressif) | 3.x |
| Python + esptool | For manual flashing |

### Installing the ESP32 Board Package

1. Open Arduino IDE → **File → Preferences**.
2. Add this URL to *Additional Boards Manager URLs*:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools → Board → Boards Manager**, search for `esp32`, and install the **Espressif Systems** package.

### Arduino IDE Settings

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB |
| Partition Scheme | 16MB Flash (3MB APP / 9.9MB FATFS) |
| PSRAM | OPI PSRAM |
| Upload Speed | 921600 |

### Installing Libraries

Copy every folder inside `Libraries/` into your Arduino `libraries` directory, or install them individually via the Library Manager. Key libraries include:

- `TFT_eSPI`
- `PCF8574`
- `XPT2046_Touchscreen`
- `NimBLE-Arduino`
- `RCSwitch`
- `ELECHOUSE_CC1101_SRC_DRV`
- `arduinoFFT`

---

## Coding Standards

- Follow the existing **namespace-per-feature** pattern (`namespace WifiScan { ... }`).
- Every feature module must expose at minimum a `Setup()` and `Loop()` function.
- Use `feature_exit_requested = true` to exit a feature loop rather than calling `return` or `break` directly inside the loop.
- Avoid blocking `delay()` calls longer than **200 ms** inside feature loops — use non-blocking timing with `millis()`.
- All display writes should respect the active theme via `UI_BG`, `UI_FG`, `UI_TEXT`, `UI_ICON`, and `UI_ACCENT` constants defined in `shared.h`.
- Do **not** use `Serial.print` in production code paths — use the built-in Serial Monitor feature or TFT debug overlays.
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
feat(wifi): add channel hopping option to Packet Monitor
fix(rfid): resolve crash when no card is present on startup
docs: add CONTRIBUTING guide
chore(ci): add Arduino compile check workflow
```

---

## Branch Naming

| Pattern | Example |
|---|---|
| `feat/<description>` | `feat/rfid-sector-dump` |
| `fix/<description>` | `fix/wifi-scanner-nvs-crash` |
| `docs/<description>` | `docs/add-contributing-guide` |
| `chore/<description>` | `chore/update-libraries` |

---

> Questions? Start a [Discussion](https://github.com/cifertech/ESP32-DIV/discussions) or open an [Issue](https://github.com/cifertech/ESP32-DIV/issues). We appreciate every contribution, no matter how small!
