# Wiring and bring-up

Pin assignments below match the `PIN DEFINITIONS` block in `firmware.ino`. That file
is the source of truth.

## MFRC522 RFID reader — SPI

| RC522 | ESP32 GPIO |
|---|---|
| SDA (SS) | 5 |
| RST | 4 |
| SCK | 18 |
| MOSI | 23 |
| MISO | 19 |
| 3.3V | 3.3V |
| GND | GND |

The RC522 is 3.3 V only. Connecting it to 5 V will damage it. SCK, MOSI and MISO are
the ESP32's hardware SPI pins and are not configurable in this firmware.

## SSD1306 OLED — I2C

| OLED | ESP32 GPIO |
|---|---|
| SDA | 21 |
| SCL | 22 |
| VCC | 3.3V |
| GND | GND |

Address is `0x3C` (`OLED_ADDR` in the firmware). Some modules ship at `0x3D`; if the
display stays blank with no I2C error, change that constant before assuming a wiring
fault.

## Servo

| Servo | Connection |
|---|---|
| Signal (orange) | GPIO 2 |
| VCC (red) | External 5–6 V supply |
| GND (brown) | Supply GND, tied to ESP32 GND |

The servo must not be powered from the ESP32's USB rail. Stall current on a metal-gear
servo exceeds what the onboard regulator can deliver, and the symptom is the ESP32
browning out and rebooting mid-actuation rather than an obvious power fault. Use a
separate supply and tie the grounds together.

GPIO 2 is also the onboard LED pin, so the LED flickers during servo PWM. Harmless,
but it looks like a fault if you are not expecting it.

## 4x4 matrix keypad

| Keypad pin | ESP32 GPIO | Role |
|---|---|---|
| 1 | 13 | Row 1 |
| 2 | 12 | Row 2 |
| 3 | 14 | Row 3 |
| 4 | 27 | Row 4 |
| 5 | 26 | Column 1 |
| 6 | 25 | Column 2 |
| 7 | 33 | Column 3 |
| 8 | 32 | Column 4 |

Keypads vary in whether pins 1–4 are rows or columns. If keys register as the wrong
character in a transposed pattern, swap `rowPins` and `colPins` in the firmware
rather than rewiring.

## Bring-up order

Bring the peripherals up one at a time. Each has a different failure mode and
debugging them together is slower than doing them in sequence.

1. **OLED first.** It is the only output device, so everything after it is easier to
   diagnose once the display works.
2. **Keypad.** Print characters to serial before wiring them into the PIN logic.
3. **RFID.** Confirm the reader returns a UID on a card tap before registering
   anything through the dashboard.
4. **Servo last**, on its external supply, so a brownout cannot corrupt the earlier
   steps.
5. **WiFi and WebSocket** once the hardware is known good, so connection failures are
   unambiguously network problems.

## Configuration

`WIFI_SSID`, `WIFI_PASSWORD`, `SERVER_HOST` and `DEVICE_TOKEN` are at the top of
`firmware.ino`. `SERVER_HOST` is the LAN IP of the machine running the server; the
ESP32 and the server must be on the same network.

Do not commit real credentials. The placeholders exist so that the working values
stay local.