<div align="center">

<div align="center">
  <img src="https://github.com/user-attachments/assets/a30bde48-e39d-4b11-8749-3401bcb82a68" width="150">
  <h1><span>ESP32-DIV</span></h1>
</div>

  <p>
    ESP32DIV - Advanced Wireless Toolkit
  </p>
   
<!-- Badges -->
<a href="https://github.com/cifertech/ESP32-DIV" title="Go to GitHub repo"><img src="https://img.shields.io/static/v1?label=cifertech&message=ESP32-DIV&color=purple&logo=github" alt="cifertech - ESP32-DIV"></a>
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/cifertech/esp32-div/total)
<a href="https://github.com/cifertech/ESP32-DIV"><img src="https://img.shields.io/github/stars/cifertech/ESP32-DIV?style=social" alt="stars - ESP32-DIV"></a>
<a href="https://github.com/cifertech/ESP32-DIV"><img src="https://img.shields.io/github/forks/cifertech/ESP32-DIV?style=social" alt="forks - ESP32-DIV"></a>
   
<h4>
    <a href="https://twitter.com/cifertech1">TWITTER</a>
  <span> · </span>
    <a href="https://www.instagram.com/cifertech/">INSTAGRAM</a>
  <span> · </span>
    <a href="https://www.youtube.com/c/techcifer">YOUTUBE</a>
  <span> · </span>
    <a href="https://cifertech.net/">WEBSITE</a>
  </h4>
</div> 
 
<br />

## 📖 Explore the ESP32-DIV Wiki

Complete project story, in-depth tutorials, and all the features in [Wiki](https://github.com/cifertech/ESP32-DIV/wiki)! From Wi-Fi deauthentication attacks to Sub-GHz signal replay, the Wiki covers everything you need to get started. [Click here to explore now!](https://github.com/cifertech/ESP32-DIV/wiki)


<div>&nbsp;</div>

<!-- About the Project -->
## :star2: About the Project
Welcome to **ESP32DIV**, a powerful open-source multi-band wireless toolkit built on the ESP32!  
This device supports **Wi-Fi**, **BLE**, **2.4GHz**, and **Sub-GHz** frequency bands and is designed for **wireless testing**, **signal analysis**, **jammer development**, and **protocol spoofing**.

> ⚠️ This project is intended for **educational and research purposes only**. Do not use it for malicious activities or unauthorized access.

<div align="center"> 
  <img src="https://github.com/user-attachments/assets/2a6250cc-270d-460a-9875-5c2654d10fcf" alt="screenshot" width="Auto" height="Auto" />
</div>

<div>&nbsp;</div>

<!-- Features -->
## :dart: Features

#### 📡 Wi-Fi Tools
- **Packet Monitor** – Real-time waterfall graph of all 14 Wi-Fi channels
- **Beacon Spammer** – Broadcast fake SSIDs (custom or random)
- **Deauth Detector** – Monitor for Wi-Fi deauthentication attacks
- **Wi-Fi Scanner** – List nearby Wi-Fi networks with extended details
- **Wi-Fi Deauthentication Attack** - Send deauthentication frames to disrupt client connections
- **Captive Portal**  - ESP32 runs as AP + DNS + web server. Clone networks, force sign-in pages, all before HTTPS/authentication

#### 🔵 Bluetooth Tools
- **BLE Jammer** – Disrupt BLE and classic Bluetooth channels
- **BLE Spoofer** – Broadcast fake BLE advertisements
- **Sour Apple** – Spoof Apple BLE advertisements (e.g., AirDrop)
- **BLE Scanner** – Scan for hidden and visible BLE devices
- **BLE Sniffer** - Scans BLE advertisements, tracking MAC, RSSI, packet count, and last-seen time. Suspicious devices are highlighted
- **BLE Rubber Ducky** - Acts as a BLE keyboard and executes SD card scripts. Keys are released and advertising stops on exit

#### 📶 2.4GHz Tools
- **2.4GHz Scanner** – Spectrum analyzer for 128 channels (Zigbee, custom RF, etc.)
- **Protokill** – Disrupt Zigbee, Wi-Fi, and other 2.4GHz protocols

#### 📻 Sub-GHz Tools
- **Replay Attack** – Capture and replay Sub-GHz commands (e.g., door remotes)
- **Sub-GHz Jammer** – Disrupt Sub-GHz communication across various bands
- **Saved Profiles** – Store and manage captured signal profiles

#### 📺 Infrared (IR) Tools
- **IR Replay Attack** - Capture real IR remote presses, visualize, replay, and save to SD
- **IR Saved Profiles** - Browser for IR captures, preserving signal and carrier frequency for accurate retransmission

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
- **Multiple antennas**
- **Infrared**

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
 
Distributed under the MIT License. See LICENSE.txt for more information.


<!-- Resources --> 
## 📎 Resources

- 📖 [Project WiKi](https://github.com/cifertech/ESP32-DIV/wiki)
- 🔗 [GitHub Releases](https://github.com/cifertech/ESP32-DIV/releases)
- 🎥 [YouTube Video](https://youtu.be/jVp1zlcsrOY)


<!-- Contact -->
## :handshake: Contact 

▶ Support me on Patreon [patreon.com/cifertech](https://www.patreon.com/cifertech)

CiferTech - [@twitter](https://twitter.com/techcifer) - CiferTech@gmali.com

Project Link: [https://github.com/cifertech/ESP32-DIV](https://github.com/cifertech/ESP32-DIV)


<!-- Support & Contributions -->
## 💬 Support & Contributions

- 💬 Found a bug or have a feature request? Open an [Issue](https://github.com/cifertech/ESP32-DIV/issues)
- ⭐ Like the project? Star the repo!
- 🛠 Want to contribute? Fork it and submit a pull request.
 
 
