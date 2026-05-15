<div align="center">

<div align="center">
  <img src="https://github.com/user-attachments/assets/a30bde48-e39d-4b11-8749-3401bcb82a68" width="150">
  <h1><span>ESP32-DIV</span></h1>
</div>

  <p>
    ESP32DIV - Multi-purpose wireless offensive and defensive toolkit powered by an ESP32
  </p>
   
<!-- Badges -->
<a href="https://github.com/cifertech/ESP32-DIV" title="Go to GitHub repo"><img src="https://img.shields.io/static/v1?label=cifertech&message=ESP32-DIV&color=purple&logo=github" alt="cifertech - ESP32-DIV"></a>
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/cifertech/esp32-div/total)
<a href="https://github.com/cifertech/ESP32-DIV"><img src="https://img.shields.io/github/stars/cifertech/ESP32-DIV?style=social" alt="stars - ESP32-DIV"></a>
<a href="https://github.com/cifertech/ESP32-DIV"><img src="https://img.shields.io/github/forks/cifertech/ESP32-DIV?style=social" alt="forks - ESP32-DIV"></a>
   
<h4>
    <a href="https://twitter.com/techcifer">TWITTER(X)</a>
  <span> · </span>
    <a href="https://www.instagram.com/cifertech/">INSTAGRAM</a>
  <span> · </span>
    <a href="https://www.youtube.com/c/techcifer">YOUTUBE</a>
  <span> · </span>
    <a href="https://cifertech.net/">WEBSITE</a>
  </h4>
</div> 


---

## 📖 Explore the ESP32-DIV Wiki

