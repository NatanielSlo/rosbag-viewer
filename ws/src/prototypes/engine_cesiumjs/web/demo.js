// Scene for prototype B. Native code calls init(token) once and setDrone(...) at 30 Hz.
let viewer, drone, tileset;
let current = { lon: 0, lat: 0, alt: 0, heading: 0 };
const trail = [];

function notice(text) {
  const n = document.getElementById("notice");
  n.textContent = text; n.style.display = text ? "block" : "none";
}

async function init(token) {
  const hasToken = !!token;
  if (hasToken) Cesium.Ion.defaultAccessToken = token;

  viewer = new Cesium.Viewer("c", {
    baseLayer: false,                 // no default imagery: it needs an ion token
    baseLayerPicker: false, geocoder: false, homeButton: false, sceneModePicker: false,
    navigationHelpButton: false, animation: false, timeline: false, fullscreenButton: false,
    infoBox: false, selectionIndicator: false,
    globe: hasToken ? false : undefined,   // Google tiles replace the globe (undefined = default Globe)
    requestRenderMode: false,
  });
  viewer.scene.debugShowFramesPerSecond = false;
  viewer.scene.skyAtmosphere.show = true;

  if (hasToken) {
    try {
      tileset = await Cesium.Cesium3DTileset.fromIonAssetId(2275207);   // Google Photorealistic 3D Tiles
      viewer.scene.primitives.add(tileset);
      console.log("STATUS Google 3D Tiles: loading");
      tileset.allTilesLoaded.addEventListener(() => console.log("STATUS Google 3D Tiles: loaded"));
    } catch (e) {
      notice("3D Tiles failed: " + e.message + " (token wrong or no internet)");
      viewer.scene.globe = new Cesium.Globe(viewer.scene.ellipsoid);   // plain globe instead of black
      console.log("STATUS tiles failed");
    }
  } else {
    notice("No Cesium ion token set (CESIUM_ION_TOKEN): showing the bare globe");
    console.log("STATUS no token, bare globe");
  }

  drone = viewer.entities.add({
    position: new Cesium.CallbackProperty(() =>
      Cesium.Cartesian3.fromDegrees(current.lon, current.lat, current.alt), false),
    orientation: new Cesium.CallbackProperty(() =>
      Cesium.Transforms.headingPitchRollQuaternion(
        Cesium.Cartesian3.fromDegrees(current.lon, current.lat, current.alt),
        new Cesium.HeadingPitchRoll(current.heading, 0, 0)), false),
    box: { dimensions: new Cesium.Cartesian3(4, 4, 1), material: Cesium.Color.ORANGE },
    cylinder: undefined,
  });
  viewer.entities.add({
    polyline: {
      positions: new Cesium.CallbackProperty(() => trail, false),
      width: 3, material: Cesium.Color.CYAN, clampToGround: false,
    },
  });
  viewer.camera.setView({
    destination: Cesium.Cartesian3.fromDegrees(11.6710, 48.2615, 700),
    orientation: { heading: 0, pitch: Cesium.Math.toRadians(-35), roll: 0 },
  });

  // frame-rate probe
  let frames = 0, last = performance.now();
  viewer.scene.postRender.addEventListener(() => {
    frames++;
    const now = performance.now();
    if (now - last >= 1000) { console.log("FPS " + (frames * 1000 / (now - last)).toFixed(1)); frames = 0; last = now; }
  });
}

function setDrone(lon, lat, alt, heading) {
  current = { lon, lat, alt, heading };
  if (trail.length === 0 || Cesium.Cartesian3.distance(trail[trail.length - 1],
        Cesium.Cartesian3.fromDegrees(lon, lat, alt)) > 3)
    trail.push(Cesium.Cartesian3.fromDegrees(lon, lat, alt));
}
