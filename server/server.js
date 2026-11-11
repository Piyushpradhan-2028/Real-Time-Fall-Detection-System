// Fall Detection - backend API + dashboard host
const express = require('express');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 3000;
const API_KEY = process.env.API_KEY || 'change-me';   // must match firmware config.h
const ONLINE_MS = 40000;                              // device is "online" if seen in last 40 s
const DEMO_FOR_EVERYONE = process.env.DEMO === '1';   // allow demo buttons from other PCs too
const DATA_DIR = path.join(__dirname, 'data');
const DB_FILE = path.join(DATA_DIR, 'db.json');

// ---------- tiny JSON "database" ----------
let db = { devices: {}, events: [] };
try {
  fs.mkdirSync(DATA_DIR, { recursive: true });
  if (fs.existsSync(DB_FILE)) db = JSON.parse(fs.readFileSync(DB_FILE, 'utf8'));
} catch (e) { console.error('Could not load db, starting empty:', e.message); }

let lastSave = 0;
function save() {
  lastSave = Date.now();
  try { fs.writeFileSync(DB_FILE, JSON.stringify(db)); }
  catch (e) { console.error('Save failed:', e.message); }
}

const num = v => (typeof v === 'number' && isFinite(v) ? v : null);
const str = (v, max = 40) => String(v ?? '').slice(0, max);

function addEvent({ deviceId, type, ts, fix, lat, lon, smsSent }) {
  const ev = {
    id: ts.toString(36) + Math.random().toString(36).slice(2, 6),
    deviceId, type, ts,
    fix: !!fix,
    lat: fix ? num(lat) : null,
    lon: fix ? num(lon) : null,
    smsSent: !!smsSent
  };
  db.events.push(ev);
  if (db.events.length > 1000) db.events.shift();
  if (ev.lat !== null && ev.lon !== null) {
    const d = db.devices[deviceId] || { deviceId, lastSeen: 0 };
    d.lastLocation = { lat: ev.lat, lon: ev.lon, at: ts };
    db.devices[deviceId] = d;
  }
  save();
  console.log(`[event] ${deviceId} ${type} sms=${ev.smsSent}`);
  return ev;
}

// ---------- built-in demo device (no hardware needed) ----------
const DEMO_ID = 'fall-01', DEMO_LAT = 19.0800, DEMO_LON = 83.8200;
const demo = { running: false, timer: null, timeouts: [], pct: 92, state: 'MONITORING' };

function demoBeat() {
  demo.pct = Math.max(5, demo.pct - 0.05);
  db.devices[DEMO_ID] = {
    deviceId: DEMO_ID,
    battery: +(3.3 + (demo.pct / 100) * 0.9).toFixed(2),
    batteryPct: Math.round(demo.pct),
    state: demo.state,
    rssi: -55 - Math.round(Math.random() * 10),
    gsmOk: true,
    lastSeen: Date.now(),
    lastLocation: {
      lat: DEMO_LAT + (Math.random() - 0.5) * 0.0004,
      lon: DEMO_LON + (Math.random() - 0.5) * 0.0004,
      at: Date.now()
    }
  };
}
const demoLater = (fn, ms) => demo.timeouts.push(setTimeout(fn, ms));

function demoStart() {
  if (demo.running) return;
  demo.running = true;
  demoBeat();
  demo.timer = setInterval(demoBeat, 3000);
  console.log('[demo] virtual device started');
}
function demoStop() {
  clearInterval(demo.timer);
  demo.timeouts.forEach(clearTimeout);
  demo.timeouts = []; demo.running = false; demo.state = 'MONITORING';
  console.log('[demo] stopped');
}
function demoFall(cancelled) {
  demoStart();
  if (demo.state !== 'MONITORING') return false;
  demo.state = 'COUNTDOWN'; demoBeat();
  if (cancelled) {
    demoLater(() => {
      addEvent({ deviceId: DEMO_ID, type: 'cancelled', ts: Date.now(), fix: true, lat: DEMO_LAT, lon: DEMO_LON, smsSent: false });
      demo.state = 'MONITORING'; demoBeat();
    }, 4000);
  } else {
    demoLater(() => {                       // 10 s countdown not cancelled -> alert sent
      addEvent({ deviceId: DEMO_ID, type: 'confirmed', ts: Date.now(), fix: true, lat: DEMO_LAT, lon: DEMO_LON, smsSent: true });
      demo.state = 'ALERT_SENT'; demoBeat();
      demoLater(() => { demo.state = 'MONITORING'; demoBeat(); }, 30000);
    }, 10000);
  }
  return true;
}

