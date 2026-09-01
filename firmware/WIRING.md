# Smart Lock ESP32 — Wiring Guide

## RFID-RC522 (SPI)
| RC522 Pin | ESP32 Pin |
|-----------|-----------|
| SDA (SS)  | GPIO 5    |
| SCK       | GPIO 18   |
| MOSI      | GPIO 23   |
| MISO      | GPIO 19   |
| RST       | GPIO 27   |
| 3.3V      | 3.3V      |
| GND       | GND       |

⚠️ RC522 is 3.3V only — do NOT connect to 5V

## HW-125 SD Card (SPI, shared bus)
| SD Pin | ESP32 Pin     |
|--------|---------------|
| CS     | GPIO 4        |
| SCK    | GPIO 18 (shared) |
| MOSI   | GPIO 23 (shared) |
| MISO   | GPIO 19 (shared) |
| VCC    | 5V            |
| GND    | GND           |

## OLED GMEI2864 (I2C)
| OLED Pin | ESP32 Pin |
|----------|-----------|
| SDA      | GPIO 21   |
| SCL      | GPIO 22   |
| VCC      | 3.3V      |
| GND      | GND       |

If nothing shows, try changing OLED_ADDR from 0x3C to 0x3D in the code.

## MG996R Servo
| Servo Wire | ESP32 Pin    |
|------------|--------------|
| Signal (orange) | GPIO 13 |
| VCC (red)  | External 5-6V power supply |
| GND (brown)| GND (shared with ESP32) |

⚠️ MG996R draws too much current for the ESP32's USB power.
Use a separate 5V power supply for the servo and connect GND together.

## 4x4 Matrix Keypad
| Keypad Pin | ESP32 Pin | Role |
|------------|-----------|------|
| Pin 1      | GPIO 32   | Row 1 |
| Pin 2      | GPIO 33   | Row 2 |
| Pin 3      | GPIO 25   | Row 3 |
| Pin 4      | GPIO 26   | Row 4 |
| Pin 5      | GPIO 14   | Col 1 |
| Pin 6      | GPIO 12   | Col 2 |
| Pin 7      | GPIO 15   | Col 3 |
| Pin 8      | GPIO 2    | Col 4 |

Note: GPIO 2 has the onboard LED — it works fine for keypad but will
flicker during boot. If this causes issues, swap to another free GPIO.

## How to Use

1. Open Arduino IDE
2. Install ESP32 board support (Board Manager → search "esp32")
3. Install libraries: Adafruit SSD1306, Adafruit GFX, MFRC522,
   Keypad, ESP32Servo, ArduinoJson, WebSockets (by Markus Sattler)
4. Change WIFI_SSID, WIFI_PASSWORD, SERVER_HOST, DEVICE_TOKEN in the code
5. Select board: "ESP32 Dev Module"
6. Upload

## User Flow

- IDLE screen shows "LOCKED" with status bar
- Press # to start entering PIN → type digits → press # to submit
- Tap RFID card anytime
- BLE scans automatically every 10 seconds
- Once all configured factors pass → servo unlocks → auto-locks after 5s
- Press * anytime while unlocked to re-lock immediately

## Dashboard Commands

- "Change PIN" → OLED shows prompt → type new PIN → press # → confirm → press #
- "Scan RFID" → OLED shows prompt → tap card → registered
- BLE MAC is added directly on the dashboard, pushed to ESP32 via WebSocket
