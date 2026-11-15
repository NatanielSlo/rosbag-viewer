// Flight Viewer desktop shell.
//
// Starts the Python backend (backend/server.py) on a free loopback port with a
// fresh random token, then shows the viewer in a window. The token reaches the
// page through the preload script, never through the URL.
const { app, BrowserWindow, Menu, dialog, ipcMain, shell } = require("electron");
const { spawn } = require("node:child_process");
const crypto = require("node:crypto");
const fs = require("node:fs");
const net = require("node:net");
const path = require("node:path");

const ROOT = path.resolve(__dirname, "..");
// separate profile for automated runs (tools/screenshot.mjs)
if (process.env.VIEWER_USER_DATA) app.setPath("userData", process.env.VIEWER_USER_DATA);
const TOKEN = crypto.randomBytes(24).toString("hex");
let backend = null;
let backendLog = "";
let win = null;
let baseUrl = "";

// ---------- settings (library folder, window size) ----------
const settingsFile = () => path.join(app.getPath("userData"), "settings.json");
function loadSettings() {
  try { return JSON.parse(fs.readFileSync(settingsFile(), "utf8")); } catch { return {}; }
}
function saveSettings(patch) {
  const s = { ...loadSettings(), ...patch };
  fs.mkdirSync(path.dirname(settingsFile()), { recursive: true });
  fs.writeFileSync(settingsFile(), JSON.stringify(s, null, 2));
  return s;
}
function libraryDir() {
  return loadSettings().library || process.env.ROSBAG_DIR || path.join(ROOT, "..", "bags");
}

// ---------- backend ----------
function freePort() {
  return new Promise((resolve, reject) => {
    const srv = net.createServer();
    srv.once("error", reject);
    srv.listen(0, "127.0.0.1", () => {
      const { port } = srv.address();
      srv.close(() => resolve(port));
    });
  });
}

function pythonExe() {
  return process.env.VIEWER_PYTHON || path.join(ROOT, ".venv", "bin", "python");
}

async function startBackend() {
  const port = await freePort();
  const args = [path.join(ROOT, "backend", "server.py"), "--bags", libraryDir(),
    "--port", String(port), "--parent-pid", String(process.pid)];
  backend = spawn(pythonExe(), args, {
    cwd: ROOT, env: { ...process.env, VIEWER_TOKEN: TOKEN }, stdio: ["ignore", "pipe", "pipe"],
  });
  const keep = (d) => { backendLog = (backendLog + d).slice(-4000); };
  backend.stdout.on("data", keep);
  backend.stderr.on("data", keep);
  const exited = new Promise((resolve) => backend.once("exit", resolve));
  backend.once("error", (err) => keep(String(err)));

  baseUrl = `http://127.0.0.1:${port}`;
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    const gone = await Promise.race([exited.then(() => true), sleep(150).then(() => false)]);
    if (gone) break;
    try {
      const r = await fetch(`${baseUrl}/api/config`, { headers: { "X-Viewer-Token": TOKEN } });
      if (r.ok) return;
    } catch { /* not listening yet */ }
  }
  throw new Error(`The data service did not start.\n\n${backendLog.trim().slice(-1500)}`);
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function stopBackend() {
  if (backend && backend.exitCode === null) backend.kill();
  backend = null;
}

// ---------- window ----------
function createWindow() {
  const s = loadSettings();
  win = new BrowserWindow({
    width: s.width || 1440, height: s.height || 900, minWidth: 960, minHeight: 600,
    backgroundColor: "#0c1117", title: "Flight Viewer", show: false,
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true, sandbox: true, nodeIntegration: false,
    },
  });
  if (s.maximized) win.maximize();
  win.once("ready-to-show", () => win.show());
  win.on("close", () => {
    const [width, height] = win.isMaximized() ? [s.width, s.height] : win.getSize();
    saveSettings({ width, height, maximized: win.isMaximized() });
  });

  // Links (e.g. the Cesium / Google attribution) open in the system browser;
  // the window itself never navigates away from the viewer.
  win.webContents.setWindowOpenHandler(({ url }) => {
    if (/^https?:\/\//.test(url)) shell.openExternal(url);
    return { action: "deny" };
  });
  win.webContents.on("will-navigate", (e, url) => {
    if (!url.startsWith(baseUrl)) { e.preventDefault(); if (/^https?:/.test(url)) shell.openExternal(url); }
  });
  win.loadURL(`${baseUrl}/`);
}

function send(action) { win?.webContents.send("menu", action); }

function buildMenu() {
  const template = [
    { label: "File", submenu: [
      { label: "Recordings…", accelerator: "CmdOrCtrl+O", click: () => send("recordings") },
      { label: "Open Recording From Disk…", accelerator: "CmdOrCtrl+Shift+O", click: () => send("open-file") },
      { label: "Choose Library Folder…", click: () => send("choose-library") },
      { type: "separator" },
      { role: "quit" },
    ] },
    { label: "View", submenu: [
      { label: "Map / 3D", accelerator: "M", registerAccelerator: false, click: () => send("toggle-mode") },
      { label: "Analysis Panel", accelerator: "CmdOrCtrl+J", click: () => send("toggle-analysis") },
      { type: "separator" },
      { role: "togglefullscreen" },
      { role: "resetZoom" }, { role: "zoomIn" }, { role: "zoomOut" },
      { type: "separator" },
      { role: "reload" }, { role: "toggleDevTools" },
    ] },
    { label: "Help", submenu: [
      { label: "Keyboard Shortcuts", accelerator: "?", registerAccelerator: false, click: () => send("shortcuts") },
    ] },
  ];
  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}

// ---------- IPC for the page ----------
ipcMain.on("viewer:token", (e) => { e.returnValue = TOKEN; });
ipcMain.handle("viewer:choose-library", async () => {
  const r = await dialog.showOpenDialog(win, {
    title: "Choose the folder that holds your recordings",
    defaultPath: libraryDir(), properties: ["openDirectory"],
  });
  if (r.canceled || !r.filePaths[0]) return null;
  saveSettings({ library: r.filePaths[0] });
  return r.filePaths[0];
});
ipcMain.handle("viewer:open-recording", async () => {
  const r = await dialog.showOpenDialog(win, {
    title: "Open a recording (bag folder or .mcap / .db3 file)",
    defaultPath: libraryDir(), properties: ["openFile", "openDirectory"],
    filters: [{ name: "ROS 2 bags", extensions: ["mcap", "db3", "yaml"] }],
  });
  return r.canceled ? null : r.filePaths[0] ?? null;
});

// ---------- lifecycle ----------
if (!app.requestSingleInstanceLock()) app.quit();
app.on("second-instance", () => { if (win) { if (win.isMinimized()) win.restore(); win.focus(); } });

app.whenReady().then(async () => {
  buildMenu();
  try {
    await startBackend();
  } catch (err) {
    dialog.showErrorBox("Flight Viewer could not start", String(err.message || err));
    app.quit();
    return;
  }
  createWindow();
});
app.on("window-all-closed", () => app.quit());
app.on("will-quit", stopBackend);
