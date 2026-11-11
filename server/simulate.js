// Fake device for testing the dashboard without hardware.
//   node simulate.js            -> heartbeat every 3 s (battery slowly drains)
//   node simulate.js fall       -> send one CONFIRMED fall event
//   node simulate.js cancel     -> send one CANCELLED event
const BASE = process.env.SERVER || 'http://localhost:3000';
const KEY = process.env.API_KEY || 'change-me';
const ID = process.env.DEVICE_ID || 'fall-01';
const LAT = 19.0800, LON = 83.8200;

async function post(p, body) {
  const r = await fetch(BASE + p, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'x-api-key': KEY },
    body: JSON.stringify(body)
  });
  if (!r.ok) throw new Error(p + ' -> HTTP ' + r.status);
}

const mode = process.argv[2];
if (mode === 'fall' || mode === 'cancel') {
  post('/api/events', {
    deviceId: ID, type: mode === 'fall' ? 'confirmed' : 'cancelled',
    fix: true, lat: LAT, lon: LON, smsSent: mode === 'fall', ageMs: 0
  }).then(() => console.log('event sent:', mode)).catch(e => { console.error(e.message); process.exit(1); });
} else {
  let pct = 92;
  const beat = async () => {
    pct = Math.max(5, pct - 0.05);
    const v = 3.3 + (pct / 100) * 0.9;
    try {
      await post('/api/telemetry', {
        deviceId: ID, battery: +v.toFixed(2), batteryPct: Math.round(pct), fix: true,
        lat: LAT + (Math.random() - 0.5) * 0.0004, lon: LON + (Math.random() - 0.5) * 0.0004,
        state: 'MONITORING', rssi: -55 - Math.round(Math.random() * 10), gsmOk: true
      });
      console.log('heartbeat ok, battery', Math.round(pct) + '%');
    } catch (e) { console.error(e.message); }
  };
  beat(); setInterval(beat, 3000);
}
