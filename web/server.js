const express = require('express');
const http = require('http');
const path = require('path');
const { Server } = require('socket.io');
const bcrypt = require('bcrypt');
const db = require('./db');

const authRoutes = require('./routes/auth');
const deviceRoutes = require('./routes/devices');

const app = express();
const server = http.createServer(app);
const io = new Server(server, {
  cors: { origin: '*' },
  allowEIO3: true
});

// ─── MIDDLEWARE ────────────────────────────────────

app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// ─── API ROUTES ───────────────────────────────────

app.use('/api/auth', authRoutes);
app.use('/api/devices', deviceRoutes);

// ─── SOCKET.IO ────────────────────────────────────

const espSockets = new Map();
const dashSockets = new Map();

app.set('io', io);
app.set('espSockets', espSockets);

io.on('connection', (socket) => {
  console.log(`Socket connected: ${socket.id}`);

  // ── ESP32 authenticates with its device token ──
  socket.on('esp_auth', (data) => {
    const { device_token } = data;

    const device = db.prepare('SELECT * FROM devices WHERE device_token = ?').get(device_token);
    if (!device) {
      socket.emit('error', { message: 'Invalid device token' });
      socket.disconnect();
      return;
    }

    socket.deviceToken = device_token;
    socket.deviceId = device.id;
    socket.userId = device.user_id;
    espSockets.set(device_token, socket);

    db.prepare('UPDATE devices SET is_online = 1 WHERE id = ?').run(device.id);

    console.log(`ESP32 authenticated: ${device.label} (token: ${device_token.slice(0, 8)}...)`);
    socket.emit('auth_ok', { device_id: device.id, label: device.label });

    // Send current credentials to ESP32
    const rfidTags = db.prepare(
      'SELECT tag_uid FROM rfid_tags WHERE device_id = ?'
    ).all(device.id).map(t => t.tag_uid);

    const pin = db.prepare('SELECT pin_hash FROM pins WHERE device_id = ?').get(device.id);

    socket.emit('credentials_update', {
      rfid_tags: rfidTags,
      has_pin: !!pin
    });

    notifyDashboard(device.user_id, 'device_status', {
      device_id: device.id,
      is_online: true
    });
  });

  // ── Dashboard client authenticates ──
  socket.on('dash_auth', (data) => {
    const { userId } = data;
    socket.userId = userId;
    socket.isDashboard = true;
    dashSockets.set(userId, socket);
    console.log(`Dashboard connected for user ${userId}`);
  });

  // ── ESP32 sends back a new PIN ──
  socket.on('pin_changed', async (data) => {
    if (!socket.deviceId) return;

    const { new_pin } = data;
    const pinHash = await bcrypt.hash(new_pin, 10);

    const existing = db.prepare('SELECT id FROM pins WHERE device_id = ?').get(socket.deviceId);
    if (existing) {
      db.prepare('UPDATE pins SET pin_hash = ?, updated_at = CURRENT_TIMESTAMP WHERE device_id = ?')
        .run(pinHash, socket.deviceId);
    } else {
      db.prepare('INSERT INTO pins (device_id, pin_hash) VALUES (?, ?)')
        .run(socket.deviceId, pinHash);
    }

    socket.emit('pin_hash_update', { pin_hash: pinHash });

    notifyDashboard(socket.userId, 'pin_updated', {
      device_id: socket.deviceId
    });

    console.log(`PIN updated for device ${socket.deviceId}`);
  });

  // ── ESP32 sends PIN for verification ──
  socket.on('verify_pin', async (data) => {
    if (!socket.deviceId) return;

    const { pin } = data;
    const stored = db.prepare('SELECT pin_hash FROM pins WHERE device_id = ?').get(socket.deviceId);

    if (!stored) {
      socket.emit('pin_verified', { success: false });
      return;
    }

    const valid = await bcrypt.compare(pin, stored.pin_hash);
    socket.emit('pin_verified', { success: valid });
  });

  // ── ESP32 sends a newly scanned RFID tag ──
  socket.on('rfid_scanned', (data) => {
    if (!socket.deviceId) return;

    const { tag_uid, label } = data;

    const existing = db.prepare(
      'SELECT id FROM rfid_tags WHERE device_id = ? AND tag_uid = ?'
    ).get(socket.deviceId, tag_uid);

    if (existing) {
      socket.emit('rfid_result', { success: false, message: 'Tag already registered' });
      return;
    }

    db.prepare(
      'INSERT INTO rfid_tags (device_id, tag_uid, label) VALUES (?, ?, ?)'
    ).run(socket.deviceId, tag_uid, label || 'My Card');

    socket.emit('rfid_result', { success: true, message: 'Tag registered' });

    // Push updated credentials to ESP32
    const rfidTags = db.prepare(
      'SELECT tag_uid FROM rfid_tags WHERE device_id = ?'
    ).all(socket.deviceId).map(t => t.tag_uid);
    socket.emit('credentials_update', {
      rfid_tags: rfidTags,
      has_pin: !!db.prepare('SELECT pin_hash FROM pins WHERE device_id = ?').get(socket.deviceId)
    });

    notifyDashboard(socket.userId, 'rfid_added', {
      device_id: socket.deviceId,
      tag_uid,
      label: label || 'My Card'
    });

    console.log(`RFID tag added: ${tag_uid} for device ${socket.deviceId}`);
  });

  // ── ESP32 reports an access attempt ──
  socket.on('access_attempt', (data) => {
    if (!socket.deviceId) return;

    const { method, success, detail } = data;

    db.prepare(
      'INSERT INTO access_log (device_id, method, success, detail) VALUES (?, ?, ?, ?)'
    ).run(socket.deviceId, method, success ? 1 : 0, detail || null);

    notifyDashboard(socket.userId, 'access_attempt', {
      device_id: socket.deviceId,
      method,
      success,
      detail,
      timestamp: new Date().toISOString()
    });
  });

  // ── Disconnect ──
  socket.on('disconnect', () => {
    if (socket.deviceToken) {
      espSockets.delete(socket.deviceToken);
      db.prepare('UPDATE devices SET is_online = 0 WHERE device_token = ?').run(socket.deviceToken);
      notifyDashboard(socket.userId, 'device_status', {
        device_id: socket.deviceId,
        is_online: false
      });
      console.log(`ESP32 disconnected: ${socket.deviceToken.slice(0, 8)}...`);
    }
    if (socket.isDashboard && socket.userId) {
      dashSockets.delete(socket.userId);
    }
  });
});

function notifyDashboard(userId, event, data) {
  const dashSocket = dashSockets.get(userId);
  if (dashSocket) {
    dashSocket.emit(event, data);
  }
}

// ─── CATCH-ALL ────────────────────────────────────

app.get('*', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// ─── START ────────────────────────────────────────

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`\n🔒 Smart Lock Server running on http://localhost:${PORT}\n`);
});
