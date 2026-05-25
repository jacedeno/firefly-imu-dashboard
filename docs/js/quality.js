// Adaptive quality: pick a tier at load, then watch FPS and step down once
// if the device can't hold the target. One-way downshift (no oscillation).
export function detectTier() {
  const mobile = /Mobi|Android|iPhone|iPad/i.test(navigator.userAgent);
  const cores = navigator.hardwareConcurrency || 4;
  const small = Math.min(window.innerWidth, window.innerHeight) < 700;
  const dpr = window.devicePixelRatio || 1;
  if (mobile || small || cores <= 4 || dpr > 2.5) return 'low';
  return 'high';
}

export function createWatchdog(onDownshift) {
  let frames = 0, acc = 0, fps = 60, tripped = false, lowStreak = 0;
  return {
    tick(dt) {
      frames++; acc += dt;
      if (acc >= 0.5) {                  // recompute twice a second
        fps = Math.round(frames / acc);
        frames = 0; acc = 0;
        if (!tripped) {
          if (fps < 45) lowStreak++; else lowStreak = 0;
          if (lowStreak >= 4) { tripped = true; onDownshift && onDownshift(); } // ~2s sustained
        }
      }
      return fps;
    },
    get fps() { return fps; },
  };
}
