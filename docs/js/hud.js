// HUD: rolling charts, eased telemetry readouts, attitude indicator, status.
import { quatToEuler, heading, smoothK, lerp, lerpAngle } from './mathx.js';

const COLORS = { x: '#ff6b6b', y: '#9aca3c', z: '#7fb3ff', zero: 'rgba(154,202,60,.18)' };

function RollingChart(canvas, min, max) {
  this.ctx = canvas.getContext('2d');
  this.canvas = canvas; this.min = min; this.max = max; this.n = 120;
  this.X = []; this.Y = []; this.Z = [];
}
RollingChart.prototype.push = function (x, y, z) {
  this.X.push(x); this.Y.push(y); this.Z.push(z);
  if (this.X.length > this.n) { this.X.shift(); this.Y.shift(); this.Z.shift(); }
};
RollingChart.prototype.draw = function () {
  const { ctx, canvas, min, max, n } = this;
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  const zy = h * (max / (max - min));
  ctx.strokeStyle = COLORS.zero; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(0, zy); ctx.lineTo(w, zy); ctx.stroke();
  const series = [[this.X, COLORS.x], [this.Y, COLORS.y], [this.Z, COLORS.z]];
  for (const [arr, color] of series) {
    ctx.strokeStyle = color; ctx.lineWidth = 1.6; ctx.beginPath();
    for (let i = 0; i < arr.length; i++) {
      const px = (i / (n - 1)) * w;
      const py = h - ((arr[i] - min) / (max - min)) * h;
      i ? ctx.lineTo(px, py) : ctx.moveTo(px, py);
    }
    ctx.stroke();
  }
};

export function createHud() {
  const $ = (id) => document.getElementById(id);
  const el = {
    qw: $('q-w'), qx: $('q-x'), qy: $('q-y'), qz: $('q-z'),
    roll: $('roll-val'), pitch: $('pitch-val'), yaw: $('yaw-val'),
    adi: $('adi-ball'), hdg: $('hdg-val'), cal: $('cal-val'),
    link: $('link-state'), linkLabel: $('link-label'),
    rate: $('rate-val'), fps: $('fps-val'), tier: $('tier-val'),
  };
  const accel = new RollingChart($('accel-chart'), -20, 20);
  const gyro = new RollingChart($('gyro-chart'), -8, 8);

  const s = { w: 1, x: 0, y: 0, z: 0, roll: 0, pitch: 0, yaw: 0 };

  return {
    update(dt, d, latest) {
      const k = smoothK(16, dt);
      s.w = lerp(s.w, d.w, k); s.x = lerp(s.x, d.x, k);
      s.y = lerp(s.y, d.y, k); s.z = lerp(s.z, d.z, k);
      el.qw.textContent = s.w.toFixed(3); el.qx.textContent = s.x.toFixed(3);
      el.qy.textContent = s.y.toFixed(3); el.qz.textContent = s.z.toFixed(3);

      const e = quatToEuler(d.w, d.x, d.y, d.z);
      s.roll = lerpAngle(s.roll, e.roll, k);
      s.pitch = lerp(s.pitch, e.pitch, k);
      s.yaw = lerpAngle(s.yaw, e.yaw, k);
      el.roll.textContent = s.roll.toFixed(1) + '°';
      el.pitch.textContent = s.pitch.toFixed(1) + '°';
      el.yaw.textContent = s.yaw.toFixed(1) + '°';
      el.hdg.textContent = String(Math.round(heading(s.yaw))).padStart(3, '0') + '°';
      el.adi.style.setProperty('--roll', (-s.roll) + 'deg');
      el.adi.style.setProperty('--pitch', (s.pitch * 1.5) + 'px');

      if (latest) { accel.push(d.ax, d.ay, d.az); gyro.push(d.gx, d.gy, d.gz); }
      accel.draw(); gyro.draw();
    },
    setLink(label, live) {
      el.linkLabel.textContent = label;
      el.link.className = 'chip ' + (live ? 'chip-live' : 'chip-off');
    },
    setCal(ok) { el.cal.textContent = ok ? '✓' : '…'; el.cal.className = 'v' + (ok ? ' ok' : ''); },
    setRate(hz) { el.rate.textContent = hz + ' Hz'; },
    setFps(fps) { el.fps.textContent = String(fps); },
    setTier(name) { el.tier.textContent = name; },
  };
}
