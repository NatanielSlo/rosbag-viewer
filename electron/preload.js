// Bridge between the sandboxed page and the desktop shell. Only these few
// calls are exposed; the page gets no Node.js access.
const { contextBridge, ipcRenderer, webUtils } = require("electron");

contextBridge.exposeInMainWorld("VIEWER_TOKEN", ipcRenderer.sendSync("viewer:token"));
contextBridge.exposeInMainWorld("desktop", {
  chooseLibrary: () => ipcRenderer.invoke("viewer:choose-library"),
  openRecording: () => ipcRenderer.invoke("viewer:open-recording"),
  // real path of a file/folder dropped on the window
  pathForFile: (file) => webUtils.getPathForFile(file),
  onMenu: (cb) => ipcRenderer.on("menu", (_e, action) => cb(action)),
});
