const express = require('express');
const bcrypt = require('bcrypt');
const crypto = require('crypto');
const db = require('../db');
const { authMiddleware } = require('../middleware/auth');

const router = express.Router();

router.use(authMiddleware);

// ─── DEVICES ──────────────────────────────────────

// GET /api/devices
router.get('/', (req, res) => {
  const devices = db.prepare(`
    SELECT id, label, device_token, is_online, created_at
    FROM devices WHERE user_id = ?
  `).all(req.user.id);
  res.json({ devices });
});

// POST /api/devices
router.post('/', (req, res) => {
  const { label } = req.body;
  const deviceToken = crypto.randomBytes(16).toString('hex');

  const result = db.prepare(
    'INSERT INTO devices (user_id, label, device_token) VALUES (?, ?, ?)'
  ).run(req.user.id, label || 'New Lock', deviceToken);

  res.status(201).json({
    device: {
      id: result.lastInsertRowid,
      label: label || 'New Lock',
      device_token: deviceToken
    }
  });
});

// PATCH /api/devices/:id
router.patch('/:id', (req, res) => {
  const { label } = req.body;
  const device = getDeviceForUser(req.params.id, req.user.id);
  if (!device) return res.status(404).json({ error: 'Device not found' });

  db.prepare('UPDATE devices SET label = ? WHERE id = ?').run(label, device.id);
  res.json({ success: true });
});

// ─── PIN MANAGEMENT ───────────────────────────────

// POST /api/devices/:id/pin — request PIN change
router.post('/:id/pin', (req, res) => {
  const device = getDeviceForUser(req.params.id, req.user.id);
  if (!device) return res.status(404).json({ error: 'Device not found' });

  const io = req.app.get('io');
  const espSocket = req.app.get('espSockets')?.get(device.device_token);

  if (!espSocket) {
    return res.status(503).json({ error: 'Device is offline' });
  }

  espSocket.emit('command', { action: 'change_pin' });
  res.json({ status: 'pending', message: 'Enter new PIN on the lock keypad' });
});

// GET /api/devices/:id/pin
router.get('/:id/pin', (req, res) => {
  const device = getDeviceForUser(req.params.id, req.user.id);
  if (!device) return res.status(404).json({ error: 'Device not found' });

  const pin = db.prepare('SELECT id, updated_at FROM pins WHERE device_id = ?').get(device.id);
  res.json({ hasPin: !!pin, updatedAt: pin?.updated_at || null });
});

// ─── RFID MANAGEMENT ──────────────────────────────

// GET /api/devices/:id/rfid
router.get('/:id/rfid', (req, res) => {
  const device = getDeviceForUser(req.params.id, req.user.id);
  if (!device) return res.status(404).json({ error: 'Device not found' });

  const tags = db.prepare(
    'SELECT id, tag_uid, label, created_at FROM rfid_tags WHERE device_id = ?'
  ).all(device.id);
  res.json({ tags });
});

// POST /api/devices/:id/rfid — request RFID scan mode
router.post('/:id/rfid', (req, res) => {
  const device = getDeviceForUser(req.params.id, req.user.id);
  if (!device) return res.status(404).json({ error: 'Device not found' });

  const io = req.app.get('io');
  const espSocket = req.app.get('espSockets')?.get(device.device_token);

  if (!espSocket) {
    return res.status(503).json({ error: 'Device is offline' });
  }

  const label = req.body.label || 'My Card';
  espSocket.emit('command', { action: 'scan_rfid', label });
  res.json({ status: 'pending', message: 'Scan your RFID card on the lock' });
});

// DELETE /api/devices/:id/rfid/:tagId
router.delete('/:id/rfid/:tagId', (req, res) => {
  const device = getDeviceForUser(req.params.id, req.user.id);
  if (!device) return res.status(404).json({ error: 'Device not found' });

  const result = db.prepare(
    'DELETE FROM rfid_tags WHERE id = ? AND device_id = ?'
  ).run(req.params.tagId, device.id);

  if (result.changes === 0) return res.status(404).json({ error: 'Tag not found' });

  pushCredentialsToDevice(device, req.app);
  res.json({ success: true });
});

// ─── DASHBOARD UNLOCK ─────────────────────────────

// POST /api/devices/:id/dash-unlock
router.post('/:id/dash-unlock', (req, res) => {
  const device = getDeviceForUser(req.params.id, req.user.id);
  if (!device) return res.status(404).json({ error: 'Device not found' });

  const io = req.app.get('io');
  const espSocket = req.app.get('espSockets')?.get(device.device_token);

  if (!espSocket) {
    return res.status(503).json({ error: 'Device is offline' });
  }

  espSocket.emit('command', { action: 'dash_unlock' });
  res.json({ status: 'sent', message: 'Unlock signal sent to lock' });
});

// ─── ACCESS LOG ───────────────────────────────────

router.get('/:id/log', (req, res) => {
  const device = getDeviceForUser(req.params.id, req.user.id);
  if (!device) return res.status(404).json({ error: 'Device not found' });

  const logs = db.prepare(
    'SELECT * FROM access_log WHERE device_id = ? ORDER BY timestamp DESC LIMIT 50'
  ).all(device.id);
  res.json({ logs });
});

// ─── HELPERS ──────────────────────────────────────

function getDeviceForUser(deviceId, userId) {
  return db.prepare(
    'SELECT * FROM devices WHERE id = ? AND user_id = ?'
  ).get(deviceId, userId);
}

function pushCredentialsToDevice(device, app) {
  const espSocket = app.get('espSockets')?.get(device.device_token);
  if (!espSocket) return;

  const rfidTags = db.prepare(
    'SELECT tag_uid FROM rfid_tags WHERE device_id = ?'
  ).all(device.id).map(t => t.tag_uid);

  espSocket.emit('credentials_update', {
    rfid_tags: rfidTags
  });
}

module.exports = router;
module.exports.pushCredentialsToDevice = pushCredentialsToDevice;
