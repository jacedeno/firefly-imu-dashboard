// "Lunar Mission Control" 3D scene: Blue Ghost lander on the Moon, Earth +
// starfield, cinematic light, soft contact shadow, subtle UnrealBloom, and an
// auto-orbiting OrbitControls camera. All effects scale via setQuality().
/* global THREE */
import { createLander } from './lander.js';

function shadowSprite() {
  const c = document.createElement('canvas'); c.width = c.height = 256;
  const ctx = c.getContext('2d');
  const grd = ctx.createRadialGradient(128, 128, 8, 128, 128, 128);
  grd.addColorStop(0, 'rgba(0,0,0,0.55)');
  grd.addColorStop(0.6, 'rgba(0,0,0,0.28)');
  grd.addColorStop(1, 'rgba(0,0,0,0)');
  ctx.fillStyle = grd; ctx.fillRect(0, 0, 256, 256);
  return new THREE.CanvasTexture(c);
}

export function createScene(container) {
  const renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
  renderer.outputEncoding = THREE.sRGBEncoding;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.12;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  container.appendChild(renderer.domElement);

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x05060e);
  scene.fog = new THREE.FogExp2(0x05060e, 0.006);

  const camera = new THREE.PerspectiveCamera(46, 1, 0.1, 400);
  camera.position.set(4.5, 3.0, 6.2);

  const controls = new THREE.OrbitControls(camera, renderer.domElement);
  controls.target.set(0, 1.0, 0);
  controls.enableDamping = true; controls.dampingFactor = 0.06;
  controls.enablePan = false; controls.minDistance = 3.2; controls.maxDistance = 22;
  controls.maxPolarAngle = Math.PI * 0.92;
  controls.autoRotate = true; controls.autoRotateSpeed = 0.55;
  let resumeTimer = null;
  controls.addEventListener('start', () => { controls.autoRotate = false; clearTimeout(resumeTimer); });
  controls.addEventListener('end', () => { resumeTimer = setTimeout(() => (controls.autoRotate = true), 4000); });

  // ---- Lights ----
  const sun = new THREE.DirectionalLight(0xfff2d6, 2.7);
  sun.position.set(7, 10, 5); sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  sun.shadow.camera.near = 1; sun.shadow.camera.far = 40;
  sun.shadow.camera.left = -8; sun.shadow.camera.right = 8;
  sun.shadow.camera.top = 8; sun.shadow.camera.bottom = -8;
  sun.shadow.bias = -0.0004;
  scene.add(sun);
  const hemi = new THREE.HemisphereLight(0x9fc4e6, 0x4a3a22, 0.55); scene.add(hemi);
  const accent = new THREE.PointLight(0x9aca3c, 0.7, 18); accent.position.set(-3, 2.5, 3); scene.add(accent);
  const fill = new THREE.DirectionalLight(0x6688cc, 0.4); fill.position.set(-6, 3, -4); scene.add(fill);

  // ---- Lunar ground ----
  const ground = new THREE.Mesh(
    new THREE.CircleGeometry(80, 64),
    new THREE.MeshStandardMaterial({ color: 0x413c33, roughness: 1.0, metalness: 0.0 })
  );
  ground.rotation.x = -Math.PI / 2; ground.receiveShadow = true; scene.add(ground);

  // contact shadow blob
  const blob = new THREE.Mesh(
    new THREE.PlaneGeometry(5.6, 5.6),
    new THREE.MeshBasicMaterial({ map: shadowSprite(), transparent: true, depthWrite: false })
  );
  blob.rotation.x = -Math.PI / 2; blob.position.y = 0.02; scene.add(blob);

  // ---- Stars ----
  let stars = null;
  function buildStars(count) {
    if (stars) { scene.remove(stars); stars.geometry.dispose(); }
    const pos = new Float32Array(count * 3);
    for (let i = 0; i < count; i++) {
      const r = 140, u = Math.random(), v = Math.random();
      const th = 2 * Math.PI * u, ph = Math.acos(2 * v - 1);
      pos[i * 3] = r * Math.sin(ph) * Math.cos(th);
      pos[i * 3 + 1] = Math.abs(r * Math.cos(ph)) * 0.8 + 6;
      pos[i * 3 + 2] = r * Math.sin(ph) * Math.sin(th);
    }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    stars = new THREE.Points(geo, new THREE.PointsMaterial({ color: 0xcfe0ff, size: 0.7, sizeAttenuation: false }));
    scene.add(stars);
  }

  // ---- Earth ----
  const earth = new THREE.Mesh(
    new THREE.SphereGeometry(4.2, 32, 32),
    new THREE.MeshStandardMaterial({ color: 0x2a6cc0, emissive: 0x123a78, emissiveIntensity: 0.7, roughness: 0.85, metalness: 0.0 })
  );
  earth.position.set(-24, 17, -46); scene.add(earth);
  const halo = new THREE.Mesh(
    new THREE.SphereGeometry(4.9, 24, 24),
    new THREE.MeshBasicMaterial({ color: 0x4a90e2, transparent: true, opacity: 0.18, side: THREE.BackSide })
  );
  halo.position.copy(earth.position); scene.add(halo);

  // ---- Lander ----
  const lander = createLander(renderer);
  lander.position.y = 1.2; scene.add(lander);
  const targetQ = new THREE.Quaternion();
  // IMU frame is Z-up (Madgwick gravity reference); Three.js is Y-up. Convert by
  // conjugation: q_display = FRAME * q_imu * FRAME^-1, FRAME = -90deg about X.
  // Keeps identity upright while mapping sensor yaw to the lander's vertical.
  const FRAME = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1, 0, 0), -Math.PI / 2);
  const FRAME_INV = FRAME.clone().invert();

  // ---- Post-processing ----
  const composer = new THREE.EffectComposer(renderer);
  composer.addPass(new THREE.RenderPass(scene, camera));
  const bloom = new THREE.UnrealBloomPass(new THREE.Vector2(1, 1), 0.62, 0.6, 0.82);
  composer.addPass(bloom);
  let bloomOn = true;

  function setSize(w, h) {
    renderer.setSize(w, h);
    composer.setSize(w, h);
    bloom.resolution.set(w, h);
    camera.aspect = w / h; camera.updateProjectionMatrix();
  }
  setSize(container.clientWidth || 1, container.clientHeight || 1);

  return {
    renderer, scene, camera,
    setOrientation(q) {
      targetQ.set(q.x, q.y, q.z, q.w);
      targetQ.premultiply(FRAME).multiply(FRAME_INV);
    },
    render(dt) {
      controls.update();
      lander.quaternion.slerp(targetQ, 1 - Math.exp(-14 * dt));
      earth.rotation.y += dt * 0.03;
      if (bloomOn) composer.render(); else renderer.render(scene, camera);
    },
    resize() { setSize(container.clientWidth, container.clientHeight); },
    setQuality(tier) {
      const high = tier === 'high';
      renderer.setPixelRatio(high ? Math.min(window.devicePixelRatio, 2) : 1);
      renderer.shadowMap.enabled = high;
      sun.castShadow = high; ground.receiveShadow = high;
      bloomOn = true; bloom.strength = high ? 0.62 : 0.4;
      buildStars(high ? 1800 : 520);
      setSize(container.clientWidth, container.clientHeight);
    },
    setBloom(on) { bloomOn = on; },
  };
}
