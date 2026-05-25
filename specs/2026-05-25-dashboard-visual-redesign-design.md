# Design — Dashboard Visual Redesign ("Lunar Mission Control")

- **Date:** 2026-05-25
- **Status:** Approved (brainstorming) → ready for implementation plan
- **Repo:** `firefly-imu-dashboard`
- **Relationship:** Refines the *Frontend* layer of `PLAN.md`. Transport/firmware (BLE, 9-DOF, Nano 33 BLE) are unchanged from `PLAN.md`; this spec only governs how the browser dashboard looks, moves, and performs.

## Context & Goal
The dashboard is now rendered entirely in the browser (GitHub Pages), free of the old ESP32/LittleFS size limits. The user wants it **much more impactful and "fancy"** while keeping two hard constraints, confirmed during brainstorming:
1. **Telemetry stays clearly readable** (instrument-grade), and
2. **Rendering stays smooth (~60 FPS) on both PC and Android** — effects must scale down automatically on weaker devices.

Chosen visual direction: **a blend of "Cinematic Lunar" (A) + "Mission Control HUD" (B)** — a cinematic Blue Ghost-on-the-Moon scene as the hero, with disciplined aerospace-HUD telemetry overlaid.

## Design Principles
- **Impact without illegibility:** the 3D scene impresses; the HUD never fights the data.
- **Adaptive by default:** one codebase, quality tiers selected at runtime + an FPS watchdog.
- **On-theme:** Firefly black + lime, gold-foil lander, real reused Firefly logo, Blue Ghost lunar landing motif (Blue Ghost landed on the Moon March 2025).

## Visual Direction & Composition
Full-bleed 3D viewport with overlaid glass HUD (no split sidebar). Layout:
- **Top bar (glass):** Firefly logo mark + wordmark `FIREFLY · BLUE GHOST IMU` (left); `● LIVE · BLE`, sensor `Hz`, `FPS` (right).
- **3D hero:** Blue Ghost lander (gold foil, legs down) on the lunar surface; Earth + starfield behind; soft contact shadow. Subtle corner **brackets** frame the lander (mission-control touch).
- **Right glass rail:** stacked glassmorphism cards — `Quaternion (W/X/Y/Z)`, `Attitude (Roll/Pitch/Yaw)`, `Accel` chart, `Gyro` chart.
- **Attitude indicator (artificial horizon):** bottom-left circular gauge + `HDG` heading + `CAL ✓` magnetometer-calibration status.
- **Credit:** "Crafted by GeekendZone" (kept).
- **Mobile:** rail collapses to a bottom row; brackets/ADI retained; scene detail reduced (see Adaptive Quality).

## Components

### 1. 3D Scene
- **Lander (Blue Ghost):** procedural `THREE.Group` — low/wide central body (octagonal/box), top deck with payload blocks, **4 angled legs + footpads**, high-gain antenna dish. Neutral pose: standing on legs on the ground plane; the IMU quaternion rotates it from there.
  - Materials: **PBR `MeshStandardMaterial`** — gold **MLI foil** body (`metalness ~0.9`, `roughness ~0.35`, base `#c8a24e`, accents `#f0d27e`), matte black panels, metallic legs.
  - **Firefly logo** applied as an **emissive decal/texture** on the hull (lime glow). Requires an **alpha-keyed** version of the logo (black → transparent); see Assets.
- **Environment:** lunar regolith ground (subtle displacement/normal or radial-shaded plane), **Earth** sphere (emissive/lit blue marble) in the sky, **starfield** (Points or large sphere). Dark space background/fog `#03050e → #0a0d1c`.
- **Lighting:** hard `DirectionalLight` (sun) casting the contact shadow; soft `HemisphereLight` (sky/regolith fill); a faint lime accent light. Shadows via `PCFSoftShadowMap` (high tier only).

### 2. HUD / UI
- Glassmorphism cards: `background rgba(16,20,16,.42)`, `border 1px rgba(154,202,60,.28)`, `backdrop-filter blur(8px)`, soft shadow + inner top highlight.
- **Attitude indicator:** rotating sky/ground disc driven by roll/pitch; fixed lime aircraft mark; heading readout from yaw.
- Framing brackets and reticle as light SVG/CSS overlays (low cost).

### 3. Post-Processing
- **`EffectComposer` + `UnrealBloomPass`** — *subtle* bloom (low strength/threshold) so the logo, Earth, lights, and lime accents glow premium, not blown-out. Bloom reduced/disabled on low tier.

