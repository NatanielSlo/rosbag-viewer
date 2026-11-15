// 3D map scene of the viewer. The native side (Qt) owns time and data and calls into this
// page: init(token), setPath(segments), setDrone([lon,lat,h,heading]|null), camera(mode), ...
// The page talks back with console messages: "READY", "FPS n", "STATUS text", "SEEK seconds".
let viewer, tileset, droneEntity;
let current = null;                 // [lon, lat, ellipsoid height, heading rad]
let pathEntities = [];
let pathPoints = [];                // [{t, mode, h, cart}] for click and hover
let followMode = false;
// Chase camera: angle relative to the drone's heading (0 = straight behind it), pitch, distance.
const follow = { rel: 0, pitch: Cesium.Math.toRadians(-30), range: 60 };

function notice(text) {
  const n = document.getElementById("notice");
  n.textContent = text; n.style.display = text ? "block" : "none";
}

async function init(token) {
  const hasToken = !!token;
  if (hasToken) Cesium.Ion.defaultAccessToken = token;
  viewer = new Cesium.Viewer("c", {
    baseLayer: false, baseLayerPicker: false, geocoder: false, homeButton: false,
    sceneModePicker: false, navigationHelpButton: false, animation: false, timeline: false,
    fullscreenButton: false, infoBox: false, selectionIndicator: false,
    globe: hasToken ? false : undefined,
    requestRenderMode: false,
  });
  viewer.scene.skyAtmosphere.show = true;

  if (hasToken) {
    try {
      tileset = await Cesium.Cesium3DTileset.fromIonAssetId(2275207);   // Google Photorealistic 3D Tiles
      viewer.scene.primitives.add(tileset);
      console.log("STATUS Google 3D Tiles: loading");
      tileset.allTilesLoaded.addEventListener(() => console.log("STATUS Google 3D Tiles: loaded"));
    } catch (e) {
      notice("3D Tiles failed: " + e.message + " (token wrong or no internet)");
      viewer.scene.globe = new Cesium.Globe(viewer.scene.ellipsoid);
      console.log("STATUS tiles failed");
    }
  } else {
    notice("No Cesium ion token set (CESIUM_ION_TOKEN): showing the bare globe");
    console.log("STATUS no token, bare globe");
  }

  droneEntity = viewer.entities.add({
    show: false,
    position: new Cesium.CallbackProperty(() => cart(current), false),
    orientation: new Cesium.CallbackProperty(() => Cesium.Transforms.headingPitchRollQuaternion(
      cart(current), new Cesium.HeadingPitchRoll(current ? current[3] - Math.PI / 2 : 0, 0, 0)), false),
    box: { dimensions: new Cesium.Cartesian3(3, 1, 0.6), material: Cesium.Color.ORANGE },
  });

  const tip = document.getElementById("tip");
  const handler = new Cesium.ScreenSpaceEventHandler(viewer.canvas);
  handler.setInputAction(m => { const p = nearestPathPoint(m.position); if (p) console.log("SEEK " + p.t); },
                         Cesium.ScreenSpaceEventType.LEFT_CLICK);
  // Following: the left button drags the chase angle, the wheel changes the distance. The
  // normal camera controls are switched off meanwhile; any other camera action leaves follow mode.
  let dragging = false;
  handler.setInputAction(() => { if (followMode) dragging = true; }, Cesium.ScreenSpaceEventType.LEFT_DOWN);
  handler.setInputAction(() => { dragging = false; }, Cesium.ScreenSpaceEventType.LEFT_UP);
  handler.setInputAction(() => { if (followMode) exitFollow(); }, Cesium.ScreenSpaceEventType.RIGHT_DOWN);
  handler.setInputAction(() => { if (followMode) exitFollow(); }, Cesium.ScreenSpaceEventType.MIDDLE_DOWN);
  handler.setInputAction(m => {   // touchpad / touch pinch zooms the chase view too
    if (followMode && m.distance.endPosition.y > 0)
      follow.range = Math.min(Math.max(follow.range * m.distance.startPosition.y / m.distance.endPosition.y, 5), 3000);
  }, Cesium.ScreenSpaceEventType.PINCH_MOVE);
  handler.setInputAction(d => {
    if (followMode) follow.range = Math.min(Math.max(follow.range * (d > 0 ? 0.9 : 1 / 0.9), 5), 3000);
  }, Cesium.ScreenSpaceEventType.WHEEL);
  handler.setInputAction(m => {
    if (followMode && dragging) {
      follow.rel += (m.endPosition.x - m.startPosition.x) * 0.005;
      follow.pitch = Math.min(Math.max(follow.pitch - (m.endPosition.y - m.startPosition.y) * 0.005, -1.5), -0.05);
      return;
    }
    const p = nearestPathPoint(m.endPosition);
    if (!p) { tip.style.display = "none"; return; }
    tip.textContent = "t " + p.t.toFixed(1) + " s\n" + p.mode + "\n" + p.h.toFixed(0) + " m (ellipsoid)";
    tip.style.left = (m.endPosition.x + 14) + "px"; tip.style.top = (m.endPosition.y + 14) + "px";
    tip.style.display = "block";
  }, Cesium.ScreenSpaceEventType.MOUSE_MOVE);

  viewer.camera.setView({
    destination: Cesium.Cartesian3.fromDegrees(11.6710, 48.2615, 700),
    orientation: { heading: 0, pitch: Cesium.Math.toRadians(-35), roll: 0 },
  });

  viewer.scene.preRender.addEventListener(() => {   // keeps the chase view glued to the drone
    if (followMode && current)
      viewer.camera.lookAt(cart(current), new Cesium.HeadingPitchRange(current[3] + follow.rel, follow.pitch, follow.range));
  });

  let frames = 0, last = performance.now();
  viewer.scene.postRender.addEventListener(() => {
    frames++;
    const now = performance.now();
    if (now - last >= 1000) { console.log("FPS " + (frames * 1000 / (now - last)).toFixed(1)); frames = 0; last = now; }
  });
  console.log("READY");
}

