# FermBuddy
**Track more than Gravity**

FermBuddy is an open-source display for Hydrometers. (Support for Tilt Hydrometers only at the moment)
It connects directly to your fermentation monitoring devices and displays gravity, temperature, attenuation and estimated ABV on a dedicated display.

## License

The source code is licensed under the GNU General Public License (GPL).

The name **FermBuddy**, its logo and other branding elements are not covered by this license and may not be used without permission.

## Project Status

> ⚠️ FermBuddy is currently under active development.
> Features, hardware and documentation may change before the first stable release.

## Hardware

- LilyGO T-Display S3
  https://lilygo.cc/products/t-display-s3

## Software Requirements

- Arduino IDE 2.x
- ESP32 Arduino Core 3.x or newer

## Required Libraries

- LittleFS
- TFT_eSPI
- ArduinoJson
- NimBLE-Arduino
- ESPAsyncWebServer
- DNSServer
- PNGdec

See the `#include` statements in `FermBuddy.ino` for the complete list of dependencies.
