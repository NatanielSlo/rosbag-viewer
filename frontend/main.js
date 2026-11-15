import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { MapView } from "./map.js";

// ---------- text shown to users (single place, easy to translate) ----------
const MODE_LABELS = {
  OFFBOARD: "Autonomous flight",
  POSCTL: "Manual, position hold",
  STABILIZED: "Manual, stabilized",
  ALTCTL: "Manual, altitude hold",
  "AUTO.LOITER": "Hovering in place",
  "AUTO.LAND": "Landing",
  "AUTO.RTL": "Returning home",
  "AUTO.TAKEOFF": "Taking off",
  "AUTO.MISSION": "Flying a mission",
  MANUAL: "Manual flight",
  ORBIT: "Orbiting a point",
  DESCEND: "Descending",
};
const MODE_COLORS = {
  OFFBOARD: "#4ea1ff", POSCTL: "#f5a524", STABILIZED: "#b388ff",
  ALTCTL: "#ff7ab6", "AUTO.LOITER": "#3ecf8e", "AUTO.LAND": "#ff5c5c",
  "AUTO.RTL": "#2dd4bf", "AUTO.TAKEOFF": "#a3e635", "AUTO.MISSION": "#facc15",
};
const FALLBACK_COLOR = "#8b98a6";
const modeLabel = (m) => MODE_LABELS[m] ?? m ?? "-";
const modeColor = (m) => MODE_COLORS[m] ?? FALLBACK_COLOR;

// ---------- API ----------
const headers = window.VIEWER_TOKEN ? { "X-Viewer-Token": window.VIEWER_TOKEN } : {};
async function api(url) {
  const r = await fetch(url, { headers });
  if (!r.ok) {
    let msg = `Request failed (${r.status})`;
    try { msg = (await r.json()).error ?? msg; } catch { /* not json */ }
    throw new Error(msg);
  }
  return r.json();
}
// <img src> cannot send headers, so frames carry the token as a query parameter
const tokenParam = window.VIEWER_TOKEN ? `&token=${encodeURIComponent(window.VIEWER_TOKEN)}` : "";
const frameUrl = (path, topic, t) =>
  `/api/frame?path=${encodeURIComponent(path)}&topic=${encodeURIComponent(topic)}&t=${t}${tokenParam}`;