Complete project story, in-depth tutorials, and all the features in [Wiki](https://github.com/cifertech/ESP32-DIV/wiki)! From Wi-Fi deauthentication attacks to Sub-GHz signal replay, the Wiki covers everything you need to get started. [Click here to explore now!](https://github.com/cifertech/ESP32-DIV/wiki)


<div>&nbsp;</div>

<!-- About the Project -->
## :star2: About the Project
ESP32-DIV is an open-source, multi-band wireless toolkit built on the **ESP32-S3**. It covers Wi-Fi, BLE, 2.4GHz, Sub-GHz, IR, RFID/NFC, and GPS. all from a compact handheld device with a touchscreen UI. Whether you're analyzing wireless traffic, testing signal resilience, or building your own RF tools, ESP32-DIV gives you a single platform to do it all.

> [!WARNING]
> This project is intended for **educational and research purposes only**. Use only on networks and devices you own or have explicit permission to test. Unauthorized use may violate local laws.



<div>&nbsp;</div>

<!-- Features -->
## :dart: Features

<details>
<summary><strong>📡 Wi-Fi</strong></summary>
  
| Tool | Description |
|------|-------------|
| Packet Monitor | Real-time waterfall graph across all 14 channels; optional PCAP logging to SD |
| Wi-Fi Scanner | Lists nearby networks with extended details |
| Beacon Spammer | Broadcasts fake SSIDs (custom or random) |
| Deauth Attack | Sends deauthentication frames to disrupt client connections |
| Deauth Detector | Monitors for incoming deauth attacks |
| Captive Portal | AP + DNS + web server; clone networks and force sign-in pages |
| Probe Flood | Floods probe requests to stress-test APs |
 
</details>
<details>
<summary><strong>🔵 Bluetooth</strong></summary>
  
| Tool | Description |
|------|-------------|
| BLE Scanner | Discovers hidden and visible BLE devices |
| BLE Sniffer | Tracks MAC, RSSI, packet count, and last-seen time |
| BLE Spoofer | Broadcasts fake BLE advertisements |
| Sour Apple | Spoof Apple BLE advertisements (e.g., AirDrop popups) |
| BLE Jammer | Disrupts BLE and classic Bluetooth channels |
| BLE Rubber Ducky | Acts as a BLE keyboard; executes scripts from `/ducky` on SD |
 
</details>
<details>
<summary><strong>📶 2.4GHz</strong></summary>
  
| Tool | Description |
|------|-------------|
| 2.4GHz Scanner | Spectrum analyzer across 128 channels (Zigbee, custom RF, etc.) |
| Protokill | Disrupts Zigbee, Wi-Fi, and other 2.4GHz protocols |
 
</details>
<details>
<summary><strong>📻 Sub-GHz</strong></summary>
  
| Tool | Description |
|------|-------------|
| Replay Attack | Captures and replays Sub-GHz commands (e.g., garage doors, remotes) |
| Sub-GHz Jammer | Disrupts Sub-GHz communication across various bands |
| Saved Profiles | Stores and manages captured signal profiles |
 
</details>
<details>
<summary><strong>📺 Infrared (IR)</strong></summary>
  
| Tool | Description |
|------|-------------|
| IR Replay Attack | Captures real IR presses, visualizes, replays, and saves to SD |
| IR Saved Profiles | Browses IR captures; preserves signal and carrier frequency |
| Universal IR Controller | Built-in profiles, SD imports, favorites, and remote-style control |
 
</details>
<details>
<summary><strong>🧲 RFID / NFC</strong></summary>
  
| Tool | Description |
|------|-------------|
| Card Reader | Reads UID and tag identification |
| Card Clone | Copies supported writable tags |
| Dump | Reads sectors/blocks when keys are available |
| Decode Access | Interprets access bits and ACL-style fields from dumps |
| Erase | Wipes supported writable tags |
| Jam Reader | Impedes another reader with RF patterns |
| Tag Disrupt | Advanced disruption flows for authorized physical tests |
| Disrupt Emulate | Disruption combined with emulation-style flows |
 
</details>
<details>
<summary><strong>🛰️ GPS</strong></summary>
  
| Tool | Description |
|------|-------------|
| Wardriver | Logs GNSS position with Wi-Fi/BLE observations to SD |
| Satellite Scanner | Shows satellites in view, signal strength, and fix diagnostics |
 
</details>
<details>
<summary><strong>🧰 Device & System</strong></summary>
  
| Tool | Description |
|------|-------------|
| Serial Monitor | Mirrors serial traffic on the TFT for field debugging |
| SD File Manager | Browses and manages files on the SD card |
| Update Firmware | Flashes new firmware from SD |
| Touch Calibrate | Four-corner XPT2046 touchscreen calibration |
| Settings | Brightness, dark/light theme, NeoPixel, background auto-scan |
 
</details>



<div>&nbsp;</div>

<!-- ESP32-DIV --> 
<table>
  <tr>
    <td style="text-align: center;">
      <img src="https://github.com/user-attachments/assets/52e5be67-9dc7-4b08-bd2a-0a4e4235cf9d" alt="ESP32-DIV Beta" style="width: 1920px; border: 1px solid #ccc; border-radius: 5px;">
      <p style="font-style: italic; font-size: 14px; margin-top: 5px;">ESP32-DIV v2</p>
    </td>    
  </tr>
</table>

<table>
  <tr>
    <td style="text-align: center;">
      <img src="https://github.com/user-attachments/assets/466ffd1b-9807-47ce-b221-5a6bffc1aa7d" alt="ESP32-DIV Beta" style="width: 600px; border: 1px solid #ccc; border-radius: 5px;">
      <p style="font-style: italic; font-size: 14px; margin-top: 5px;">ESP32-DIV Beta</p>
    </td>    
    <td style="text-align: center;">
      <img src="https://github.com/user-attachments/assets/fd8ba7d9-0409-4180-af42-a3e6e82b29b3" alt="ESP32-DIV v1" style="width: 600px; border: 1px solid #ccc; border-radius: 5px;">
      <p style="font-style: italic; font-size: 14px; margin-top: 5px;">ESP32-DIV v1</p>
    </td>
  </tr>
</table>

<div>&nbsp;</div>

<!-- Hardware Overview --> 
## 🔧 Hardware Overview

ESP32DIV consists of two boards:

### 🧠 Main Board
- **ESP32-S3** – Main microcontroller with Wi-Fi and BLE
- **ILI9341 TFT Display** – 2.8" UI display
- **LF33** – 3.3V regulator
- **IP5306** – Lithium battery charging and protection
- **CP2102** – USB to serial for flashing
- **PCF8574** – I/O expander for buttons
- **SD Card Slot** – Stores logs and captured signals
- **Push Buttons** – Navigation and interaction
- **Antenna Connector** – External antenna support
- **WS2812 NeoPixels** - Giving better feedback
- **Buzzer** - It shares a GPIO with the battery voltage divider, so using it is optional.

### 🛡️ Shield
- **3x NRF24 Modules** – 2.4GHz jamming and spoofing
- **1x CC1101 Module** – Sub-GHz jamming and replay
- **Multiple antennas** - Extended range
- **IR Transceiver** - Capture & replay IR remotes

<div>&nbsp;</div>

<table>
  <tr>
    <td style="text-align: center;">
      <img src="https://github.com/user-attachments/assets/0aeca1bb-9023-43a0-9f62-f89ced53098f" alt="ESP32-DIV Beta" style="width: 600px; border: 1px solid #ccc; border-radius: 5px;">
      <p style="font-style: italic; font-size: 14px; margin-top: 5px;">ESP32-DIV v2 Main Board</p>
    </td>    
    <td style="text-align: center;">
      <img src="https://github.com/user-attachments/assets/bbfa2c55-02e0-4795-b003-fde06f2d64d9" alt="ESP32-DIV v1" style="width: 600px; border: 1px solid #ccc; border-radius: 5px;">
      <p style="font-style: italic; font-size: 14px; margin-top: 5px;">ESP32-DIV v2 Shield</p>
    </td>
  </tr>
</table>

<table>
  <tr>
    <td style="text-align: center;">
      <img src="https://github.com/user-attachments/assets/b4e3ad5e-4f43-4c08-ae33-d713be0a3855" alt="ESP32-DIV Beta" style="width: 600px; border: 1px solid #ccc; border-radius: 5px;">
      <p style="font-style: italic; font-size: 14px; margin-top: 5px;">ESP32-DIV v1 Main Board</p>
    </td>    
    <td style="text-align: center;">
      <img src="https://github.com/user-attachments/assets/21f10c62-5e6c-4565-8b86-7b89e24680c3" alt="ESP32-DIV v1" style="width: 600px; border: 1px solid #ccc; border-radius: 5px;">
      <p style="font-style: italic; font-size: 14px; margin-top: 5px;">ESP32-DIV v1 Shield</p>
    </td>
  </tr>
</table>

<div>&nbsp;</div>


<!-- License --> 
## :warning: License
 
> Distributed under the MIT License. See LICENSE.txt for more information.

<div>&nbsp;</div>


<!-- Support & Contributions -->
## 💬 Support & Contributions

> - 💬 Found a bug or have a feature request? Open an [Issue](https://github.com/cifertech/ESP32-DIV/issues)
> - ⭐ Like the project? Star the repo!
> - 🛠 Want to contribute? Fork it and submit a pull request.
‎
<div>&nbsp;</div>

<!-- Contact -->
## :handshake: Contact 

> ▶ Support me on Patreon [patreon.com/cifertech](https://www.patreon.com/cifertech)
>
> CiferTech - [@twitter](https://twitter.com/techcifer) - CiferTech@gmali.com
>
> Project Link: [https://github.com/cifertech/ESP32-DIV](https://github.com/cifertech/ESP32-DIV)

<div>&nbsp;</div>

<!-- Troubleshooting -->
## 🛠️ Troubleshooting & FAQ

A quick reference for the most common issues reported by the community. If your problem isn't listed here, please [open an issue](https://github.com/cifertech/ESP32-DIV/issues).

---

### 📡 Wi-Fi

**Q: Wi-Fi Scanner shows no networks or crashes on v1.53+**
> This is a known issue related to the NVS partition becoming full after repeated scans. Flash the latest pre-compiled binary from the `Pre-compiled Bin/` folder, or perform a full chip erase before re-flashing: `esptool.py erase_flash`. Then re-flash the firmware.

**Q: Deauth attack has no effect on the target network**
> Make sure you are within close range of the target AP and client. Some modern routers (802.11w / PMF enabled) are immune to deauthentication frames by design. The attack will not work on those.

**Q: Beacon Spammer SSIDs are not visible on nearby devices**
> Reduce the number of SSIDs in your spam list. Broadcasting too many simultaneously can cause the ESP32-S3 watchdog to trigger a reset.

---

### ⌨️ Keyboard / Input

**Q: The on-screen keyboard is unresponsive or registers wrong characters**
> This is usually a touchscreen calibration issue. Go to **Device & System → Touch Calibrate** and run the four-corner calibration routine. Save the result and restart the device.

**Q: Navigation buttons do not respond**
> Check that the PCF8574 I/O expander is seated correctly on the board. Inspect solder joints on the button headers. If using a custom build, verify the I2C address (default `0x20`) matches the address jumper configuration on the PCF8574.

---

### 🔋 Power & Battery

**Q: What battery type and capacity should I use?**
> The IP5306 charging IC supports a single-cell **3.7V Li-Ion / LiPo** battery. A capacity of **1000–2000 mAh** is recommended for several hours of operation. Do **not** use Li-Ion cells rated above 4.2V or flat-top cells without a protection circuit.

**Q: The device powers off immediately after unplugging USB**
> The IP5306 requires a minimum load current to stay on. If the system draw is too low it will auto-shutdown. Enabling the display backlight or an active scan will keep the load sufficient. Alternatively, a short press of the power button after unplug will re-enable the output.

**Q: Charging LED never turns green / battery never fully charges**
> Confirm your USB power supply can deliver at least **5V 1A**. Underpowered chargers cause the IP5306 to cycle in and out of charge mode.

---

### 🖥️ Firmware & Flashing

**Q: Cannot flash — `esptool` reports "Failed to connect"**
> 1. Hold the **BOOT** button on the ESP32-S3 before connecting USB, then release after the flashing command starts.
> 2. Try a different USB cable (data-capable, not charge-only).
> 3. Check that the CP2102 driver is installed on your OS.

**Q: How do I load the project in Arduino IDE / Android Studio?**
> The firmware is built with the **Arduino IDE** (not Android Studio). Install the **ESP32 board package** by Espressif via Board Manager, then install all libraries listed in the `Libraries/` folder. Set the board to **ESP32S3 Dev Module**, partition scheme to **16MB Flash (3MB APP/9.9MB FATFS)**.

**Q: Firmware update from SD card fails or gets stuck**
> Ensure the `.bin` file is placed in the **root** of the SD card and is named exactly as expected by the OTA routine. Use a FAT32-formatted card with a capacity of 32 GB or less.

---

### 📻 Sub-GHz / NRF24 / CC1101

**Q: Sub-GHz Jammer / Replay shows no received signals**
> Verify the CC1101 module is firmly seated on the shield header. Check SPI connections and confirm the correct frequency band is selected in settings (433 MHz, 868 MHz, or 915 MHz depending on your region).

**Q: 2.4GHz Scanner or Protokill has no effect**
> Confirm all three NRF24 modules are inserted and powered. A missing or poorly seated module will silently reduce jamming coverage.

---

### 🧲 RFID / NFC

**Q: Card Reader shows "No card detected" even when a card is present**
> Ensure the card is held **flat and still** within 2–3 cm of the antenna. High-frequency interference from nearby Wi-Fi or BLE activity can reduce read range — try disabling other active scans temporarily.

---

> 💡 **Tip:** Always check the [Wiki](https://github.com/cifertech/ESP32-DIV/wiki) and existing [Issues](https://github.com/cifertech/ESP32-DIV/issues) before opening a new report. Many common problems are already documented there.

 