### 4. Motion & Interaction
- **Connect screen:** centered Firefly logo + glowing "Connect BLE" button (Web Bluetooth requires a user gesture); transitions into the live scene on connect.
- **Idle auto-orbit:** slow camera orbit when the user isn't interacting; existing drag-rotate + wheel/pinch-zoom take over on input, auto-orbit resumes after a timeout.
- **Telemetry easing:** numeric values interpolate (no jumpy text); `LIVE` dot pulses; smooth fade on connect/disconnect.
- **Camera:** use Three.js `OrbitControls` with `enableDamping`; its built-in `autoRotate` drives the idle orbit and is disabled on user input, re-enabled after an idle timeout. Replaces the current hand-rolled orbit/zoom code.

### 5. Typography & Color
- **Titles/wordmark:** **Rajdhani** (Google Fonts, weights 500/600/700).
- **Telemetry numbers:** **JetBrains Mono**.
- **Palette:** background `#03050e–#0a0d1c`; Firefly lime `#9aca3c` (accent) / `#b6f24a` (glow/emissive); gold `#c8a24e`/`#f0d27e`; text `#eaf6d6`; muted `#7e8a6a`. Chart series: X `#ff6b6b`, Y `#9aca3c`, Z `#7fb3ff`.

### 6. Data Flow (frontend)
- BLE characteristic → 10×`int16` (per `PLAN.md`) → de-scale to quaternion `(w,x,y,z)` + accel + gyro.
- **Derive Euler (roll/pitch/yaw) + heading from the quaternion** in JS for the Attitude card and the artificial-horizon gauge.
- Quaternion applied to the lander: `lander.quaternion.set(x, y, z, w)`.
- Reuse existing `onSensorData()`, rolling-chart logic; replace WebSocket with the Web Bluetooth client (per `PLAN.md`).

## Adaptive Quality Strategy (the 60 FPS guarantee)
- **Tier detection at load:** from `devicePixelRatio`, `navigator.hardwareConcurrency`, screen size, and coarse UA/mobile check → choose **High** or **Low**.
  - **High (PC):** full starfield, Earth, soft shadows, full bloom, `pixelRatio` up to 2.
  - **Low (mobile/weak):** reduced star count, no real-time shadows (flat contact-shadow sprite instead), bloom reduced or off, `pixelRatio` 1, simpler ground.
- **FPS watchdog:** rolling FPS average; if it stays below a threshold (e.g. <45 for ~2 s), step the tier down (drop bloom → drop shadows → reduce pixelRatio). One-way down-shift to avoid oscillation.
- Pause the render loop when the tab/page is hidden (`visibilitychange`).

## Tech Stack
- **Three.js r128** (CDN) + matching examples addons: `EffectComposer`, `RenderPass`, `UnrealBloomPass`, `OrbitControls`.
- **Google Fonts:** Rajdhani + JetBrains Mono.
- Static site under **`docs/`** (GitHub Pages), per `PLAN.md`.

## Assets
- `firefly-logo.png` — 64×64 reused from `m5tough-firefly-launch` (`src/logo_firefly.h`), already extracted into the repo. Lime-on-black RGB.
- **Derive an alpha version** (`firefly-logo-alpha.png`) by keying out black, for the emissive hull decal and crisp HUD mark. In CSS overlays, `mix-blend-mode: screen` is an acceptable fallback.

## File Structure (what changes)
- `docs/index.html` — new markup: top bar, viewport, glass rail, ADI, connect overlay; load Three.js + addons + fonts from CDN.
- `docs/app.js` — refactor into clear units: `ble.js`-style transport (Web Bluetooth), `scene.js` (lander + environment + lighting), `hud.js` (cards, ADI, value easing), `quality.js` (tier detection + FPS watchdog), `math.js` (quaternion→Euler/heading). May stay one file with clear sections if splitting complicates GitHub Pages; prefer small modules via `<script type="module">`.
- `docs/style.css` — Firefly theme, glassmorphism, Rajdhani/JetBrains Mono, responsive (rail → bottom row).
- `docs/assets/` — `firefly-logo.png`, `firefly-logo-alpha.png`.

## Non-Goals
- Firmware / BLE protocol changes (owned by `PLAN.md`).
- iOS Safari support (Web Bluetooth unsupported — accepted).
- Photoreal lunar terrain / heavy GLTF models (keep procedural + light for perf).

## Success Criteria
- Visibly "wow" on PC (cinematic lunar scene, glowing gold lander + logo) **and** telemetry remains instantly readable.
- Smooth ~60 FPS on PC and on a mid-range Android in Chrome; effects degrade gracefully under load.
- The lander rotates live from the BLE quaternion; Attitude card + artificial horizon track it; heading is stable after mag calibration.

## Risks / Open Questions
- **`backdrop-filter` (glass) cost on mobile** — if it hurts FPS, fall back to a flat semi-opaque panel on low tier.
- **Bloom on mobile GPUs** — likely off on low tier.
- **Font FOUT** — use `font-display: swap`; acceptable.
- Alpha-keying the logo cleanly (anti-aliased edges over gold) — verify visually; tune threshold.
