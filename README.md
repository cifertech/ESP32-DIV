<div align="center">

  <img src="https://user-images.githubusercontent.com/62047147/195847997-97553030-3b79-4643-9f2c-1f04bba6b989.png" alt="logo" width="100" height="auto" />
  <h1>ESP32-DIV</h1>
   
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

<div>&nbsp;</div>

## 📖 Explore the ESP32-DIV Wiki

Complete project story, in-depth tutorials, and all the features in [Wiki](https://github.com/cifertech/ESP32-DIV/wiki)! From Wi-Fi deauthentication attacks to Sub-GHz signal replay, the Wiki covers everything you need to get started. [Click here to explore now!](https://github.com/cifertech/ESP32-DIV/wiki)


<div>&nbsp;</div>

<!-- About the Project -->
## :star2: About the Project
Welcome to **ESP32DIV**, a powerful open-source multi-band wireless toolkit built on the ESP32!  
This device supports **Wi-Fi**, **BLE**, **2.4GHz**, and **Sub-GHz** frequency bands and is designed for **wireless testing**, **signal analysis**, **jammer development**, and **protocol spoofing**.

> ⚠️ This project is intended for **educational and research purposes only**. Do not use it for malicious activities or unauthorized access.

<div align="center"> 
  <img src="https://github.com/user-attachments/assets/a3c46ee0-5da8-421e-aabf-b3120f21eb10" alt="screenshot" width="Auto" height="Auto" />
</div>

<div>&nbsp;</div>

<!-- Features -->
## :dart: Features

#### 📡 Wi-Fi Tools
- **Packet Monitor** – Real-time waterfall graph of all 14 Wi-Fi channels
- **Beacon Spammer** – Broadcast fake SSIDs (custom or random)
- **Deauth Detector** – Monitor for Wi-Fi deauthentication attacks
- **Wi-Fi Scanner** – List nearby Wi-Fi networks with extended details

#### 🔵 Bluetooth Tools
- **BLE Jammer** – Disrupt BLE and classic Bluetooth channels
- **BLE Spoofer** – Broadcast fake BLE advertisements
- **Sour Apple** – Spoof Apple BLE advertisements (e.g., AirDrop)
- **BLE Scanner** – Scan for hidden and visible BLE devices

#### 📶 2.4GHz Tools
- **2.4GHz Scanner** – Spectrum analyzer for 128 channels (Zigbee, custom RF, etc.)
- **Protokill** – Disrupt Zigbee, Wi-Fi, and other 2.4GHz protocols

#### 📻 Sub-GHz Tools
- **Replay Attack** – Capture and replay Sub-GHz commands (e.g., door remotes)
- **Sub-GHz Jammer** – Disrupt Sub-GHz communication across various bands
- **Saved Profiles** – Store and manage captured signal profiles

<div>&nbsp;</div>

<!-- ESP32-DIV --> 
## :eyes: ESP32-DIV Versions:

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
- **ESP32-U (16MB)** – Main microcontroller with Wi-Fi and BLE
- **ILI9341 TFT Display** – 2.4" UI display
- **LF33** – 3.3V regulator
- **TP4056** – Lithium battery charging and protection
- **CP2102** – USB to serial for flashing
- **PCF8574** – I/O expander for buttons
- **SD Card Slot** – Stores logs and captured signals
- **Push Buttons** – Navigation and interaction
- **Antenna Connector** – External antenna support

### 🛡️ Shield
- **3x NRF24 Modules** – 2.4GHz jamming and spoofing
- **1x CC1101 Module** – Sub-GHz jamming and replay

<table>
  <tr>
    <td style="text-align: center;">
      <img src="https://github.com/user-attachments/assets/b4e3ad5e-4f43-4c08-ae33-d713be0a3855" alt="ESP32-DIV Beta" style="width: 600px; border: 1px solid #ccc; border-radius: 5px;">
      <p style="font-style: italic; font-size: 14px; margin-top: 5px;">ESP32-DIV Main Board</p>
    </td>    
    <td style="text-align: center;">
      <img src="https://github.com/user-attachments/assets/21f10c62-5e6c-4565-8b86-7b89e24680c3" alt="ESP32-DIV v1" style="width: 600px; border: 1px solid #ccc; border-radius: 5px;">
      <p style="font-style: italic; font-size: 14px; margin-top: 5px;">ESP32-DIV Shield</p>
    </td>
  </tr>
</table>

<div>&nbsp;</div>

<!-- License --> 
## :warning: License
 
Distributed under the MIT License. See LICENSE.txt for more information.

<div>&nbsp;</div>

<!-- Resources --> 
## 📎 Resources

- 📖 [Project WiKi](https://github.com/cifertech/ESP32-DIV/wiki)
- 🔗 [GitHub Releases](https://github.com/cifertech/ESP32-DIV/releases)
- 🎥 [YouTube Video](https://youtu.be/jVp1zlcsrOY)

<div>&nbsp;</div>

<!-- Contact -->
## :handshake: Contact 

▶ Support me on Patreon [patreon.com/cifertech](https://www.patreon.com/cifertech)

CiferTech - [@twitter](https://twitter.com/techcifer) - CiferTech@gmali.com

Project Link: [https://github.com/cifertech/ESP32-DIV](https://github.com/cifertech/ESP32-DIV)

<div>&nbsp;</div>

<!-- Support & Contributions -->
## 💬 Support & Contributions

- 💬 Found a bug or have a feature request? Open an [Issue](https://github.com/cifertech/ESP32-DIV/issues)
- ⭐ Like the project? Star the repo!
- 🛠 Want to contribute? Fork it and submit a pull request.
 
 
