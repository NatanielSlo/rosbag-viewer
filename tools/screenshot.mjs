// Launch the desktop app, open a recording and save a screenshot.
//   node tools/screenshot.mjs --library DIR [--bag NAME] [--t SEC] [--keys "m"] [--out FILE]
// Uses a throw-away profile so the user's saved settings are never touched.
// --no-sandbox is for this local automation only (it loads only our own page).
import { _electron as electron } from "playwright";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const arg = (name, def) => {
  const i = process.argv.indexOf(`--${name}`);
  return i > 0 ? process.argv[i + 1] : def;
};
const out = arg("out", "screenshot.png");
const profile = fs.mkdtempSync(path.join(os.tmpdir(), "fv-profile-"));
const app = await electron.launch({
  args: ["--no-sandbox", "."],
  env: { ...process.env, ROSBAG_DIR: arg("library", ""), VIEWER_USER_DATA: profile },
});
try {
  const page = await app.firstWindow();
  page.on("console", (m) => { if (m.type() === "error") console.error("console:", m.text()); });
  page.on("pageerror", (e) => console.error("pageerror:", e.message));
  await page.setViewportSize?.({ width: Number(arg("w", 1440)), height: Number(arg("h", 900)) });
  const bag = arg("bag");
  if (bag) {
    const q = new URLSearchParams({ bag, t: arg("t", "0") });
    await page.goto(page.url().split("?")[0] + "?" + q);
  }
  await page.waitForTimeout(Number(arg("wait", 2500)));
  for (const k of (arg("keys", "") || "").split(" ").filter(Boolean)) {
    await page.keyboard.press(k);
    await page.waitForTimeout(400);
  }
  if (arg("keys")) await page.waitForTimeout(Number(arg("after", 3000)));
  await page.screenshot({ path: out });
  console.log("saved", out);
} finally {
  await app.close();
  fs.rmSync(profile, { recursive: true, force: true });
}
