// CesiumJS map view for flights that have a GPS track (flight.geo).
//
// Cesium is loaded lazily (only when such a flight is opened) from the URL the
// server gives in /api/config: a pinned CDN version, or /vendor/cesium/ if a
// copy was vendored there. Google Photorealistic 3D Tiles are streamed through
// Cesium ion (asset 2275207), so only the ion token is needed.

const GOOGLE_PHOTOREALISTIC_ION_ASSET = 2275207;
const FOLLOW_RANGE_M = 35;
const DRONE_SCALE = 3;

let cesiumPromise = null;
function loadCesium(base) {
  if (window.Cesium) return Promise.resolve(window.Cesium);
  if (!cesiumPromise) {
    window.CESIUM_BASE_URL = base;
    cesiumPromise = new Promise((resolve, reject) => {
      const css = document.createElement("link");
      css.rel = "stylesheet";
      css.href = base + "Widgets/widgets.css";
      document.head.append(css);
      const s = document.createElement("script");
      s.src = base + "Cesium.js";
      s.onload = () => resolve(window.Cesium);
      s.onerror = () => {
        cesiumPromise = null;
        reject(new Error("Could not load the map library. The map needs an internet connection."));
      };
      document.head.append(s);
    });
  }
  return cesiumPromise;
}

function lowerBound(arr, x) {
  let lo = 0, hi = arr.length;
  while (lo < hi) { const m = (lo + hi) >> 1; if (arr[m] < x) lo = m + 1; else hi = m; }
  return lo;
}

export class MapView {
  /** @param container element for the Cesium viewer
   *  @param config    {cesium_ion_token, cesium_base} from /api/config
   *  @param modeColor function(modeName) -> css color */
  constructor(container, config, modeColor) {
    this.container = container;
    this.config = config;
    this.modeColor = modeColor;
    this.viewer = null;
    this.flight = null;
    this.heights = [];        // aligned ellipsoid heights, one per geo sample
    this.align = true;
    this.photoreal = false;
    this.view = "free";
    this.trail = [];
    this.google = null;
    this.osm = null;
    this.ready = false;
    this.token = config.cesium_ion_token || "";
  }

  async init() {
    if (this.viewer) return;
    const C = await loadCesium(this.config.cesium_base);
    if (this.token) C.Ion.defaultAccessToken = this.token;
    const opts = {
      baseLayerPicker: false, geocoder: false, homeButton: false, sceneModePicker: false,
      navigationHelpButton: false, animation: false, timeline: false, fullscreenButton: false,
      infoBox: false, selectionIndicator: false, shouldAnimate: false,
    };
    if (this.token) {
      this.terrain = C.Terrain.fromWorldTerrain();   // loads asynchronously
      opts.terrain = this.terrain;
    } else {
      // no ion token: plain OpenStreetMap on a flat globe
      opts.baseLayer = new C.ImageryLayer(
        new C.OpenStreetMapImageryProvider({ url: "https://tile.openstreetmap.org/" }));
    }
    this.viewer = new C.Viewer(this.container, opts);
    this.viewer.scene.globe.depthTestAgainstTerrain = true;
  }

  /** Load a flight onto the map. Returns a short note for the user (or ""). */
  async setFlight(flight) {
    await this.init();
    const C = window.Cesium;
    this.flight = flight;
    this.ready = false;
    this.viewer.entities.removeAll();
    this.trail = [];
    this.lastPos = null;
    const g = flight.geo;
    if (!g || !g.t.length) return "This recording has no GPS track.";

    this.groundHeight = await this.sampleGround(g.lon[0], g.lat[0]);
    this.buildEntities();
    this.ready = true;
    if (this.view === "follow") this.setView("follow"); else this.flyToFlight();
    if (this.photoreal) await this.setPhotoreal(true);
    else await this.ensureOsmBuildings();
    return this.note();
  }

  note() {
    if (!this.token) return "No Cesium ion token set: showing a flat OpenStreetMap.";
    if (!this.align) return "";
    if (this.groundHeight === null) {
      return "Could not read the ground height, so the raw GPS altitude is shown.";
    }
    const sh = this.offset;
    return `Takeoff placed on the ground (${this.groundHeight.toFixed(0)} m); ` +
      `flight shifted by ${sh >= 0 ? "+" : ""}${sh.toFixed(0)} m.`;
  }

  /** Wait until the world terrain has finished loading (it is created
   *  asynchronously; sampling before that silently returns height 0). */
  async terrainReady(timeoutMs = 15000) {
    const t = this.terrain;
    if (!t) return true;                       // flat globe, nothing to wait for
    if (t.provider) return true;
    return new Promise((resolve) => {
      const done = (ok) => { clearTimeout(timer); resolve(ok); };
      const timer = setTimeout(() => done(false), timeoutMs);
      t.readyEvent.addEventListener(() => done(true));
      t.errorEvent.addEventListener(() => done(false));
    });
  }

