# MatrixPortal S3 - NFL Clock & Weather Display

![Arduino](https://img.shields.io/badge/Arduino-00979D?style=for-the-badge&logo=Arduino&logoColor=white)
![ESP32](https://img.shields.io/badge/ESP32-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![License](https://img.shields.io/github/license/markstamp/MatrixPortal-NFL-Clock?style=for-the-badge)

A smart LED matrix display for the Adafruit MatrixPortal S3 (ESP32-S3) showing a clock, weather conditions, live NFL scores with touchdown animations, and 2026 Winter Olympics medal counts.

## Features

- **Clock Display** - 12-hour format with AM/PM indicator
- **Weather** - Temperature and conditions from OpenWeatherMap with icons (sun, clouds, rain)
- **Live NFL Scores** - Real-time scores from ESPN with team colors for all 32 teams
- **Touchdown Animation** - 5-second celebration with team colors and blinking effect when a touchdown is detected
- **Olympics Medals** - USA gold, silver, and bronze counts for the 2026 Milano Cortina Winter Olympics
- **Game Prioritization** - Live games shown for 55s, upcoming for 15s, final for 5s
- **WiFi Setup Portal** - Customer-friendly captive portal for first-time configuration
- **OTA Updates** - Automatic firmware updates from GitHub releases (checks every 6 hours)

## Hardware

- Adafruit MatrixPortal S3
- 64x32 RGB LED Matrix (or compatible)
- USB-C power supply

## Setup

1. Flash the firmware via Arduino IDE or the prebuilt binary in `build/`
2. On first boot, connect to the `MatrixClock-Setup` WiFi network
3. Open the captive portal and configure:
   - WiFi credentials
   - City and country for weather
   - Timezone (PST/MST/CST/EST)
   - OpenWeatherMap API key (optional, for weather display)
4. The device will restart and connect to your WiFi

### Buttons

- **GPIO 0** (boot) - Hold on startup to clear all settings and re-enter setup mode
- **GPIO 3** - Press to manually trigger a firmware update check

## Dependencies

- [Adafruit Protomatter](https://github.com/adafruit/Adafruit_Protomatter)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
- ESP32 Arduino Core (WiFi, WebServer, DNSServer, HTTPClient, Preferences, Update, HTTPUpdate)

## APIs Used

- **OpenWeatherMap** - Weather data (`https://api.openweathermap.org`)
- **ESPN** - NFL scoreboard (`https://site.api.espn.com`)
- **whereig.com** - Olympics medal table (`https://www.whereig.com`)
- **GitHub Releases** - OTA firmware updates
- **NTP** - Time synchronization (`pool.ntp.org`)

## Changelog

### v1.5.0 - Olympics Medal Display

- Added new display mode showing USA medal counts (Gold, Silver, Bronze, Total) for the 2026 Milano Cortina Winter Olympics
- Medal data fetched from whereig.com every 10 minutes
- Display rotation updated: Clock/Weather -> Olympics Medals (10s) -> NFL Scores -> repeat
- Bumped firmware version to 1.5.0

### v1.4.0 - Code Review Fixes

**Bug Fixes:**
- Fixed weather change detection off-by-one tracking bug where `lastTemperature` was set to the previous value instead of the newly fetched value, causing incorrect change detection
- Fixed touchdown detection failing on 0-0 games — initialization check previously relied on scores being zero, now uses an explicit `initialized` flag
- Fixed timezone adjustment for upcoming game times not handling day rollover (e.g., 1AM UTC with timezone -5 would show the wrong date)

**Security:**
- Switched OpenWeatherMap and ESPN API calls from HTTP to HTTPS to protect API keys in transit

**Reliability:**
- Added WiFi auto-reconnection logic so the device recovers from network drops without requiring a power cycle
- HTTP response validation now checks for status 200 instead of any positive status code (previously 404/500 responses would be parsed as valid data)

**Performance:**
- Use `NFLGame&` reference instead of full struct copy in the display loop (avoids 4 String deep-copies per cycle)
- Pass `const String&` in `getTeamPrimaryColor`, `getTeamSecondaryColor`, and `drawTeamBox` to avoid unnecessary String copies

**Cleanup:**
- Removed duplicate `#include <Preferences.h>`
- Removed leftover development comments ("ADD THIS LINE", "THIS LINE MUST BE HERE!")

## License

MIT License - see [LICENSE](LICENSE) for details.
