// Small math helpers: quaternion <-> Euler, smoothing.
const RAD2DEG = 180 / Math.PI;
const DEG2RAD = Math.PI / 180;

// Tait-Bryan (aerospace ZYX) from a quaternion (w,x,y,z). Returns degrees.
export function quatToEuler(w, x, y, z) {
  // roll (x-axis rotation)
  const sinr_cosp = 2 * (w * x + y * z);
  const cosr_cosp = 1 - 2 * (x * x + y * y);
  const roll = Math.atan2(sinr_cosp, cosr_cosp);
  // pitch (y-axis rotation)
  let sinp = 2 * (w * y - z * x);
  sinp = Math.max(-1, Math.min(1, sinp));
  const pitch = Math.asin(sinp);
  // yaw (z-axis rotation)
  const siny_cosp = 2 * (w * z + x * y);
  const cosy_cosp = 1 - 2 * (y * y + z * z);
  const yaw = Math.atan2(siny_cosp, cosy_cosp);
  return { roll: roll * RAD2DEG, pitch: pitch * RAD2DEG, yaw: yaw * RAD2DEG };
}

export function heading(yawDeg) {
  return ((yawDeg % 360) + 360) % 360;
}

export function eulerToQuat(rollDeg, pitchDeg, yawDeg) {
  const cr = Math.cos(rollDeg * DEG2RAD / 2), sr = Math.sin(rollDeg * DEG2RAD / 2);
  const cp = Math.cos(pitchDeg * DEG2RAD / 2), sp = Math.sin(pitchDeg * DEG2RAD / 2);
  const cy = Math.cos(yawDeg * DEG2RAD / 2), sy = Math.sin(yawDeg * DEG2RAD / 2);
  return {
    w: cr * cp * cy + sr * sp * sy,
    x: sr * cp * cy - cr * sp * sy,
    y: cr * sp * cy + sr * cp * sy,
    z: cr * cp * sy - sr * sp * cy,
  };
}

export const lerp = (a, b, t) => a + (b - a) * t;

// Frame-rate independent exponential smoothing factor.
export const smoothK = (perSecond, dt) => 1 - Math.exp(-perSecond * dt);

// Shortest-path interpolation for angles in degrees.
export function lerpAngle(a, b, t) {
  let d = ((b - a + 540) % 360) - 180;
  return a + d * t;
}
