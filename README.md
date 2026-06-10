<div align="center">

  <img src="https://github.com/user-attachments/assets/1f70e8ba-d8be-4889-959a-700294068a3e" alt="ESP32-DIV Banner" width="100%"/>

  <br/>
  <br/>

  <p align="center">
    <a href="https://github.com/cifertech/ESP32-DIV"><img src="https://img.shields.io/static/v1?label=cifertech&message=ESP32-DIV&color=orange&logo=github"/></a>
    <a href="https://github.com/cifertech/ESP32-DIV"><img src="https://img.shields.io/github/stars/cifertech/ESP32-DIV?style=social"/></a>
    <a href="https://github.com/cifertech/ESP32-DIV"><img src="https://img.shields.io/github/forks/cifertech/ESP32-DIV?style=social"/></a>
    <img src="https://img.shields.io/github/downloads/cifertech/esp32-div/total?color=orange&label=downloads&logo=github"/>
    <img src="https://img.shields.io/badge/ESP32-Offensive%20%2B%20Defensive-orange?logo=espressif"/>
    <img src="https://img.shields.io/badge/license-MIT-orange"/>
  </p>

  <p align="center">
    <a href="https://twitter.com/techcifer"><img src="https://img.shields.io/badge/Twitter-orange?logo=x&logoColor=black"/></a>
    <a href="https://www.instagram.com/cifertech/"><img src="https://img.shields.io/badge/Instagram-orange?logo=instagram&logoColor=black"/></a>
    <a href="https://www.youtube.com/c/techcifer"><img src="https://img.shields.io/badge/YouTube-orange?logo=youtube&logoColor=black"/></a>
    <a href="https://cifertech.net/"><img src="https://img.shields.io/badge/Website-orange?logo=googlechrome&logoColor=black"/></a>
  </p>

</div>

&nbsp;

## 📖 Explore the ESP32-DIV Wiki

Complete project story, in-depth tutorials, and all the features in [Wiki](https://github.com/cifertech/ESP32-DIV/wiki)! From Wi-Fi deauthentication attacks to Sub-GHz signal replay, the Wiki covers everything you need to get started. [Click here to explore now!](https://github.com/cifertech/ESP32-DIV/wiki)

> 💡 **Tip:** Always check the [Wiki](https://github.com/cifertech/ESP32-DIV/wiki) and existing [Issues](https://github.com/cifertech/ESP32-DIV/issues) before opening a new report. Many common problems are already documented there.


<div>&nbsp;</div>

<!-- About the Project -->
## :star2: About the Project
ESP32-DIV is an open-source, multi-band wireless toolkit built on the **ESP32-S3**. It covers Wi-Fi, BLE, 2.4GHz, Sub-GHz, IR, RFID/NFC, and GPS. all from a compact handheld device with a touchscreen UI. Whether you're analyzing wireless traffic, testing signal resilience, or building your own RF tools, ESP32-DIV gives you a single platform to do it all.


> [!WARNING]
> This project is intended for **educational and research purposes only**. Use only on networks and devices you own or have explicit permission to test. Unauthorized use may violate local laws.





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




<!-- License --> 
## :warning: License
 
Distributed under the MIT License. See LICENSE.txt for more information.




<!-- Support & Contributions -->
## 💬 Support & Contributions

- 💬 Found a bug or have a feature request? Open an [Issue](https://github.com/cifertech/ESP32-DIV/issues)
- ⭐ Like the project? Star the repo!
- 🛠 Want to contribute? Fork it and submit a pull request.
‎


<!-- Contact -->
## :handshake: Contact 

▶ Support me on Patreon [patreon.com/cifertech](https://www.patreon.com/cifertech)

CiferTech - [@twitter](https://twitter.com/techcifer) - CiferTech@gmali.com

Project Link: [https://github.com/cifertech/ESP32-DIV](https://github.com/cifertech/ESP32-DIV)

 
