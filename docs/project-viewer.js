import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { DRACOLoader } from "three/addons/loaders/DRACOLoader.js";

const BASE_URL = new URL(".", import.meta.url).href;

const dracoLoader = new DRACOLoader();
dracoLoader.setDecoderPath(new URL("vendor/libs/draco/gltf/", BASE_URL).href);
dracoLoader.preload();

/* Camera / rotation tuning for the ESP32-DIV v2 PCB model */
const PROFILE = {
  baseX: -Math.PI / 2,
  baseY: 0,
  baseZ: 0,
  rotX: 1.5,
  rotY: 0,
  fitScale: 0.5,
  zoom: 1.1,
  camera: [0.8, 0.28, 0.98],
};

const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

document.querySelectorAll("[data-model]").forEach((root) => {
  const canvas = root.querySelector("canvas");
  if (!canvas) return;
  root.__viewer = createViewer(root, canvas, new URL(root.dataset.model, BASE_URL).href);
});

/* Hero model switcher (v1 / v2) */
const modelSwitch = document.getElementById("model-switch");
if (modelSwitch) {
  modelSwitch.addEventListener("click", (e) => {
    const btn = e.target.closest("[data-model-src]");
    if (!btn || btn.classList.contains("is-active")) return;
    const root = document.getElementById(modelSwitch.dataset.target || "hero-viewer");
    if (!root || !root.__viewer) return;
    modelSwitch.querySelectorAll("button").forEach((b) =>
      b.classList.toggle("is-active", b === btn)
    );
    root.__viewer.load(new URL(btn.dataset.modelSrc, BASE_URL).href);
  });
}