function cart(p) {
  return p ? Cesium.Cartesian3.fromDegrees(p[0], p[1], p[2]) : Cesium.Cartesian3.ZERO;
}

// segments: [{mode, color, pos:[lon,lat,h,...], t:[...]}] — one piece of path per flight mode.
function setPath(segments) {
  pathEntities.forEach(e => viewer.entities.remove(e));
  pathEntities = []; pathPoints = [];
  const all = [];
  for (const s of segments) {
    const positions = Cesium.Cartesian3.fromDegreesArrayHeights(s.pos);
    pathEntities.push(viewer.entities.add({
      polyline: { positions, width: 4, material: Cesium.Color.fromCssColorString(s.color) },
    }));
    positions.forEach((c, i) => pathPoints.push({ t: s.t[i], mode: s.mode, h: s.pos[i * 3 + 2], cart: c }));
    all.push(...positions);
  }
  if (all.length) {
    pathEntities.push(viewer.entities.add({     // start / home marker
      position: all[0],
      point: { pixelSize: 10, color: Cesium.Color.WHITE, outlineColor: Cesium.Color.BLACK, outlineWidth: 2 },
      label: { text: "H", font: "bold 14px sans-serif", pixelOffset: new Cesium.Cartesian2(0, -18),
               fillColor: Cesium.Color.WHITE, outlineColor: Cesium.Color.BLACK, outlineWidth: 3,
               style: Cesium.LabelStyle.FILL_AND_OUTLINE },
    }));
    zoomToFlight();
  }
}

function nearestPathPoint(pos) {
  let best = null, bestD = 14;   // pixels
  for (const p of pathPoints) {
    const w = Cesium.SceneTransforms.worldToWindowCoordinates(viewer.scene, p.cart);
    if (!w) continue;
    const d = Math.hypot(w.x - pos.x, w.y - pos.y);
    if (d < bestD) { bestD = d; best = p; }
  }
  return best;
}

function setDrone(p) {
  current = p;
  droneEntity.show = !!p;
}

function setFollow(on) {
  const c = viewer.scene.screenSpaceCameraController;
  followMode = on;
  c.enableRotate = c.enableTilt = c.enableLook = c.enableZoom = !on;   // the page handles drag and wheel
  console.log("CAMERA " + (on ? "follow" : "free"));
}

function exitFollow() {
  if (!followMode) return;
  setFollow(false);
  viewer.camera.lookAtTransform(Cesium.Matrix4.IDENTITY);   // keep the view where it is, hand control back
}

function freeCamera() {
  viewer.camera.cancelFlight();
  exitFollow();
  viewer.camera.lookAtTransform(Cesium.Matrix4.IDENTITY);
}

function zoomToFlight() {
  freeCamera();
  if (!pathPoints.length) return;
  const sphere = Cesium.BoundingSphere.fromPoints(pathPoints.map(p => p.cart));
  viewer.camera.flyToBoundingSphere(sphere, {
    duration: 1.2,
    offset: new Cesium.HeadingPitchRange(0, Cesium.Math.toRadians(-35), Math.max(sphere.radius * 3, 150)),
  });
}

// mode: "follow" | "top" | "reset" | "zoom"
function camera(mode) {
  if (!viewer) return;
  if (mode === "zoom" || mode === "reset") return zoomToFlight();
  if (!current) return;
  const wasFollowing = followMode;
  freeCamera();
  if (mode === "follow" && wasFollowing) return;   // the button toggles: second press stops following
  const target = cart(current);
  if (mode === "top") {
    viewer.camera.flyToBoundingSphere(new Cesium.BoundingSphere(target, 1), {
      duration: 1.0, offset: new Cesium.HeadingPitchRange(0, Cesium.Math.toRadians(-90), 250) });
  } else if (mode === "follow") {
    follow.rel = 0;
    viewer.camera.flyToBoundingSphere(new Cesium.BoundingSphere(target, 1), {
      duration: 1.0,
      offset: new Cesium.HeadingPitchRange(current[3], follow.pitch, follow.range),
      complete: () => setFollow(true),
    });
  }
}

// View keys. In follow mode they change the chosen chase angle, otherwise they move the camera.
function view(action) {
  if (!viewer) return;
  const c = viewer.camera;
  if (followMode) {   // rotate and zoom adjust the chase view; panning leaves follow mode
    if (action === "rotLeft") return void (follow.rel -= 0.1);
    if (action === "rotRight") return void (follow.rel += 0.1);
    if (action === "in") return void (follow.range = Math.max(follow.range * 0.85, 5));
    if (action === "out") return void (follow.range = Math.min(follow.range / 0.85, 3000));
    exitFollow();
  }
  const step = Math.max(c.positionCartographic.height * 0.05, 2);
  switch (action) {
    case "left": c.moveLeft(step); break;
    case "right": c.moveRight(step); break;
    case "up": c.moveForward(step); break;
    case "down": c.moveBackward(step); break;
    case "rotLeft": c.lookLeft(0.1); break;
    case "rotRight": c.lookRight(0.1); break;
    case "in": c.zoomIn(step * 2); break;
    case "out": c.zoomOut(step * 2); break;
  }
}
