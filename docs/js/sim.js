// Simulated IMU source — smooth, believable 9-DOF data so the whole
// dashboard works without hardware. Same {w,x,y,z,ax,ay,az,gx,gy,gz}
// shape the BLE source emits.
import { eulerToQuat } from './mathx.js';

const G = 9.80665;

export function createSimSource() {
  let timer = null;
  let t0 = 0;

  function sample(now) {
    const t = (now - t0) / 1000;
    // Slow, multi-axis attitude.
    const roll = 28 * Math.sin(t * 0.55);
    const pitch = 16 * Math.sin(t * 0.37 + 1.1);
    const yaw = 45 * Math.sin(t * 0.16);
    const q = eulerToQuat(roll, pitch, yaw);

    // Gravity projected into the (rotating) body frame + a little noise.
    const r = roll * Math.PI / 180, p = pitch * Math.PI / 180;
    const ax = G * Math.sin(p) + n(0.15);
    const ay = -G * Math.sin(r) * Math.cos(p) + n(0.15);
    const az = G * Math.cos(r) * Math.cos(p) + n(0.15);

    // Gyro ~ derivative of the attitude (rad/s).
    const gx = (28 * 0.55) * Math.cos(t * 0.55) * Math.PI / 180 + n(0.02);
    const gy = (16 * 0.37) * Math.cos(t * 0.37 + 1.1) * Math.PI / 180 + n(0.02);
    const gz = (45 * 0.16) * Math.cos(t * 0.16) * Math.PI / 180 + n(0.02);

    return { w: q.w, x: q.x, y: q.y, z: q.z, ax, ay, az, gx, gy, gz };
  }

  const n = (a) => (Math.random() - 0.5) * 2 * a;

  return {
    name: 'DEMO',
    live: true,
    start(onData) {
      t0 = performance.now();
      timer = setInterval(() => onData(sample(performance.now())), 10); // ~100 Hz
    },
    stop() { if (timer) clearInterval(timer); timer = null; },
  };
}
