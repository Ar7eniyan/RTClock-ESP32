[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]
[![GNU GPL License][license-shield]][license-url]

<div align="center">
  <h3 align="center">RTClock-ESP32</h3>

  <p align="center">
    <p>An open source alarm clock on ESP32 with control from REST API (Android app is in development)</p>
    <!-- <a href="#getting-started">Compile the code</a>
    ·
    <a href="https://github.com/Ar7eniyan/RTClock-ESP32/issues">Report Bug</a>
    ·
    <a href="https://github.com/Ar7eniyan/RTClock-ESP32/issues">Request Feature</a> -->
  </p>
</div>


## About The Project

I've made this alarm clock on ESP32 controller as my hobby project. It has REST API for controlling alarms and changing settings. Currently I'm developing Android application for the project that copies the functionality of Android stock alarms app, but the backend is ESP32-based clock. 

The following hardware is used for this project:
 * ESP32 (main microcontroller)
 * MAX98357A (sound amplifier)
 * HT16K33-based LED display
 * DS3231 (real-time clock module, has 2 alarm slots)
 * SPI SD card reader (is used to store alarm ringtones)

The main features are:
 * You can add as many alarms as you want
 * SSH tunelling for API is supported (if you don't have public IP address)
 * NTP protocol is used for time synchronization.

### Built With

* [Platformio](https://platformio.org)
* [VS Code](https://code.visualstudio.com/)


## Getting Started

### Prerequisites

Platrormio framework is used to build the project, so you need to get Platformio Core:
  ```sh
  python3 -c "$(curl -fsSL https://raw.githubusercontent.com/platformio/platformio/master/scripts/get-platformio.py)"
  ```
  (More on https://platformio.org)

### Compiling

1. Prepare SSH server with public IP for API, server should have port forwardung turned on.
   Set ```AllowTcpForwarding``` to ```all``` in ```sshd_config```
   (More in ```man sshd_config```)

3. Clone the repo
   ```sh
   git clone https://github.com/Ar7eniyan/RTClock-ESP32.git
   ```

2. Copy your SSH private key to src/keys/server.key

4. Create your build configuration based on private_config.template.ini:
   * Copy private_config.template.ini to private_config.ini
   * Change build configuration as you want (WiFi and SSH settings, logging options, etc.)

5. Build firmware from sources and upload it to ESP32:
   * Build:
      ```sh
      pio run -e build
      ```

   * Upload:
      ```sh
      pio run -e esptool_upload
      ```
      or
      ```sh
      pio run -e jlink_upload
      ```

## Roadmap

 - [ ] Make alarms presistent across reboots (save them on SD or NVRAM)
 - [ ] Let user choose custom alarm ringtones
    - [ ] Add API endpoint for uploading ringtones to SD
 - [ ] Add circuit scheme to README

## License

Distributed under the GNU GPL License. See `LICENSE.txt` for more information.

## Credits

Used libraries:
 * [Mongoose](https://github.com/cesanta/mongoose)
 * [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
 * [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S.git)
 * [HT16K33](https://github.com/Ar7eniyan/HT16K33.git) (my fork)
 * [LibSSH-ESP32](https://github.com/ewpa/LibSSH-ESP32)
 * [NTPClient](https://github.com/Ar7eniyan/NTPClient) (my fork)
 * [RTClib](https://github.com/adafruit/RTClib)
 * [SdFat](https://github.com/greiman/SdFat)

Thanks to:
 * [@othneildrew](https://github.com/othneildrew) for [readme template](https://github.com/othneildrew/Best-README-Template)


[contributors-shield]: https://img.shields.io/github/contributors/Ar7eniyan/RTClock-ESP32.svg?style=for-the-badge
[contributors-url]: https://github.com/Ar7eniyan/RTClock-ESP32/graphs/contributors
[forks-shield]: https://img.shields.io/github/forks/Ar7eniyan/RTClock-ESP32.svg?style=for-the-badge
[forks-url]: https://github.com/Ar7eniyan/RTClock-ESP32/network/members
[stars-shield]: https://img.shields.io/github/stars/Ar7eniyan/RTClock-ESP32.svg?style=for-the-badge
[stars-url]: https://github.com/Ar7eniyan/RTClock-ESP32/stargazers
[issues-shield]: https://img.shields.io/github/issues/Ar7eniyan/Rtclock-ESP32.svg?style=for-the-badge
[issues-url]: https://github.com/Ar7eniyan/RTClock-ESP32/issues
[license-shield]: https://img.shields.io/github/license/Ar7eniyan/RTClock-ESP32.svg?style=for-the-badge
[license-url]: https://github.com/Ar7eniyan/RTClock-ESP32/blob/master/LICENSE.txt