function createViewer(root, canvas, url) {
  const status = root.querySelector(".viewer__status");

  const renderer = new THREE.WebGLRenderer({
    canvas,
    antialias: true,
    alpha: true,
    powerPreference: "high-performance",
  });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.5));
  renderer.outputColorSpace = THREE.SRGBColorSpace;

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(40, 1, 0.001, 5000);

  const key = new THREE.DirectionalLight(0xffffff, 1.35);
  key.position.set(5, 8, 6);
  const fill = new THREE.DirectionalLight(0xaabbff, 0.55);
  fill.position.set(-6, 3, -4);
  const rim = new THREE.DirectionalLight(0xee3a17, 0.45);
  rim.position.set(0, 2, -6);
  scene.add(new THREE.AmbientLight(0xffffff, 0.65), key, fill, rim);

  let model = null;
  let loaded = false;
  let dragging = false;
  let lastX = 0;
  let lastY = 0;
  let rotY = PROFILE.rotY;
  let rotX = PROFILE.rotX;

  function setStatus(text) {
    if (status) status.textContent = text;
  }

  function getSize() {
    const w = root.clientWidth || root.offsetWidth;
    const h = root.clientHeight || root.offsetHeight;
    if (w > 8 && h > 8) return { w, h };
    const fallbackW = Math.min(520, window.innerWidth * 0.9);
    return { w: fallbackW, h: fallbackW * 0.75 };
  }

  function resize() {
    const { w, h } = getSize();
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    if (loaded) renderFrame();
  }

  function fitModel(object) {
    object.rotation.set(PROFILE.baseX, PROFILE.baseY, PROFILE.baseZ);

    const pivot = new THREE.Group();
    const holder = new THREE.Group();
    scene.add(pivot);
    pivot.add(holder);
    holder.add(object);

    holder.updateMatrixWorld(true);
    let box = new THREE.Box3().setFromObject(holder, true);
    if (box.isEmpty()) return;

    const center = box.getCenter(new THREE.Vector3());
    holder.position.sub(center);

    holder.updateMatrixWorld(true);
    box = new THREE.Box3().setFromObject(holder, true);
    const size = box.getSize(new THREE.Vector3());
    const maxDim = Math.max(size.x, size.y, size.z) || 1;

    const fovRad = (camera.fov * Math.PI) / 180;
    const distance =
      ((maxDim * PROFILE.fitScale) / Math.tan(fovRad / 2)) * 1.15 / PROFILE.zoom;

    camera.position.set(
      distance * PROFILE.camera[0],
      distance * PROFILE.camera[1],
      distance * PROFILE.camera[2]
    );
    camera.near = Math.max(0.01, distance / 200);
    camera.far = distance * 200;
    camera.lookAt(0, 0, 0);
    camera.updateProjectionMatrix();

    model = pivot;
  }

  function renderFrame() {
    if (!model) return;
    if (!dragging && !reducedMotion) rotY += 0.005;
    model.rotation.order = "YXZ";
    model.rotation.x = rotX;
    model.rotation.y = rotY;
    renderer.render(scene, camera);
  }

  let visible = true;
  let docVisible = document.visibilityState === "visible";

  function shouldRender() {
    return loaded && visible && docVisible;
  }

  function loop() {
    if (shouldRender()) renderFrame();
    requestAnimationFrame(loop);
  }
  requestAnimationFrame(loop);

  const visibilityObs = new IntersectionObserver(
    ([entry]) => {
      visible = entry.isIntersecting;
    },
    { rootMargin: "80px 0px", threshold: 0 }
  );
  visibilityObs.observe(root);

  document.addEventListener("visibilitychange", () => {
    docVisible = document.visibilityState === "visible";
  });

  new ResizeObserver(() => requestAnimationFrame(resize)).observe(root);
  resize();

  root.addEventListener("pointerdown", (e) => {
    dragging = true;
    lastX = e.clientX;
    lastY = e.clientY;
    root.setPointerCapture(e.pointerId);
  });

  root.addEventListener("pointermove", (e) => {
    if (!dragging) return;
    rotY += (e.clientX - lastX) * 0.008;
    rotX += (e.clientY - lastY) * 0.006;
    rotX = Math.max(-1.1, Math.min(1.6, rotX));
    lastX = e.clientX;
    lastY = e.clientY;
    renderFrame();
  });

  const endDrag = (e) => {
    dragging = false;
    root.releasePointerCapture(e.pointerId);
  };
  root.addEventListener("pointerup", endDrag);
  root.addEventListener("pointercancel", endDrag);

  const loader = new GLTFLoader();
  loader.setDRACOLoader(dracoLoader);

  function clearModel() {
    if (!model) return;
    scene.remove(model);
    model.traverse((o) => {
      if (!o.isMesh) return;
      o.geometry?.dispose();
      const mats = Array.isArray(o.material) ? o.material : [o.material];
      mats.forEach((m) => m?.dispose());
    });
    model = null;
  }

  let loadToken = 0;

  async function loadModel(nextUrl) {
    const token = ++loadToken;
    loaded = false;
    root.classList.remove("is-ready");
    setStatus("Downloading model…");
    try {
      const res = await fetch(nextUrl);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);

      setStatus("Processing model…");
      const buffer = await res.arrayBuffer();
      const gltf = await loader.parseAsync(buffer, nextUrl);
      if (token !== loadToken) return; // a newer load superseded this one

      gltf.scene.traverse((child) => {
        if (!child.isMesh) return;
        const mats = Array.isArray(child.material) ? child.material : [child.material];
        mats.forEach((mat) => {
          if (!mat) return;
          mat.side = THREE.DoubleSide;
          if (mat.map) mat.map.colorSpace = THREE.SRGBColorSpace;
        });
      });

      clearModel();
      fitModel(gltf.scene);
      loaded = true;

      await new Promise((r) => requestAnimationFrame(r));
      resize();
      renderFrame();
      root.classList.add("is-ready");
    } catch (err) {
      if (token !== loadToken) return;
      console.error("[viewer]", err);
      let msg = err?.message || "Load failed";
      if (window.location.protocol === "file:") {
        msg = "Use a local server (not file://)";
      }
      setStatus(`Could not load model — ${msg}`);
    }
  }

  loadModel(url);

  return { load: loadModel };
}