  /** Ground height (ellipsoid) at lon/lat, or null if it could not be read. */
  async sampleGround(lon, lat) {
    const C = window.Cesium;
    try {
      if (!(await this.terrainReady())) return null;
      const provider = this.terrain ? this.terrain.provider : this.viewer.terrainProvider;
      const [p] = await C.sampleTerrainMostDetailed(provider, [C.Cartographic.fromDegrees(lon, lat)]);
      return Number.isFinite(p.height) ? p.height : null;
    } catch { return null; }
  }

  computeHeights() {
    const g = this.flight.geo;
    // Cesium works in ellipsoid height. With "align", the takeoff point is put
    // on the map's ground and the rest follows the flown altitude difference,
    // which keeps simulated / uncalibrated altitudes from ending up underground.
    const alt = g.alt_ellipsoid;
    const canAlign = this.align && this.groundHeight !== null;
    this.offset = canAlign ? this.groundHeight - alt[0] : 0;
    this.heights = alt.map((a) => a + this.offset);
  }

  positionAt(i) {
    const C = window.Cesium, g = this.flight.geo;
    return C.Cartesian3.fromDegrees(g.lon[i], g.lat[i], this.heights[i]);
  }

  buildEntities() {
    const C = window.Cesium, v = this.viewer, f = this.flight, g = f.geo;
    v.entities.removeAll();
    this.computeHeights();

    // the route: one line that grows with the playback time
    this.trail = [];
    v.entities.add({ polyline: {
      positions: new C.CallbackProperty(() => this.trail, false), width: 5,
      material: C.Color.fromCssColorString("#4ea1ff") } });

    // takeoff marker
    v.entities.add({ position: this.positionAt(0),
      point: { pixelSize: 10, color: C.Color.fromCssColorString("#3ecf8e"),
        outlineColor: C.Color.WHITE, outlineWidth: 2,
        disableDepthTestDistance: Number.POSITIVE_INFINITY } });

    // the drone: a small quadcopter built from primitives that turns with the
    // heading (drawn ~3x life size so it stays visible from a distance), plus a
    // dot that stays visible from afar
    this.drone = v.entities.add({
      position: this.positionAt(0),
      point: { pixelSize: 8, color: C.Color.fromCssColorString("#ff5c5c"),
        outlineColor: C.Color.WHITE, outlineWidth: 2,
        disableDepthTestDistance: Number.POSITIVE_INFINITY },
    });
    this.buildDroneParts();
  }

  buildDroneParts() {
    const C = window.Cesium, v = this.viewer, K = DRONE_SCALE;
    const dark = C.Color.fromCssColorString("#2a2f36");
    const rotor = C.Color.fromCssColorString("#9fb4c8").withAlpha(0.55);
    const red = C.Color.fromCssColorString("#ff5c5c");
    const white = C.Color.fromCssColorString("#e6edf3");
    // offsets are x forward, y left, z up (metres, before scaling); yaw is the part's own turn
    const parts = [];
    const box = (off, dims, mat, yaw = 0) => parts.push({ off, yaw, mat,
      geom: { box: { dimensions: new C.Cartesian3(dims[0] * K, dims[1] * K, dims[2] * K), material: mat } } });
    box([0, 0, 0], [0.30, 0.20, 0.10], white);                   // body
    box([0.20, 0, 0.01], [0.10, 0.08, 0.06], red);               // front marker
    box([0, 0, 0.0], [0.80, 0.045, 0.035], dark, Math.PI / 4);   // arm
    box([0, 0, 0.0], [0.80, 0.045, 0.035], dark, -Math.PI / 4);  // arm
    const a = 0.28;
    for (const [x, y] of [[a, a], [a, -a], [-a, a], [-a, -a]]) {
      parts.push({ off: [x, y, 0.045], yaw: 0, mat: rotor, geom: { cylinder: {
        length: 0.012 * K, topRadius: 0.17 * K, bottomRadius: 0.17 * K, material: rotor } } });
      box([x, y, 0.02], [0.05, 0.05, 0.05], dark);               // motor
    }
    this.parts = parts.map((p) => {
      const e = v.entities.add({ position: this.positionAt(0), ...p.geom });
      return { ...p, e };
    });
  }

  placeDrone(pos, yawEnu) {
    const C = window.Cesium, K = DRONE_SCALE;
    this.drone.position = pos;
    const enu = C.Transforms.eastNorthUpToFixedFrame(pos);
    const c = Math.cos(yawEnu), s = Math.sin(yawEnu);
    for (const p of this.parts) {
      const [fx, fy, fz] = p.off;
      const local = new C.Cartesian3((c * fx - s * fy) * K, (s * fx + c * fy) * K, fz * K);
      const at = C.Matrix4.multiplyByPoint(enu, local, new C.Cartesian3());
      p.e.position = at;
      p.e.orientation = C.Transforms.headingPitchRollQuaternion(
        at, new C.HeadingPitchRoll(-(yawEnu + p.yaw), 0, 0));
    }
  }

