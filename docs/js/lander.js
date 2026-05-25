// Procedural Blue Ghost lunar lander. Returns a THREE.Group whose local
// origin is the body center (so the IMU quaternion rotates about it). Foot
// pads sit at local y = -1.2; the scene lifts the group so pads rest on y=0.
/* global THREE */

function strut(from, to, r, mat) {
  const dir = new THREE.Vector3().subVectors(to, from);
  const len = dir.length();
  const geo = new THREE.CylinderGeometry(r, r * 1.25, len, 8);
  const m = new THREE.Mesh(geo, mat);
  m.position.copy(from).add(to).multiplyScalar(0.5);
  m.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.clone().normalize());
  m.castShadow = true;
  return m;
}

export function createLander(renderer) {
  const g = new THREE.Group();

  const foil = new THREE.MeshStandardMaterial({
    color: 0xd8b25a, metalness: 0.95, roughness: 0.34,
    emissive: 0x2a1f08, emissiveIntensity: 0.35,
  });
  const black = new THREE.MeshStandardMaterial({ color: 0x14151a, metalness: 0.55, roughness: 0.5 });
  const metal = new THREE.MeshStandardMaterial({ color: 0xb9bdc6, metalness: 0.9, roughness: 0.28 });
  const lime = new THREE.MeshStandardMaterial({
    color: 0x9aca3c, emissive: 0x9aca3c, emissiveIntensity: 0.9, metalness: 0.2, roughness: 0.5,
  });

  // Main body — octagonal foil drum.
  const body = new THREE.Mesh(new THREE.CylinderGeometry(1.05, 1.18, 0.78, 8), foil);
  body.castShadow = true; body.receiveShadow = true;
  g.add(body);

  // Black structural band around the lower body.
  const band = new THREE.Mesh(new THREE.CylinderGeometry(1.2, 1.22, 0.16, 8), black);
  band.position.y = -0.34; band.castShadow = true;
  g.add(band);

  // Top deck + payload blocks.
  const deck = new THREE.Mesh(new THREE.CylinderGeometry(0.92, 0.98, 0.18, 8), black);
  deck.position.y = 0.48; deck.castShadow = true; deck.receiveShadow = true;
  g.add(deck);
  const pay1 = new THREE.Mesh(new THREE.BoxGeometry(0.5, 0.3, 0.5), foil);
  pay1.position.set(-0.3, 0.72, 0.18); pay1.castShadow = true; g.add(pay1);
  const pay2 = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.22, 0.34), lime);
  pay2.position.set(0.34, 0.68, -0.12); pay2.castShadow = true; g.add(pay2);

  // High-gain antenna (mast + dish).
  const mast = new THREE.Mesh(new THREE.CylinderGeometry(0.03, 0.03, 0.8, 6), metal);
  mast.position.set(0.42, 1.05, 0.3); mast.castShadow = true; g.add(mast);
  const dish = new THREE.Mesh(new THREE.SphereGeometry(0.22, 16, 8, 0, Math.PI * 2, 0, Math.PI / 2.4), metal);
  dish.material = metal; dish.position.set(0.42, 1.46, 0.3); dish.rotation.x = -0.7; g.add(dish);

  // Bottom engine nozzle.
  const nozzle = new THREE.Mesh(new THREE.CylinderGeometry(0.18, 0.32, 0.34, 12), metal);
  nozzle.position.y = -0.62; nozzle.castShadow = true; g.add(nozzle);

  // Four landing legs + footpads (local pads at y = -1.2).
  const legGroup = new THREE.Group();
  for (let i = 0; i < 4; i++) {
    const a = Math.PI / 4 + i * Math.PI / 2;
    const dx = Math.cos(a), dz = Math.sin(a);
    const hip = new THREE.Vector3(dx * 0.95, -0.25, dz * 0.95);
    const pad = new THREE.Vector3(dx * 1.85, -1.2, dz * 1.85);
    legGroup.add(strut(hip, pad, 0.05, metal));
    // small secondary brace
    legGroup.add(strut(new THREE.Vector3(dx * 0.6, 0.1, dz * 0.6), new THREE.Vector3(dx * 1.4, -0.8, dz * 1.4), 0.025, metal));
    const foot = new THREE.Mesh(new THREE.CylinderGeometry(0.17, 0.17, 0.07, 14), black);
    foot.position.copy(pad); foot.castShadow = true; foot.receiveShadow = true;
    legGroup.add(foot);
  }
  g.add(legGroup);

  // Firefly logo decal on the +Z hull face. Additive blending → the PNG's
  // black background contributes nothing; only the lime mark glows.
  const tex = new THREE.TextureLoader().load('assets/firefly-logo.png');
  tex.encoding = THREE.sRGBEncoding;
  tex.anisotropy = renderer ? renderer.capabilities.getMaxAnisotropy() : 1;
  const decal = new THREE.Mesh(
    new THREE.PlaneGeometry(0.92, 0.92),
    new THREE.MeshBasicMaterial({ map: tex, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false })
  );
  decal.position.set(0, 0.02, 1.085);
  g.add(decal);

  return g;
}
