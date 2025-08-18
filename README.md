
# 📄 ESP8266 D1 Mini – Door Opener System with Telegram Bot and OLED

## Index

- [Description](#description)
- [Main Features](#main-features)
- [Hardware Requirements](#hardware-requirements)
- [Wiring Diagram (for D1 mini)](#wiring-diagram-for-d1-mini)
- [Required Libraries Installation (via Library Manager)](#required-libraries-installation-via-library-manager)
- [Initial Telegram Setup](#initial-telegram-setup)
- [Circuit Assembly](#circuit-assembly)
- [First Boot Configuration](#first-boot-configuration)
- [Main Features - Details](#main-features---details)
- [Available Commands](#available-commands)
- [Security](#security)
- [Operation Logging (Log)](#operation-logging-log)
- [Sound Feedback](#sound-feedback)
- [Output Examples](#output-examples)
- [Updates and Customization](#updates-and-customization)
- [Reset/Recovery](#resetrecovery)
- [License](#license)
- [Credits](#credits)


## Description

📋

This project transforms an **ESP8266 D1 mini** into a remote control system for a **doorphone/door opener**, operable both via a **physical button** and a **Telegram bot**. The system also manages **staircase lighting**, **displays status/logs on an OLED screen**, and provides an **integrated web page** for WiFi, bot token, and authorized users configuration.

## Main Features

📋

- **Door opening** via physical button or Telegram command
- **Staircase light control** via Telegram command
- **OLED Display** (0.92" SSD1306 128x64) showing status and recent operations
- **Captive portal web page** for configuring WiFi and bot token
- **Authorized user management** through Telegram chat IDs
- **Logging of last 5 operations**
- **Access control** via web with password protection


## Hardware Requirements

🔧


| Component | Description |
| :-- | :-- |
| ESP8266 D1 Mini | Main microcontroller |
| Door Relay | 5V |
| Light Relay | 5V |
| Status LED | Blue LED integrated on `D4 (GPIO2)` |
| Buzzer | Optional |
| OLED SSD1306 Display | Connected via I2C to `D1 (GPIO5)` and `D2 (GPIO4)` |
| Door Button | Normally Open (NO) |
| Config Reset Button | Normally Open (NO) |
| Power Supply | 3.3V or 5V depending on components |

## Wiring Diagram (for D1 mini)

🔧


| D1 mini Pin | Connection | Description |
| :-- | :-- | :-- |
| D5 (GPIO14) | Door Relay | Door opening activation |
| D8 (GPIO15) | Light Relay | Staircase light activation |
| D4 (GPIO2) | Status LED | Onboard LED on D1 mini |
| D7 (GPIO13) | Buzzer (optional) | Acoustic signal |
| D1 (GPIO5) | OLED Display SCL | I2C communication |
| D2 (GPIO4) | OLED Display SDA | I2C communication |
| D3 (GPIO0) | Door Button | NO between GPIO0 and GND |
| D0 (GPIO16) | Config Button | NO between GPIO16 and GND (config mode) |
| 3.3V/5V | Relay VCC | Relay power supply |
| 3.3V | OLED VCC | Display power supply |
| GND | Ground | Common ground |

## Required Libraries Installation (via Library Manager)

📦

Make sure to install the following libraries before uploading the code:

- [UniversalTelegramBot](https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot) by Brian Lough
- [ArduinoJson](https://arduinojson.org/) by Benoit Blanchon (version 6.x)
- [Adafruit_GFX](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit_SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- [ESP8266WiFi](https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/readme.html)
- [ESP8266WebServer](https://arduino-esp8266.readthedocs.io/en/latest/esp8266webserver/readme.html)
- [DNSServer](https://github.com/zaklaus/ESPAsyncUDP/tree/master/src/DNSServer)


## Initial Telegram Setup

⚙️

1. **Create the Telegram bot**
    - Message [@BotFather](https://telegram.me/BotFather) on Telegram
    - Use `/newbot` and follow instructions
    - Copy the provided token
2. **Find chat IDs**
    - Send the bot to users who will use it
    - Each user must send you their ID:
        - To find it, they need to go to @MyIDBot on Telegram and send `/getid` in the chat to get their ID.
        - Copy all IDs

## Circuit Assembly

⚙️

The circuit can be assembled on a breadboard following the electronic schematic below. This way, before making the printed circuit board, you have the possibility to test it.

<img src="https://github.com/FyaSKoBoVe/apriporta/blob/main/img/schema01_1.png" width="1005" height="auto"/>

<br>


## First Boot Configuration

⚙️

1. On first boot (or if a WiFi error occurs), the device creates an AP network named "**ESP_Config**".
2. Connect via PC/smartphone and open **192.168.4.1**.
3. **Web Configuration**
    - The page requires a password to access settings (default: **admin**).
    - Web page allows entering:
        - WiFi SSID and Password
        - Telegram Bot Token
        - Authorized user Chat IDs (comma-separated, e.g., `12345678,87654321`)
        - Password to access configuration
    - Option to **reset the device**
4. Save and reboot (automatic).

On subsequent boots:

- The program reads data saved in **EEPROM**.
- If no valid data or connection error:
    - It enters **AP mode** creating WiFi "**ESP_Config**".
    - Starts a web server on `http://192.168.4.1` for initial setup.


## Main Features - Details

🌐

- **Remote control via Telegram**
    - `/open` – Open the door
    - `/light` – Turn on the staircase light
    - `/status` – Show current system status
    - `/log` – Display recent operations log
    - `/help` – Full command guide
    - `/menu` – Interactive menu with inline buttons
- **Security**
    - Only authorized users can execute commands
    - All operations are logged
- **OLED Display**
    - Shows WiFi and Telegram status
    - Last 5 logged events
    - Real-time system title and status


## Available Commands

🎮

### Text Commands

| Command | Action |
| :-- | :-- |
| `/start` | Welcome message |
| `/open` | Opens the door |
| `/light` | Turns on the staircase light |
| `/status` | Shows full system status |
| `/log` | Shows recent logged operations |
| `/help` | Displays full guidance |
| `/menu` | Shows interactive menu with buttons |

### Telegram Inline Buttons

The interactive menu allows control with a few taps:

- **🚪 Open Door**
- **💡 Turn On Light**
- **ℹ️ System Status**
- **📋 Show Log**
- **❓ Help**


## Security

🔒

- The system **blocks command access** to all except authorized chat IDs.
- Every action is logged along with the user’s name.
- The web configuration page is password protected (default: `admin`).


## Operation Logging (Log)

📈

The system tracks the last **5 operations**:

- Type of operation (`DOOR_OPENED`, `LIGHT_ON`, etc.)
- User name
- Relative timestamp (e.g., "3m ago")

Displayed both on the OLED and sent via Telegram when using `/log`.

## Sound Feedback

🔊

Optionally, the system can emit sound signals:

- **Single beep**: confirms command execution
- **Startup melody**: system powered on and ready
- **Error signal**: access denied or wrong password


## Output Examples

### OLED Display

```
D1 Mini Door Opener
Door Button 3m
UserX Light 1h
UserY Door 5h
WiFi:OK TG:OK
```


### Telegram Log

```
📋 Operation Log
• 🚪 DOOR OPENED
  👤 UserX
  ⏰ 3 minutes ago
• 💡 LIGHT ON
  👤 UserY
  ⏰ 1 hour ago
```


## Updates and Customization

🔄

You can customize:

- Door opening and light duration (`PORTA_TIMEOUT`, `LUCE_TIMEOUT`)
- Sound melodies and feedback
- Text size and position on the display
- Web interface (HTML/CSS)

*ONLY BY MODIFYING THE PROGRAM*

## Reset/Recovery

⚙️

Pressing the **reset button** (**GPIO16**) for more than 5 seconds:

- The system re-enters configuration mode (AP).
- Displays a "Password" prompt.
- The same page allows **resetting EEPROM** to factory defaults ("Reset EEPROM").
- After entering the password and pressing "Login"
- You can reset SSID, token, or user list from the browser.
- Password change is also accessible from here.


## License

This project is released under the open source [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) license. You can use, modify, and share it freely.

## Credits

Created for ESP8266 D1 mini – Door Opener project [ver. 01].

### Autore

[FyaSKoBoVe](https://github.com/FyaSKoBoVe/)

### Year

Firenze, luglio 2025