  /** Third-person chase camera: behind and above the drone, looking along its heading. */
  followCamera(pos, yawEnu) {
    const C = window.Cesium, cam = this.viewer.camera;
    const want = Math.PI / 2 - yawEnu;                    // compass heading (clockwise from north)
    if (this.camHeading === undefined) this.camHeading = want;
    let d = want - this.camHeading;
    d = Math.atan2(Math.sin(d), Math.cos(d));
    this.camHeading += d * 0.12;                          // smooth the turns
    cam.lookAt(pos, new C.HeadingPitchRange(this.camHeading, C.Math.toRadians(-18), FOLLOW_RANGE_M));
    cam.lookAtTransform(C.Matrix4.IDENTITY);
  }

  flyToFlight() {
    const C = window.Cesium, pts = [];
    for (let i = 0; i < this.flight.geo.t.length; i += 3) pts.push(this.positionAt(i));
    const bs = C.BoundingSphere.fromPoints(pts);
    this.viewer.camera.flyToBoundingSphere(bs, {
      duration: 1.5,
      offset: new C.HeadingPitchRange(0, C.Math.toRadians(-40), Math.max(bs.radius * 2.6, 80)),
    });
  }

  /** Called every frame / scrub. `yawEnu` = heading in radians, counter-clockwise from East. */
  update(t, yawEnu = 0) {
    if (!this.ready) return;
    const C = window.Cesium, g = this.flight.geo;
    const n = g.t.length;
    const j = n === 1 ? 0 : Math.min(Math.max(lowerBound(g.t, t), 1), n - 1);
    const i = Math.max(j - 1, 0);
    const span = g.t[j] - g.t[i];
    const k = span > 0 ? Math.min(Math.max((t - g.t[i]) / span, 0), 1) : 0;
    const lerp = (a, b) => a + (b - a) * k;
    const pos = C.Cartesian3.fromDegrees(
      lerp(g.lon[i], g.lon[j]), lerp(g.lat[i], g.lat[j]), lerp(this.heights[i], this.heights[j]));
    this.placeDrone(pos, yawEnu);
    this.lastPos = pos; this.lastYaw = yawEnu;
    if (this.view === "follow") this.followCamera(pos, yawEnu);

    const upto = Math.max(lowerBound(g.t, t), 1);
    if (this.trail.length > upto) this.trail = [];
    for (let m = this.trail.length; m < upto; m++) this.trail.push(this.positionAt(m));
    this.trail[this.trail.length - 1] = pos;
  }

  show() {
    this.container.hidden = false;
    this.viewer?.resize();
  }
  hide() {
    this.container.hidden = true;
    this.viewer && (this.viewer.trackedEntity = undefined);
  }

  setView(mode) {
    this.view = mode;
    if (!this.ready) return;
    const C = window.Cesium, v = this.viewer;
    v.trackedEntity = undefined;
    this.camHeading = undefined;
    if (mode === "follow") {
      if (this.lastPos) this.followCamera(this.lastPos, this.lastYaw);
    } else {
      if (mode === "top") {
        const c = C.Cartographic.fromCartesian(this.drone.position.getValue(C.JulianDate.now()));
        v.camera.setView({
          destination: C.Cartesian3.fromRadians(c.longitude, c.latitude, c.height + 150),
          orientation: { heading: 0, pitch: -C.Math.PI_OVER_TWO, roll: 0 },
        });
      }
    }
  }

  async setAlign(on) {
    this.align = on;
    if (this.ready) { this.buildEntities(); }
    return this.note();
  }

  async ensureOsmBuildings() {
    if (this.osm || !this.token) return;
    try {
      this.osm = await window.Cesium.createOsmBuildingsAsync();
      this.viewer.scene.primitives.add(this.osm);
    } catch { /* optional layer */ }
  }

  /** Google Photorealistic 3D Tiles via Cesium ion. The globe is hidden while
   *  they are on (they already include the terrain). */
  async setPhotoreal(on) {
    this.photoreal = on;
    if (!this.viewer) return;
    const C = window.Cesium;
    if (on && !this.google) {
      if (!this.token) throw new Error("Photorealistic tiles need a Cesium ion token (.env.local).");
      this.google = await C.Cesium3DTileset.fromIonAssetId(GOOGLE_PHOTOREALISTIC_ION_ASSET);
      this.viewer.scene.primitives.add(this.google);
    }
    if (this.google) this.google.show = on;
    if (this.osm) this.osm.show = !on;
    this.viewer.scene.globe.show = !on;
  }
}
