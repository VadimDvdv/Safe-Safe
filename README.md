# 🔒 Safe Safe

Multi-factor authentication smart lock built at **Hack(h)er413** hackathon.

Three factors required to unlock: PIN code (keypad) + RFID badge + dashboard confirmation (phone).

## How It Works

```
Phone Browser ←→ Node.js Server ←→ ESP32 Lock
                    (SQLite)
```

The server is the middleman. ESP32 connects via WebSocket on boot, the dashboard connects from your browser. All credentials are stored server-side with bcrypt-hashed PINs.

## Hardware

- ESP32 DevKit
- RFID-RC522
- SSD1306 OLED (128x64, I2C)
- 4x4 Matrix Keypad
- DF9GMS Servo

## Setup

### Server

```bash
cd smart-lock-server
npm install
npm start
```

Opens on `http://localhost:3000`. Register an account — you'll get a device token.

### Firmware

1. Install Arduino IDE with ESP32 board support
2. Install libraries: `Adafruit SSD1306`, `Adafruit GFX`, `MFRC522`, `Keypad`, `ESP32Servo`, `ArduinoJson`, `WebSockets` (by Markus Sattler)
3. Open `smart_lock.ino` and update the config at the top:

```cpp
const char* WIFI_SSID      = "your_wifi";
const char* WIFI_PASSWORD  = "your_password";
const char* SERVER_HOST    = "your_laptop_ip";
const char* DEVICE_TOKEN   = "token_from_dashboard";
```

4. Board: `ESP32 Dev Module`, Partition: `Huge APP (3MB No OTA/1MB SPIFFS)`
5. Upload

### Wiring

| Component | ESP32 Pins |
|-----------|-----------|
| RFID SDA/RST | GPIO 5 / GPIO 4 |
| RFID SPI | MOSI=23, MISO=19, SCK=18 |
| OLED SDA/SCL | GPIO 21 / GPIO 22 |
| Servo signal | GPIO 2 |
| Keypad R1-R4 | GPIO 13, 12, 14, 27 |
| Keypad C1-C4 | GPIO 26, 25, 33, 32 |

## Usage

1. Start server → register → copy device token → flash ESP32
2. Set a PIN: dashboard → "Change PIN" → type on keypad → confirm
3. Register RFID: dashboard → "Scan New Card" → tap card on reader
4. To unlock: scan RFID + enter PIN + tap Unlock on phone → all 3 factors pass → servo turns

## Built With

C++/Arduino, Node.js, Express, Socket.IO, SQLite, bcrypt, JWT
