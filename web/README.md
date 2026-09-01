# 🔒 Smart Lock Server

Multi-factor authentication smart lock dashboard for hackathon.

## Quick Start

```bash
npm install
npm start        # production
npm run dev      # auto-reload on changes (Node 18+)
```

Server runs on `http://localhost:3000`

## Project Structure

```
smart-lock-server/
├── server.js              # Main entry: Express + Socket.IO setup
├── db.js                  # SQLite database init + schema
├── package.json
├── middleware/
│   └── auth.js            # JWT token verification middleware
├── routes/
│   ├── auth.js            # POST /api/auth/register, /login, GET /me
│   └── devices.js         # CRUD for devices, PINs, RFID, BLE
└── public/
    └── index.html         # Dashboard frontend (single file)
```

## API Endpoints

### Auth
- `POST /api/auth/register` — create account (returns JWT + device token)
- `POST /api/auth/login` — sign in (returns JWT)
- `GET /api/auth/me` — verify token

### Devices (all require JWT)
- `GET /api/devices` — list user's locks
- `POST /api/devices` — add a new lock
- `PATCH /api/devices/:id` — rename a lock

### PIN
- `GET /api/devices/:id/pin` — check PIN status
- `POST /api/devices/:id/pin` — send change-PIN command to ESP32

### RFID
- `GET /api/devices/:id/rfid` — list registered cards
- `POST /api/devices/:id/rfid` — send scan-RFID command to ESP32
- `DELETE /api/devices/:id/rfid/:tagId` — remove a card

### BLE
- `GET /api/devices/:id/ble` — list paired BLE devices
- `POST /api/devices/:id/ble` — add BLE MAC address `{ mac_address, label }`
- `DELETE /api/devices/:id/ble/:bleId` — remove a BLE device

### Log
- `GET /api/devices/:id/log` — recent access attempts

## WebSocket Events

### ESP32 → Server
- `esp_auth` `{ device_token }` — authenticate on connect
- `pin_changed` `{ new_pin }` — user typed new PIN on keypad
- `rfid_scanned` `{ tag_uid, label }` — new card scanned
- `access_attempt` `{ method, success, detail }` — log an unlock attempt

### Server → ESP32
- `auth_ok` `{ device_id, label }` — authentication successful
- `credentials_update` `{ rfid_tags[], ble_macs[], has_pin }` — push credentials
- `command` `{ action: "change_pin" | "scan_rfid" }` — user triggered from dashboard
- `pin_hash_update` `{ pin_hash }` — new PIN hash for local verification

### Server → Dashboard
- `device_status` `{ device_id, is_online }` — device connected/disconnected
- `pin_updated` `{ device_id }` — PIN was changed
- `rfid_added` `{ device_id, tag_uid, label }` — new card registered
- `access_attempt` `{ device_id, method, success, detail, timestamp }` — real-time log

## ESP32 Setup

1. Register on the dashboard → get a device token
2. Flash the token into your ESP32 firmware
3. ESP32 connects to `ws://YOUR_SERVER_IP:3000` on boot
4. Emits `esp_auth` with the token
5. Receives credentials and listens for commands

## For the Hackathon Demo

1. Run the server on a laptop connected to the same WiFi as the ESP32
2. Open `http://LAPTOP_IP:3000` on your phone for the dashboard
3. Register, get the device token, flash it to the ESP32
4. Demo: change PIN from dashboard → type on keypad → see confirmation
5. Demo: scan RFID → tap card → see it appear on dashboard
6. Demo: add BLE MAC → lock detects phone nearby → factor passes
