# API reference

The server holds all credentials and arbitrates unlock decisions. The ESP32 and the
dashboard are both clients; neither trusts the other directly. Setup instructions are
in the [root README](../README.md).

## Server layout

```
server.js           Express + Socket.IO setup, connection handling
db.js               SQLite schema and initialization
middleware/auth.js  JWT verification
routes/auth.js      POST /register, POST /login, GET /me
routes/devices.js   Device, PIN, RFID and access-log endpoints
public/index.html   Dashboard, single file
```

## Schema

Five tables in SQLite: `users`, `devices`, `pins`, `rfid_tags`, `access_log`. PINs
are stored as bcrypt hashes; RFID tags as UIDs with an optional label.

## REST API

### Auth

| Method | Path | Purpose |
|---|---|---|
| POST | `/api/auth/register` | Create an account. Returns a JWT and a device token |
| POST | `/api/auth/login` | Sign in. Returns a JWT |
| GET | `/api/auth/me` | Verify a token |

### Devices (all require a JWT)

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/devices` | List the user's locks |
| POST | `/api/devices` | Add a lock |
| PATCH | `/api/devices/:id` | Rename a lock |
| GET | `/api/devices/:id/pin` | PIN status |
| POST | `/api/devices/:id/pin` | Send a change-PIN command to the ESP32 |
| GET | `/api/devices/:id/rfid` | List registered cards |
| POST | `/api/devices/:id/rfid` | Send a scan-RFID command to the ESP32 |
| DELETE | `/api/devices/:id/rfid/:tagId` | Remove a card |
| GET | `/api/devices/:id/log` | Recent access attempts |

## WebSocket events

### ESP32 to server

| Event | Payload |
|---|---|
| `esp_auth` | `{ device_token }`, authenticate on connect |
| `pin_changed` | `{ new_pin }`, user entered a new PIN on the keypad |
| `rfid_scanned` | `{ tag_uid, label }`, card presented to the reader |
| `access_attempt` | `{ method, success, detail }`, log an unlock attempt |

### Server to ESP32

| Event | Payload |
|---|---|
| `auth_ok` | `{ device_id, label }` |
| `credentials_update` | `{ rfid_tags[], has_pin }` |
| `command` | `{ action: "change_pin" \| "scan_rfid" }` |
| `pin_hash_update` | `{ pin_hash }` |

### Server to dashboard

| Event | Payload |
|---|---|
| `device_status` | `{ device_id, is_online }` |
| `pin_updated` | `{ device_id }` |
| `rfid_added` | `{ device_id, tag_uid, label }` |
| `access_attempt` | `{ device_id, method, success, detail, timestamp }` |

## Device provisioning

1. Register on the dashboard and copy the device token.
2. Flash the token into `firmware/firmware.ino`.
3. The ESP32 connects to `ws://SERVER_HOST:3000` on boot and emits `esp_auth`.
4. On success the server replies `auth_ok` and pushes `credentials_update`.
5. The ESP32 then listens for `command` events raised from the dashboard.

Security caveats for this provisioning path are listed under Known limitations in
the [root README](../README.md).