// ---------- DOM ----------
const $ = (id) => document.getElementById(id);
const el = {
  list: $("bag-list"), banner: $("banner"), empty: $("empty"), hud: $("hud"),
  cams: $("cams"), views: $("view-buttons"), controls: $("controls"),
  play: $("play"), speed: $("speed"), scrub: $("scrub"), clock: $("clock"),
  modeBar: $("mode-bar"), legend: $("legend"), lioWrap: $("lio-wrap"),
  lioToggle: $("lio-toggle"),
  map: $("map"), mapBar: $("map-bar"), photoWrap: $("photo-wrap"), alignWrap: $("align-wrap"),
  photo: $("photo-toggle"), align: $("align-toggle"),
};
const fmtTime = (s) => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, "0")}`;

// ---------- three.js scene ----------
const canvas = $("scene");
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
const scene = new THREE.Scene();
scene.background = new THREE.Color("#0b1016");
const camera = new THREE.PerspectiveCamera(55, 1, 0.05, 2000);
camera.position.set(14, 12, 14);
const controls = new OrbitControls(camera, canvas);
controls.enableDamping = true;
scene.add(new THREE.HemisphereLight(0xffffff, 0x223344, 1.4));
const sun = new THREE.DirectionalLight(0xffffff, 1.2);
sun.position.set(5, 12, 4);
scene.add(sun);

// Everything lives in ENU (x=East, y=North, z=Up) inside `world`; rotating by
// -90 deg about X maps it to three.js' Y-up space. No per-point conversion.
const world = new THREE.Group();
world.rotation.x = -Math.PI / 2;
scene.add(world);
const dynamic = new THREE.Group(); // everything rebuilt per bag
world.add(dynamic);

function resize() {
  const r = canvas.parentElement.getBoundingClientRect();
  renderer.setSize(r.width, r.height, false);
  camera.aspect = r.width / Math.max(r.height, 1);
  camera.updateProjectionMatrix();
}
new ResizeObserver(resize).observe(canvas.parentElement);

function makeDrone() {
  const g = new THREE.Group();
  const mat = new THREE.MeshStandardMaterial({ color: 0xe6edf3, roughness: 0.5 });
  const dark = new THREE.MeshStandardMaterial({ color: 0x20262e, roughness: 0.7 });
  g.add(new THREE.Mesh(new THREE.BoxGeometry(0.28, 0.2, 0.07), mat));
  for (const [x, y] of [[1, 1], [1, -1], [-1, 1], [-1, -1]]) {
    const arm = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.025, 0.02), dark);
    arm.position.set(x * 0.15, y * 0.15, 0);
    arm.rotation.z = Math.atan2(y, x);
    g.add(arm);
    const rotor = new THREE.Mesh(new THREE.CylinderGeometry(0.1, 0.1, 0.01, 20), dark);
    rotor.rotation.x = Math.PI / 2;
    rotor.position.set(x * 0.27, y * 0.27, 0.03);
    g.add(rotor);
  }
  // nose cone so the heading is obvious (body +X = forward)
  const nose = new THREE.Mesh(new THREE.ConeGeometry(0.06, 0.2, 12),
    new THREE.MeshStandardMaterial({ color: 0xff5c5c }));
  nose.rotation.z = -Math.PI / 2;
  nose.position.x = 0.24;
  g.add(nose);
  g.scale.setScalar(1.6);
  return g;
}
const drone = makeDrone();

// ---------- state ----------
const S = {
  bags: [], flight: null, path: null, t: 0, playing: false, speed: 1,
  view: "free", trail: null, ghost: null, lio: null, cam: {},
  mode: "scene", map: null, config: null,   // "scene" (three.js) or "map" (Cesium)
};

// ---------- helpers ----------
function lowerBound(arr, x) { // first index with arr[i] >= x
  let lo = 0, hi = arr.length;
  while (lo < hi) { const m = (lo + hi) >> 1; if (arr[m] < x) lo = m + 1; else hi = m; }
  return lo;
}
function lastChangeAt(list, t) { // last {t,value} with item.t <= t
  let r = null;
  for (const it of list) { if (it.t <= t) r = it; else break; }
  return r;
}
function samplePose(p, t) {
  const n = p.t.length;
  if (n === 0) return null;
  const i = Math.min(Math.max(lowerBound(p.t, t), 1), n - 1);
  const a = i - 1, b = n === 1 ? 0 : i;
  const span = p.t[b] - p.t[a];
  const k = span > 0 ? Math.min(Math.max((t - p.t[a]) / span, 0), 1) : 0;
  const lerp = (arr) => arr[a] + (arr[b] - arr[a]) * k;
  const qa = new THREE.Quaternion(p.qx[a], p.qy[a], p.qz[a], p.qw[a]);
  const qb = new THREE.Quaternion(p.qx[b], p.qy[b], p.qz[b], p.qw[b]);
  return { pos: new THREE.Vector3(lerp(p.x), lerp(p.y), lerp(p.z)), quat: qa.slerp(qb, k) };
}
function nearestIdx(times, t) {
  const i = lowerBound(times, t);
  if (i === 0) return 0;
  if (i >= times.length) return times.length - 1;
  return t - times[i - 1] <= times[i] - t ? i - 1 : i;
}
function showBanner(text, isErr = false) {
  el.banner.textContent = text;
  el.banner.classList.toggle("err", isErr);
  el.banner.hidden = !text;
}

// ---------- building the scene for a bag ----------
function disposeGroup(g) {
  for (const o of [...g.children]) {
    g.remove(o);
    o.traverse?.((c) => { c.geometry?.dispose(); c.material?.dispose?.(); });
  }
}

function lineFromSeries(p, colorFn, opacity = 1) {
  const n = p.t.length;
  const pos = new Float32Array(n * 3), col = new Float32Array(n * 3);
  const c = new THREE.Color();
  for (let i = 0; i < n; i++) {
    pos.set([p.x[i], p.y[i], p.z[i]], i * 3);
    c.set(colorFn(p.t[i]));
    col.set([c.r, c.g, c.b], i * 3);
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  geo.setAttribute("color", new THREE.BufferAttribute(col, 3));
  const mat = new THREE.LineBasicMaterial({ vertexColors: true, transparent: opacity < 1, opacity });
  return new THREE.Line(geo, mat);
}

function buildScene(f) {
  disposeGroup(dynamic);
  S.trail = S.ghost = S.lio = null;
  const p = f.pose;
  if (!p || p.t.length === 0) return;

  // extents -> grid size and camera framing
  const min = new THREE.Vector3(Math.min(...p.x), Math.min(...p.y), Math.min(...p.z));
  const max = new THREE.Vector3(Math.max(...p.x), Math.max(...p.y), Math.max(...p.z));
  const center = min.clone().add(max).multiplyScalar(0.5);
  const radius = Math.max(max.x - min.x, max.y - min.y, max.z - min.z, 4);
  const gridSize = Math.ceil((radius * 2.2) / 2) * 2 + 4;

  const grid = new THREE.GridHelper(gridSize, gridSize, 0x3a4656, 0x222b37);
  grid.rotation.x = Math.PI / 2; // GridHelper is XZ; we need the XY (ground) plane in ENU
  grid.position.set(center.x, center.y, 0);
  dynamic.add(grid);
  dynamic.add(new THREE.AxesHelper(1.5));
  addCompass(center, gridSize / 2);

  const modes = f.modes;
  const colorAt = (t) => modeColor(lastChangeAt(modes, t)?.value);
  S.ghost = lineFromSeries(p, colorAt, 0.25);
  dynamic.add(S.ghost);
  S.trail = lineFromSeries(p, colorAt, 1);
  S.trail.geometry.setDrawRange(0, 0);
  dynamic.add(S.trail);

  // start marker
  const start = new THREE.Mesh(new THREE.RingGeometry(0.25, 0.4, 28),
    new THREE.MeshBasicMaterial({ color: 0x3ecf8e, side: THREE.DoubleSide }));
  start.position.set(p.x[0], p.y[0], 0.01);
  dynamic.add(start);

  if (f.setpoint?.t.length) { // where the autopilot was told to go
    const sp = new THREE.Mesh(new THREE.SphereGeometry(0.12, 12, 12),
      new THREE.MeshBasicMaterial({ color: 0xfacc15 }));
    sp.name = "setpoint";
    dynamic.add(sp);
  }
  if (f.lio?.t.length) {
    S.lio = lineFromSeries(f.lio, () => "#ff7ab6", 0.9);
    S.lio.visible = el.lioToggle.checked;
    dynamic.add(S.lio);
  }
  dynamic.add(drone);

  controls.target.set(center.x, center.z, -center.y); // ENU -> three space
  camera.position.set(center.x + radius * 0.9, radius * 0.8 + 2, -center.y + radius * 1.1);
  camera.far = Math.max(2000, radius * 40);
  camera.updateProjectionMatrix();
  controls.update();
}

function addCompass(center, r) {
  const mk = (txt, x, y) => {
    const c = document.createElement("canvas");
    c.width = c.height = 128;
    const g = c.getContext("2d");
    g.fillStyle = "#8b98a6"; g.font = "bold 84px system-ui";
    g.textAlign = "center"; g.textBaseline = "middle"; g.fillText(txt, 64, 70);
    const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: new THREE.CanvasTexture(c) }));
    s.scale.setScalar(1.4);
    s.position.set(center.x + x, center.y + y, 0.2);
    dynamic.add(s);
  };
  mk("N", 0, r); mk("S", 0, -r); mk("E", r, 0); mk("W", -r, 0);
}

// ---------- timeline / legend ----------
function buildTimeline(f) {
  el.modeBar.replaceChildren();
  el.legend.replaceChildren();
  const seen = new Set();
  f.modes.forEach((m, i) => {
    const end = f.modes[i + 1]?.t ?? f.duration;
    const s = document.createElement("span");
    s.style.left = `${(m.t / f.duration) * 100}%`;
    s.style.width = `${((end - m.t) / f.duration) * 100}%`;
    s.style.background = modeColor(m.value);
    s.title = `${modeLabel(m.value)} (${fmtTime(m.t)})`;
    el.modeBar.append(s);
    seen.add(m.value);
  });
  for (const m of seen) {
    const d = document.createElement("span");
    const sw = document.createElement("i");
    sw.style.background = modeColor(m);
    d.append(sw, modeLabel(m));
    el.legend.append(d);
  }
}

function buildCameras(f, path) {
  el.cams.replaceChildren();
  S.cam = {};
  for (const [topic, times] of Object.entries(f.cameras)) {
    if (!times.length) continue;
    const fig = document.createElement("figure");
    const img = document.createElement("img");
    img.alt = topic;
    const cap = document.createElement("figcaption");
    cap.textContent = topic.split("/")[1].replace("_", " ");
    fig.append(img, cap);
    el.cams.append(fig);
    S.cam[topic] = { times, img, idx: -1, want: -1, busy: false, path };
  }
  el.cams.hidden = Object.keys(S.cam).length === 0;
}

// ---------- per-frame update ----------
function updateAt(t) {
  const f = S.flight;
  if (!f) return;
  const p = f.pose;
  el.clock.textContent = `${fmtTime(t)} / ${fmtTime(f.duration)}`;
  el.scrub.value = String(Math.round((t / f.duration) * 1000));

  const mode = lastChangeAt(f.modes, t)?.value;
  $("hud-mode").textContent = modeLabel(mode);
  $("hud-mode").style.color = modeColor(mode);
  $("hud-status").textContent = lastChangeAt(f.status_events, t)?.value ?? "";
  $("hud-course").textContent = lastChangeAt(f.course, t)?.value ?? "-";

  if (p?.t.length) {
    const s = samplePose(p, t);
    drone.position.copy(s.pos);
    drone.quaternion.copy(s.quat);
    $("hud-alt").textContent = `${s.pos.z.toFixed(1)} m`;
    $("hud-dist").textContent =
      `${Math.hypot(s.pos.x - p.x[0], s.pos.y - p.y[0]).toFixed(1)} m`;
    if (S.trail) S.trail.geometry.setDrawRange(0, Math.max(lowerBound(p.t, t), 1));
    $("hud-speed").textContent = `${speedAt(f, t).toFixed(1)} m/s`;

    const sp = dynamic.getObjectByName("setpoint");
    if (sp && f.setpoint?.t.length) {
      const q = samplePose(f.setpoint, t);
      if (q) sp.position.copy(q.pos);
    }
    if (S.mode === "scene") {
      if (S.view === "follow") followCamera(s.pos);
      if (S.view === "top") topCamera(s.pos);
    } else if (S.map?.ready) {
      // heading of the nose in ENU, counter-clockwise from East
      const fx = 1 - 2 * (s.quat.y ** 2 + s.quat.z ** 2);
      const fy = 2 * (s.quat.x * s.quat.y + s.quat.z * s.quat.w);
      S.map.update(t, Math.atan2(fy, fx));
    }
  } else if (S.mode === "map" && S.map?.ready) {
    S.map.update(t, 0);
  }

  const b = f.battery;
  if (b?.t.length) {
    const i = Math.max(lowerBound(b.t, t) - 1, 0);
    const pct = b.percentage[i];
    $("hud-bat").textContent = `${pct == null ? "?" : Math.round(pct) + "%"} (${b.voltage[i].toFixed(1)} V)`;
  } else $("hud-bat").textContent = "-";

  updateDetections(f, t);
  updateCameras(t);
}

function speedAt(f, t) {
  const v = f.velocity;
  if (v?.t.length) return v.speed[nearestIdx(v.t, t)];
  const p = f.pose; // fall back to finite difference over ~0.5 s
  const a = samplePose(p, t - 0.25), b = samplePose(p, t + 0.25);
  return a && b ? Math.hypot(b.pos.x - a.pos.x, b.pos.y - a.pos.y) / 0.5 : 0;
}

function updateDetections(f, t) {
  const box = $("hud-det");
  const ev = f.detections?.events.filter((e) => Math.abs(e.t - t) < 1.0) ?? [];
  if (!ev.length) { box.hidden = true; return; }
  const labels = ev.flatMap((e) => e.detections.map((d) =>
    d.class_name ?? d.label ?? d.name ?? d.cls ?? "object"));
  box.textContent = `Detected: ${[...new Set(labels)].join(", ")}`;
  box.hidden = false;
}

// At most one request per camera at a time; while one is loading we only
// remember the newest wanted frame ("latest wins"). This keeps the server from
// being flooded when playing fast or dragging the slider.
function updateCameras(t) {
  for (const [topic, c] of Object.entries(S.cam)) {
    c.want = nearestIdx(c.times, t);
    pumpCamera(topic, c);
  }
}
function pumpCamera(topic, c) {
  if (c.busy || c.want === c.idx) return;
  c.busy = true;
  c.idx = c.want;
  const done = () => {
    c.busy = false;
    if (S.cam[topic] === c) pumpCamera(topic, c); // skip if the bag was switched
  };
  c.img.onload = done;
  c.img.onerror = done;
  c.img.src = frameUrl(c.path, topic, (c.times[c.idx] - 0.001).toFixed(3));
}

// ---------- camera modes ----------
const tmp = new THREE.Vector3();
function toThree(v) { return tmp.set(v.x, v.z, -v.y); } // ENU -> three space
function followCamera(pos) {
  const target = toThree(pos).clone();
  const delta = target.clone().sub(controls.target);
  controls.target.copy(target);
  camera.position.add(delta);
}
function topCamera(pos) {
  const target = toThree(pos).clone();
  controls.target.copy(target);
  const h = Math.max(camera.position.y - target.y, 12);
  camera.position.set(target.x, target.y + h, target.z + 0.001);
}
function setView(v) {
  S.view = v;
  if (S.mode === "map") S.map?.setView(v);
  document.querySelectorAll("#view-buttons button").forEach((b) =>
    b.classList.toggle("on", b.dataset.view === v));
  controls.enableRotate = v !== "top";
  if (v === "top") camera.up.set(0, 0, -1); // north is up on screen
  else camera.up.set(0, 1, 0);
  updateAt(S.t);
}
document.querySelectorAll("#view-buttons button").forEach((b) =>
  b.addEventListener("click", () => setView(b.dataset.view)));
el.lioToggle.addEventListener("change", () => { if (S.lio) S.lio.visible = el.lioToggle.checked; });

// ---------- scene / map switch ----------
async function setMode(mode) {
  if (!S.flight) return;
  document.querySelectorAll("#map-bar [data-mode]").forEach((b) =>
    b.classList.toggle("on", b.dataset.mode === mode));
  if (mode === "scene") {
    S.mode = "scene";
    S.map?.hide();
    canvas.style.visibility = "visible";
    el.photoWrap.hidden = el.alignWrap.hidden = true;
    return;
  }
  showBanner("Loading map...");
  try {
    S.map ??= new MapView(el.map, S.config ?? {}, modeColor);
    const note = await S.map.setFlight(S.flight);
    S.mode = "map";
    canvas.style.visibility = "hidden";
    S.map.show();
    el.photoWrap.hidden = el.alignWrap.hidden = false;
    S.map.setView(S.view);
    showBanner([S.flight.warnings.join(" "), note].filter(Boolean).join(" "));
    updateAt(S.t);
  } catch (err) {
    showBanner(err.message, true);
    setMode("scene");
  }
}
document.querySelectorAll("#map-bar [data-mode]").forEach((b) =>
  b.addEventListener("click", () => setMode(b.dataset.mode)));
el.photo.addEventListener("change", async () => {
  try { await S.map?.setPhotoreal(el.photo.checked); }
  catch (err) { el.photo.checked = false; showBanner(err.message, true); }
});
el.align.addEventListener("change", async () => {
  const note = await S.map?.setAlign(el.align.checked);
  showBanner([S.flight?.warnings.join(" "), note].filter(Boolean).join(" "));
});

// ---------- playback ----------
el.play.addEventListener("click", () => {
  if (!S.flight) return;
  if (!S.playing && S.t >= S.flight.duration - 0.05) S.t = 0;
  S.playing = !S.playing;
  el.play.textContent = S.playing ? "Pause" : "Play";
});
el.speed.addEventListener("change", () => { S.speed = Number(el.speed.value); });
el.scrub.addEventListener("input", () => {
  if (!S.flight) return;
  S.t = (Number(el.scrub.value) / 1000) * S.flight.duration;
  updateAt(S.t);
});
addEventListener("keydown", (e) => {
  if (e.code === "Space" && e.target === document.body) { e.preventDefault(); el.play.click(); }
});

let prev = performance.now();
function loop(now) {
  const dt = (now - prev) / 1000;
  prev = now;
  if (S.playing && S.flight) {
    S.t = Math.min(S.t + dt * S.speed, S.flight.duration);
    if (S.t >= S.flight.duration) { S.playing = false; el.play.textContent = "Play"; }
    updateAt(S.t);
  }
  if (S.mode === "scene") {
    controls.update();
    renderer.render(scene, camera);
  }
  requestAnimationFrame(loop);
}

// ---------- loading ----------
let openSeq = 0; // only the most recently clicked recording may take over the view
async function openBag(b, button) {
  const seq = ++openSeq;
  document.querySelectorAll("#bag-list button").forEach((x) => x.classList.remove("sel"));
  button.classList.add("sel");
  S.playing = false; el.play.textContent = "Play";
  showBanner("Loading recording...");
  try {
    const f = await api(`/api/flight?path=${encodeURIComponent(b.path)}`);
    if (seq !== openSeq) return; // another recording was clicked meanwhile
    f.path = b.path;
    S.flight = f; S.t = 0;
    el.empty.hidden = true;
    const warn = f.warnings.join(" ");
    showBanner(warn);
    const hasPath = !!f.pose?.t.length;
    buildScene(f);
    buildTimeline(f);
    buildCameras(f, b.path);
    el.hud.hidden = false; el.controls.hidden = false; el.views.hidden = !hasPath;
    el.lioWrap.hidden = !f.lio?.t.length;
    el.lioToggle.checked = false;
    drone.visible = hasPath;
    S.mode = "scene";
    S.map?.hide();
    canvas.style.visibility = "visible";
    el.mapBar.hidden = !f.geo?.t.length;
    el.photoWrap.hidden = el.alignWrap.hidden = true;
    document.querySelectorAll("#map-bar [data-mode]").forEach((b) =>
      b.classList.toggle("on", b.dataset.mode === "scene"));
    el.photo.checked = false;
    S.map?.setPhotoreal(false).catch(() => {}); // keep the map in sync with the checkbox
    setView("free");
    updateAt(0);
  } catch (err) {
    if (seq !== openSeq) return;
    S.flight = null;
    el.empty.hidden = false;
    el.hud.hidden = el.controls.hidden = el.views.hidden = el.cams.hidden = true;
    el.mapBar.hidden = true;
    disposeGroup(dynamic);
    showBanner(err.message, true);
  }
}

async function init() {
  resize();
  requestAnimationFrame(loop);
  try { S.config = await api("/api/config"); } catch { S.config = {}; }
  try {
    const { bags } = await api("/api/bags");
    S.bags = bags;
    for (const b of bags) {
      const li = document.createElement("li");
      const btn = document.createElement("button");
      const name = document.createElement("span");
      name.className = "bag-name"; name.textContent = b.name;
      const meta = document.createElement("span");
      if (b.ok) {
        meta.className = "bag-meta";
        meta.textContent = `${fmtTime(b.duration)} · ${b.has_gps ? "GPS" : b.has_pose ? "with position" : "no position"}`;
      } else {
        meta.className = "bag-err"; meta.textContent = b.error;
        btn.disabled = true;
      }
      btn.append(name, meta);
      btn.addEventListener("click", () => openBag(b, btn));
      li.append(btn);
      el.list.append(li);
      b.button = btn;
    }
    // Deep link: ?bag=<folder name>&t=<seconds>
    const q = new URLSearchParams(location.search);
    const wanted = bags.find((x) => x.name === q.get("bag") && x.ok);
    if (wanted) {
      await openBag(wanted, wanted.button);
      if (q.has("t") && S.flight) { S.t = Math.min(Number(q.get("t")), S.flight.duration); updateAt(S.t); }
    }
    if (!bags.length) el.empty.textContent = "No recordings found in the bags folder.";
  } catch (err) {
    showBanner(err.message, true);
  }
}
init();
