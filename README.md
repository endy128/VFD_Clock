# VFD Clock from AliExpress (Enhanced ESP12 Firmware)

![VFD CLock from AliExpress](image.jpg)

A complete firmware overhaul for the popular USB-powered VFD clocks found on AliExpress. This project transforms a basic clock into a highly optimized, internet-synced timepiece with custom mechanical-style animations and advanced power management.

## Hardware Info
- **VFD Model:** `8-MD-06INKM` (8-digit, 5x7 matrix characters)
- **Microcontroller:** ESP-12F (ESP8266) or ESP-12H (ESP8285), depending on the clock (see below)
- **Interface:** 3-wire Serial (SPI-like)
- **Power:** 5V via USB-C/Micro-USB

### Clock Versions

There are (at least) two versions of this clock. They look the same from the outside but have different flash sizes, so each needs its own build:

|Module on the board|Chip|Flash|PlatformIO environment|
|-------------------|----|-----|----------------------|
|ESP-12F|ESP8266|4MB|`esp12e` (default)|
|ESP-12H|ESP8285 (ESP8285MOD)|1MB|`esp12h`|

The module name is printed on the module's metal shield. If you're not sure which one you have, connect the clock (see [Flashing](#flashing)) and ask the chip:

```bash
esptool.py --port /dev/ttyUSB0 flash_id
# "Detected flash size: 4MB" -> esp12e
# "Detected flash size: 1MB" -> esp12h
```

Flashing the 4MB build onto a 1MB clock will leave it unable to boot or unable to save settings.


## Hardware Reversing

### Backup Original Firmware
Before flashing custom code, it is **highly recommended** to back up the factory "mystery" firmware in case you want to revert or analyze the original logic.

Connect the clock as described in [Flashing](#flashing), then use `esptool.py` to read the whole flash. The size depends on which [clock version](#clock-versions) you have:
```bash
# Identify your port first (e.g., COM3 or /dev/ttyUSB0)

# 4MB clock (ESP-12F)
esptool.py --port /dev/ttyUSB0 --baud 460800 read_flash 0 0x400000 backup_firmware.bin

# 1MB clock (ESP-12H / ESP8285)
esptool.py --port /dev/ttyUSB0 --baud 460800 read_flash 0 0x100000 backup_firmware.bin

# To write it back
# esptool.py --port /dev/ttyUSB0 --baud 460800 write_flash 0 backup_firmware.bin
```


### Pinout (ESP12 to VFD)

Through manual probing with a multimeter, the following pin mapping was identified for the display and input:

|ESP12 Pin|Function|Description|
|---------|--------|-----------|
|GPIO 13  |`CS`    |Chip Select|
|GPIO 12  |`CLK`   |Serial Clock|
|GPIO 14  |`DATA`  |Serial Data|
|GPIO 0   |`BUTTON`|Multi-function Input (Active Low)|
|GPIO 2   |`LED`   |Built-in Blue LED (Sync Indicator)|


### Protocol Analysis (PulseView)

To understand the factory data packets, a cheap 8-channel USB logic analyser clone was used with PulseView. Most of these clones work with PulseView's `fx2lafw` driver.

- **Sample Rate**: At least **2 MHz** (The VFD clock usually runs around 100-500kHz, so 2MHz provides plenty of headroom).
- **Decoder**: Use the **SPI** decoder.
- **Settings**: * CPOL: 0, CPHA: 0 (Mode 0)
    - **Bit order**: LSB first
    - **Word size**: 8-bit


## PlatformIO Project Capabilities
This firmware isn't just a basic clock; it’s a fully non-blocking "Stealth" OS for your desk.

1. Custom Animation Engine (The "Framebuffer")
Unlike the factory firmware, we utilize the VFD's 8 CGRAM slots as a hardware framebuffer. This allows for:
- **Mechanical Drop-Down**: Digits slide down vertically, mimicking a physical split-flap display.
- **Checkerboard Dissolve**: A sci-fi "shatter" effect where pixels dither between old and new digits.
- **Bidirectional Logic**: The time slides **down** to show the date, and the date slides **up** to return to the time.

2. "Stealth Sync" Technology
To solve the common problem of ESP8266 heat affecting the clock's accuracy:
- **Radio Silence**: WiFi connects on boot to get NTP time, then **powers off completely**.
- **Daily Maintenance**: Once during the 2 AM hour, the radio silently wakes up, re-syncs with an atomic clock, and shuts down again (the clock keeps running during the sync).
- **Power-Cut Recovery**: If WiFi isn't available at boot (e.g. the router is still starting up after a power cut), the clock retries every 60 seconds until it gets the time, with no need to power cycle it.
- **Heat Reduction**: Keeping the RF radio off 99% of the time keeps the VFD driver and ESP chip cool, extending hardware life.

3. Integrated UI & Menu System
Accessed via a single button (Short press for Date, Long press for Menu):
- **Captive Portal**: Easy WiFi setup via smartphone if no credentials are found.
- **12/24H Toggle**: Seamlessly switch between military and civilian time.
- **DST Toggle**: One-click Daylight Saving adjustment.
- **8-Level Dimmer**: Sub-menu with **Live Preview** to dial in the perfect brightness.
- **Date Peek**: A 3-second temporary date display that resets automatically.

## Installation

1. Open this folder in VS Code with the PlatformIO extension installed.

2. Ensure you have the following libraries (auto-managed by `platformio.ini`):
- `AyresWiFiManager`
- `ArduinoJson`

3. Select the PlatformIO environment for your [clock version](#clock-versions) (`esp12e` or `esp12h`) using the environment picker in the VS Code status bar. Clicking Upload without changing it always builds `esp12e`.

4. Connect the clock and put it into flash mode (see [Flashing](#flashing)).

5. Upload the firmware (**Upload**), then reset the clock into flash mode again and upload the WiFi portal web pages from the `data` folder (**Upload Filesystem Image**). Without the second step the clock works, but the WiFi setup page won't load.

From the command line, the 1MB clock for example:
```bash
pio run -e esp12h -t upload
pio run -e esp12h -t uploadfs
```

### Flashing

The clock has a row of programming pads on the board labelled `GND RST TXD RXD O0`. A pogo pin clamp on these pads, wired to a **3.3V** USB-UART adapter, is enough to flash it without soldering.

|Clock pad|Connect to|
|---------|----------|
|`GND`|Adapter `GND`|
|`TXD`|Adapter `RX`|
|`RXD`|Adapter `TX`|
|`O0` (GPIO 0)|`GND` (a wire on the pogo connector)|
|`RST`|Not connected (touch to `GND` to reset)|

- **Power:** the adapter can't power the clock because the display draws too much, so plug the clock into USB power as well. Don't connect the adapter's 3.3V/5V pin.
- **Flash mode:** with `O0` grounded, reset the clock (briefly touch `RST` to `GND`, or unplug and replug its USB power). It starts in the bootloader with the display blank, ready to flash.
- **Running normally:** remove the `O0` ground wire (or the whole clamp) and reset the clock again.

### Note

If the clock boots to `SET WIFI`, connect to the VFD-Clock access point on your phone to enter your local network credentials. The clock will remember these and skip the portal on the next boot!

### Startup Messages

Until the clock has the time, it shows its WiFi status:

|Display   |Meaning|
|----------|-------|
|`WIFI...` |Connecting to your WiFi network|
|`SYNCING` |Connected, waiting for the time from the internet|
|`RETRY 1`, `RETRY 2`|The last attempt failed; trying again in 60 seconds|
|`NO WIFI` |3 attempts have failed. The clock keeps retrying every 60 seconds in the background and shows the time as soon as it succeeds|
|`SET WIFI`|No WiFi details saved; the setup portal is open (see below)|

While one of these messages is showing, a **short press** of the button starts a new attempt straight away.

If no WiFi details are saved, the setup portal closes after 5 minutes with no activity and the clock shows `NO WIFI`. Power cycle the clock to open the portal again.

The retry timings are constants at the top of `src/main.cpp` (`RETRY_INTERVAL_MS`, `FAILS_BEFORE_ERROR`, etc.).

### WiFi Portal Credentials

- SSID: `VFD-Clock`
- Pass: `12345678`


## Acknowledgements

- [sfxfs/FUTABA-VFD-8-MD-06INKM](https://github.com/sfxfs/FUTABA-VFD-8-MD-06INKM)