const isLocal = req => ['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(req.socket.remoteAddress);
const demoAllowed = req => DEMO_FOR_EVERYONE || isLocal(req);

// ---------- app ----------
const app = express();
app.use(express.json({ limit: '10kb' }));
app.use(express.static(path.join(__dirname, 'public')));

function requireKey(req, res, next) {
  if (req.get('x-api-key') !== API_KEY) return res.status(401).json({ error: 'bad api key' });
  next();
}

// Device -> server: status heartbeat (every ~10 s)
app.post('/api/telemetry', requireKey, (req, res) => {
  const b = req.body || {};
  const id = str(b.deviceId);
  if (!id) return res.status(400).json({ error: 'deviceId required' });
  const prev = db.devices[id] || {};
  const dev = {
    deviceId: id,
    battery: num(b.battery),
    batteryPct: num(b.batteryPct),
    state: str(b.state),
    rssi: num(b.rssi),
    gsmOk: !!b.gsmOk,
    lastSeen: Date.now(),
    lastLocation: prev.lastLocation || null
  };
  if (b.fix && num(b.lat) !== null && num(b.lon) !== null) {
    dev.lastLocation = { lat: b.lat, lon: b.lon, at: Date.now() };
  }
  db.devices[id] = dev;
  if (Date.now() - lastSave > 30000) save();
  res.json({ ok: true });
});

// Device -> server: fall event (confirmed / cancelled)
app.post('/api/events', requireKey, (req, res) => {
  const b = req.body || {};
  const id = str(b.deviceId);
  const type = str(b.type, 20);
  if (!id || !['confirmed', 'cancelled'].includes(type))
    return res.status(400).json({ error: 'deviceId and type (confirmed|cancelled) required' });
  const ev = addEvent({
    deviceId: id, type, ts: Date.now() - (num(b.ageMs) || 0),
    fix: b.fix, lat: b.lat, lon: b.lon, smsSent: b.smsSent
  });
  res.json({ ok: true, id: ev.id });
});

// Dashboard reads
app.get('/api/status', (req, res) => {
  const now = Date.now();
  const devices = Object.values(db.devices).map(d => ({
    ...d, online: now - d.lastSeen < ONLINE_MS, secondsAgo: Math.round((now - d.lastSeen) / 1000)
  }));
  res.json({
    now, devices,
    demo: { allowed: demoAllowed(req), running: demo.running, busy: demo.state !== 'MONITORING' }
  });
});

app.get('/api/events', (req, res) => {
  const limit = Math.min(parseInt(req.query.limit) || 50, 500);
  res.json({ events: db.events.slice().reverse().slice(0, limit) });
});

// Demo controls (buttons on the dashboard)
app.post('/api/demo/:action', (req, res) => {
  if (!demoAllowed(req)) return res.status(403).json({ error: 'demo only allowed from the server PC (or run with DEMO=1)' });
  switch (req.params.action) {
    case 'start':  demoStart(); return res.json({ ok: true });
    case 'stop':   demoStop();  return res.json({ ok: true });
    case 'fall':   return demoFall(false) ? res.json({ ok: true }) : res.status(409).json({ error: 'demo is busy' });
    case 'cancel': return demoFall(true)  ? res.json({ ok: true }) : res.status(409).json({ error: 'demo is busy' });
    default: return res.status(404).json({ error: 'unknown action' });
  }
});

app.get('/api/health', (_req, res) => res.json({ ok: true }));

app.listen(PORT, '0.0.0.0', () => {
  console.log(`Fall detection server running: http://localhost:${PORT}`);
  if (API_KEY === 'change-me') console.log('WARNING: using default API key. Set API_KEY env var for real use.');
  if (process.argv.includes('--demo')) demoStart();
});
