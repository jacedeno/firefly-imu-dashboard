# Implementation Plan — Dashboard Visual Redesign

- **Date:** 2026-05-25
- **Spec:** `specs/2026-05-25-dashboard-visual-redesign-design.md`
- **Scope:** Frontend only (the BLE/firmware work stays in `PLAN.md`).

## Key strategy: build against a simulation mode
Firmware is gated on confirming the Nano's revision at home, so the dashboard is built and validated against a **simulated data source** (a synthetic quaternion + accel/gyro generator). The real Web Bluetooth client plugs into the same `onSensorData()` interface later. This unblocks the entire UI now and doubles as a permanent demo/dev mode (`?sim=1`).

## Phases (incremental, each independently verifiable)

### Phase 1 — Scaffold & assets
- Move `data/` → `docs/` (GitHub Pages root). Delete the obsolete `three.min.js.gz` reference.
- `docs/index.html`: load **Three.js r128** + addons (`OrbitControls`, `EffectComposer`, `RenderPass`, `UnrealBloomPass`) and **Google Fonts** (Rajdhani, JetBrains Mono) from CDN. New markup: connect overlay, top bar, viewport, glass rail, ADI.
- `docs/assets/`: `firefly-logo.png` (+ generated `firefly-logo-alpha.png`, black keyed out, for the emissive decal/crisp mark).
- Module structure under `docs/js/`: `main.js`, `sim.js`, `ble.js`, `scene.js`, `lander.js`, `hud.js`, `quality.js`, `mathx.js` (ES modules).
- **Verify:** page loads via a local server, fonts + Three.js load, empty themed scene renders.

### Phase 2 — Simulated data + render loop
- `sim.js`: synthetic quaternion (smooth multi-axis rotation) + plausible accel/gyro; `main.js` render loop; `mathx.js` quaternion→Euler/heading.
- **Verify:** a placeholder mesh rotates smoothly from sim data; FPS counter works.

### Phase 3 — 3D scene & lander
- `lander.js`: procedural Blue Ghost (body, deck, 4 legs+pads, antenna), **PBR gold-foil** materials, Firefly **emissive logo decal**.
- `scene.js`: lunar ground, Earth, starfield, sun + hemisphere + lime accent lights, soft contact shadow.
- **Verify:** lander stands on legs, rotates from sim quaternion; looks cinematic on PC.

### Phase 4 — HUD / UI
- `hud.js`: top bar (logo+wordmark, LIVE/Hz/FPS), glass rail (Quaternion, Attitude R/P/Y, Accel + Gyro rolling charts), **artificial-horizon ADI** + heading + CAL status, framing brackets, value easing, LIVE pulse.
- `style.css`: Firefly theme, glassmorphism, Rajdhani/JetBrains Mono, responsive (rail → bottom row on mobile).
- **Verify:** every readout tracks sim data; layout holds on desktop + narrow widths.

### Phase 5 — Post-processing & camera motion
- `EffectComposer` + subtle `UnrealBloomPass`; `OrbitControls` (damping + idle `autoRotate`, disabled on input).
- **Verify:** tasteful glow on logo/Earth/lights; idle orbit; bloom not blown out.

### Phase 6 — Adaptive quality
- `quality.js`: tier detection (dpr, cores, screen, mobile UA) → High/Low presets; **FPS watchdog** that steps effects down under load (bloom → shadows → pixelRatio); pause loop when tab hidden.
- **Verify:** CPU/GPU throttle + mobile emulation hold ~60 FPS; effects degrade gracefully.

### Phase 7 — Web Bluetooth client
- `ble.js`: connect screen gesture → `requestDevice` → subscribe to the GATT characteristic → de-scale 10×int16 → `onSensorData()`. Sim remains as fallback/dev mode.
- **Verify (after firmware exists):** real Nano drives the dashboard; reconnect handling works.

### Phase 8 — Deploy
- Enable GitHub Pages (branch `main`, `/docs`). 
- **Verify:** load `https://jacedeno.github.io/firefly-imu-dashboard/` on PC + Android Chrome; sim mode works for anyone, BLE works with the board.

## Order / gating
- Phases 1–6 + 8 need **no hardware** (use sim mode) → can be done now.
- Phase 7 (real BLE) overlaps with the firmware milestone in `PLAN.md` (needs the Nano revision confirmed and firmware notifying).

## Verification tooling
- Local serve `docs/` (e.g. `python3 -m http.server`) for dev; Chrome for Web Bluetooth (HTTPS/localhost).
- Optionally drive a quick visual check with the `playwright-cli` skill (screenshots at desktop + mobile viewports).
