# Safe Safe - Multi-Factor IoT Smart Lock

An electronic lock that requires three independent factors before the bolt moves:
a PIN typed on a keypad, an RFID badge, and an explicit confirmation from a phone
browser. Built in 36 hours at Hack(H)er413, where it placed 3rd in the Hardware
category.

| | |
|---|---|
| Controller | ESP32 DevKit, C++/Arduino |
| Server | Node.js, Express, Socket.IO, SQLite |
| Factors | Keypad PIN, RFID badge (MFRC522), browser confirmation |
| Peripherals | MFRC522 over SPI, SSD1306 OLED over I2C, 4x4 keypad, servo |
| Trust model | Server holds all credentials; firmware holds only a device token |
| Status | Working demo. Not production security — see [Known limitations](#known-limitations) |

**Contents** —
[Architecture](#architecture) ·
[Why the server mediates](#why-the-server-mediates) ·
[Authentication flow](#authentication-flow) ·
[Hardware](#hardware) ·
[Setup](#setup) ·
[Usage](#usage) ·
[Known limitations](#known-limitations) ·
[Repository layout](#repository-layout)

## Architecture

```
  Phone browser  ──HTTP + Socket.IO──▶  Node.js server  ◀──Socket.IO──  ESP32 lock
   (dashboard)                          Express, SQLite                  (firmware)
                                        bcrypt, JWT
```

The ESP32 opens a WebSocket to the server on boot and authenticates with a device
token. The dashboard authenticates separately with a JWT. The server is the only
component that stores credentials and the only component that decides whether the
lock opens; the ESP32 reports sensor events and acts on a single `unlock` command.

PINs are stored bcrypt-hashed. RFID tag UIDs and the access log live in SQLite
alongside the user and device records.

## Why the server mediates

The first version used BLE proximity as the third factor. The ESP32 scanned for the
owner's phone every ten seconds and treated the advertised MAC address as identity.

This does not work. iOS and Android both randomize BLE MAC addresses on a rotating
interval, specifically to prevent passive tracking. The same phone presents a
different address on successive scans, so device identity is not stable and the
factor either fails constantly or has to be disabled.

The available workarounds all meant defeating a privacy feature in order to build a
security feature — pinning a static address, or pairing and persisting a bond that
the OS is designed to rotate away from. Instead the third factor became an explicit
confirmation in a browser dashboard, with the server as the only holder of identity.

That change turned out to be the better architecture for a second reason. In the BLE
design the ESP32 evaluated factors locally and therefore had to hold credentials.
After the rewrite the trust boundary collapsed to one place: the firmware knows a
device token and nothing else, and every credential comparison happens server-side.

## Authentication flow

All three factors must pass within a single unlock window.

| Factor | Path | Verified by | On failure |
|---|---|---|---|
| RFID badge | MFRC522 reads UID over SPI, ESP32 emits `rfid_scanned` | Server compares UID against registered tags | Attempt logged, OLED shows denial, no state change |
| Keypad PIN | ESP32 collects digits, submitted with `#` | bcrypt hash comparison | Entry buffer cleared, attempt logged |
| Dashboard | User taps Unlock in the browser | JWT session on the server | Unlock window expires, servo never actuates |

The servo actuates only on an `unlock` command issued by the server after all three
have passed. No single factor can drive it directly, and the ESP32 has no local
policy that can be satisfied on its own.

The lock re-locks automatically after 5 seconds (`AUTO_LOCK_TIMEOUT`), or immediately
if `*` is pressed while unlocked.

## Hardware

- ESP32 DevKit
- MFRC522 RFID reader (SPI, 3.3 V only)
- SSD1306 OLED, 128x64, I2C at 0x3C
- 4x4 matrix keypad
- Servo, external 5–6 V supply with common ground

### Pin map

Taken from `firmware/firmware.ino`. That file is the source of truth; if this table
and the code ever disagree, the code is right.

| Signal | ESP32 GPIO |
|---|---|
| RFID SS | 5 |
| RFID RST | 4 |
| RFID SCK / MOSI / MISO | 18 / 23 / 19 (hardware SPI) |
| OLED SDA / SCL | 21 / 22 (hardware I2C) |
| Servo signal | 2 |
| Keypad rows 1–4 | 13, 12, 14, 27 |
| Keypad columns 1–4 | 26, 25, 33, 32 |

Two cautions that cost real debugging time: the MFRC522 is 3.3 V only and will be
damaged by 5 V, and the servo draws more current than the ESP32's USB rail can
supply — it needs its own 5–6 V source with grounds tied together.

## Setup

### Server

```bash
cd web
npm install
npm start        # or: npm run dev  for auto-reload
```

Serves on `http://localhost:3000`. Register an account; the response includes a
device token to flash into the firmware.

`better-sqlite3` and `bcrypt` are native modules. On Windows they need Visual Studio
Build Tools with the "Desktop development with C++" workload, or a Node version with
prebuilt binaries available. Developed against Node 18.

### Firmware

1. Arduino IDE with ESP32 board support installed.
2. Libraries: `Adafruit SSD1306`, `Adafruit GFX`, `MFRC522`, `Keypad`, `ESP32Servo`,
   `ArduinoJson`, `WebSockets` (Markus Sattler).
3. Edit the configuration block at the top of `firmware/firmware.ino`:

```cpp
const char* WIFI_SSID      = "your_network";
const char* WIFI_PASSWORD  = "your_password";
const char* SERVER_HOST    = "your_server_ip";
const char* DEVICE_TOKEN   = "token_from_dashboard";
```

4. Board: `ESP32 Dev Module`. Partition scheme: `Huge APP (3MB No OTA/1MB SPIFFS)`.
5. Upload.

The server and the ESP32 must be on the same network — `SERVER_HOST` is a LAN
address, not a hostname.

## Usage

1. Start the server, register, copy the device token, flash the ESP32.
2. Set a PIN: dashboard → Change PIN → type on the keypad → confirm.
3. Register a badge: dashboard → Scan New Card → tap the card on the reader.
4. Unlock: scan the badge, enter the PIN, tap Unlock on the phone.

The OLED shows `LOCKED` at idle. Press `#` to begin PIN entry, digits, then `#` to
submit. Press `*` while unlocked to re-lock immediately.

## Known limitations

Built in 36 hours for a demo. These are known, not overlooked:

- **No transport encryption.** PINs are bcrypt-hashed at rest but the Socket.IO
  connection is plain `ws://`. Anyone on the same network can read the traffic.
  TLS is the first thing that should change.
- **No rate limiting or lockout.** PIN attempts are unbounded, so a brute-force
  attempt against a short PIN is not prevented in software.
- **Device token is a compile-time constant.** Anyone with physical access to the
  ESP32 can read it out of flash and impersonate the lock to the server.
- **RFID UIDs are cloneable.** MFRC522 cards broadcast a readable UID with no
  challenge-response, so the badge is a possession factor, not a cryptographic one.
- **Single-user.** No account separation, no roles, and the audit trail is limited
  to the access log.
- **Dependencies are pinned to the hackathon versions** and carry known advisories.
  Not upgraded because the code has not been retested against newer majors.

## Repository layout

```
firmware/
  firmware.ino     ESP32 firmware: peripherals, state machine, WebSocket client
  WIRING.md        Pin map and bring-up order
web/
  server.js        Express + Socket.IO entry point
  db.js            SQLite schema and initialization
  middleware/      JWT verification
  routes/          Auth and device REST endpoints
  public/          Dashboard, single-file frontend
docs/
  API.md           REST endpoints and WebSocket events
```

Full endpoint and event reference: [docs/API.md](docs/API.md).

## Built with

C++/Arduino, Node.js, Express, Socket.IO, SQLite, bcrypt, JWT.