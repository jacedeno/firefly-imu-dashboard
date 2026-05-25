// Orchestrator: overlay → pick a source (BLE or demo) → drive the scene + HUD.
import { createScene } from './scene.js';
import { createHud } from './hud.js';
import { createSimSource } from './sim.js';
import { createBleSource, bleSupported } from './ble.js';
import { detectTier, createWatchdog } from './quality.js';

const overlay = document.getElementById('overlay');
const app = document.getElementById('app');
const btnConnect = document.getElementById('btn-connect');
const btnSim = document.getElementById('btn-sim');
const note = document.getElementById('overlay-note');

const hud = createHud();
let tier = detectTier();
if (tier === 'low') document.body.classList.add('tier-low');

let scene = null, source = null, latest = null, samples = 0, started = false;

function begin(src, label, live) {
  if (started) return;
  started = true; source = src;

  overlay.classList.add('hidden');
  app.classList.remove('hidden');
  app.classList.add('revealing');
  setTimeout(() => app.classList.remove('revealing'), 900);

  const viewport = document.getElementById('viewport');
  scene = createScene(viewport);
  scene.setQuality(tier);
  hud.setTier(tier.toUpperCase());
  hud.setLink(label, live);

  src.start((d) => { latest = d; samples++; });
  if (src.onDisconnect) src.onDisconnect(() => hud.setLink('OFFLINE', false));

  window.addEventListener('resize', () => scene.resize());
  setInterval(() => { hud.setRate(samples); samples = 0; }, 1000);

  const watchdog = createWatchdog(() => {
    tier = 'low'; document.body.classList.add('tier-low');
    scene.setQuality('low'); hud.setTier('LOW');
  });

  let prev = performance.now();
  function loop(now) {
    requestAnimationFrame(loop);
    const dt = Math.min((now - prev) / 1000, 0.1); prev = now;
    if (document.hidden) return;
    const fps = watchdog.tick(dt);
    if (latest) { scene.setOrientation(latest); hud.update(dt, latest, true); }
    hud.setFps(fps);
    scene.render(dt);
  }
  requestAnimationFrame(loop);
}

// ---- Demo mode ----
btnSim.addEventListener('click', () => begin(createSimSource(), 'DEMO', true));

// ---- Bluetooth ----
if (!bleSupported()) {
  btnConnect.disabled = true;
  btnConnect.style.opacity = 0.5;
  note.textContent = 'Web Bluetooth unavailable — use Chrome/Edge (desktop or Android). Try demo mode.';
}
btnConnect.addEventListener('click', async () => {
  const ble = createBleSource();
  note.textContent = 'Select "Firefly-BlueGhost-IMU"…';
  try {
    const name = await ble.start((d) => { latest = d; samples++; });
    // start() already subscribed; hand the same source to begin() without restarting.
    beginFromConnected(ble, name);
  } catch (err) {
    note.textContent = 'Connection cancelled or failed. ' + (err.message || '');
  }
});

// BLE already started notifications inside ble.start(); wire it up without
// calling start() twice.
function beginFromConnected(ble, name) {
  const passthrough = {
    name: 'BLE', live: true,
    start() { /* already streaming via the closure above */ },
    stop: ble.stop, onDisconnect: ble.onDisconnect,
  };
  begin(passthrough, name || 'BLE', true);
}
