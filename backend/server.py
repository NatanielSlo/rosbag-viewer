"""Local HTTP API + static frontend for the rosbag viewer.

Run:  python backend/server.py [--bags DIR] [--port 8765]
Binds to 127.0.0.1 only. If VIEWER_TOKEN is set, API calls must send it as
the `X-Viewer-Token` header (Electron generates one per launch), or as a
`token` query parameter where headers cannot be set (<img src> for frames).
"""
from __future__ import annotations

import argparse
import os
import secrets
import threading
import time
from pathlib import Path

import uvicorn
from fastapi import Body, Depends, FastAPI, Header, HTTPException, Query
from fastapi.responses import FileResponse, JSONResponse, Response
from fastapi.staticfiles import StaticFiles
from starlette.middleware.trustedhost import TrustedHostMiddleware

import bag_reader as br

ROOT = Path(__file__).resolve().parent.parent
FRONTEND = ROOT / "frontend"
CESIUM_VERSION = "1.146.0"  # pinned: never "latest"
CESIUM_CDN = f"https://cdn.jsdelivr.net/npm/cesium@{CESIUM_VERSION}/Build/Cesium/"
# Only answer requests addressed to the loopback host. Blocks DNS-rebinding:
# a web page whose domain is re-pointed at 127.0.0.1 still sends its own Host.
ALLOWED_HOSTS = ["127.0.0.1", "localhost"]


def load_env_file(path: Path) -> dict[str, str]:
    """Minimal KEY=VALUE parser for .env.local (no shell evaluation)."""
    out: dict[str, str] = {}
    if not path.exists():
        return out
    for line in path.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip().strip('"').strip("'")
    return out


def setting(name: str) -> str:
    """Environment variable first, then .env.local."""
    return os.environ.get(name) or load_env_file(ROOT / ".env.local").get(name, "")


def make_app(bags_dir: Path, allowed_hosts: list[str] | None = None) -> FastAPI:
    # `library` is the folder listed in the app; `extra` holds single
    # recordings the user opened from elsewhere (file dialog / drag and drop).
    state = {"library": bags_dir.resolve(), "extra": set()}
    app = FastAPI(title="Rosbag viewer")
    app.add_middleware(TrustedHostMiddleware, allowed_hosts=allowed_hosts or ALLOWED_HOSTS)
    token = os.environ.get("VIEWER_TOKEN")

    def check_token(x_viewer_token: str | None = Header(default=None),
                    token_param: str | None = Query(default=None, alias="token")) -> None:
        given = x_viewer_token or token_param or ""
        if token and not secrets.compare_digest(given.encode(), token.encode()):
            raise HTTPException(401, "Missing or wrong token")

    def resolve(path: str) -> Path:
        """Only serve bags under the library folder or ones the user opened."""
        p = Path(path).expanduser().resolve()
        roots = [state["library"], *state["extra"]]
        if not any(r == p or r in p.parents for r in roots):
            raise HTTPException(403, "Path is outside the bags directory")
        return p

    def app_only() -> None:
        """Changing what may be read is only allowed for the desktop app,
        which always runs with a per-launch token."""
        if not token:
            raise HTTPException(403, "Only available in the desktop app")

    def bag_dirs(root: Path) -> list[Path]:
        if not root.is_dir():
            return []
        return sorted(d for d in root.iterdir() if d.is_dir() and (
            (d / "metadata.yaml").exists() or any(d.glob("*.mcap")) or any(d.glob("*.db3"))))

    api = dict(dependencies=[Depends(check_token)])

    @app.exception_handler(br.BagError)
    async def bag_error(_, exc: br.BagError):
        return JSONResponse({"error": str(exc)}, status_code=422)

    @app.middleware("http")
    async def revalidate_app_files(request, call_next):
        """Always revalidate the app's own files (HTML/JS/CSS) so an updated
        viewer is never shadowed by a stale copy in the browser cache. The
        ETag makes this cheap (304). Big vendored libraries and camera frames
        keep normal caching."""
        response = await call_next(request)
        path = request.url.path
        if not path.startswith(("/api/", "/vendor/")):
            response.headers["Cache-Control"] = "no-cache"
        return response

    @app.get("/api/bags", **api)
    def list_bags():
        root = state["library"]
        return {"dir": str(root), "exists": root.is_dir(),
                "bags": [br.bag_summary(d) for d in bag_dirs(root)]}

    @app.post("/api/library", dependencies=[Depends(check_token), Depends(app_only)])
    def set_library(path: str = Body(..., embed=True)):
        p = Path(path).expanduser().resolve()
        if not p.is_dir():
            raise HTTPException(404, "Folder not found")
        state["library"] = p
        return list_bags()

    @app.post("/api/open", dependencies=[Depends(check_token), Depends(app_only)])
    def open_recording(path: str = Body(..., embed=True)):
        """Allow one recording outside the library (a bag folder, a file
        inside one, or a single .mcap/.db3 file)."""
        p = Path(path).expanduser().resolve()
        if p.is_file() and p.suffix not in (".mcap", ".db3") or p.name == "metadata.yaml":
            p = p.parent
        if p.is_file() and p.suffix == ".db3":
            p = p.parent  # rosbag2 sqlite bags need their folder
        if not p.exists():
            raise HTTPException(404, "File or folder not found")
        state["extra"].add(p)
        return br.bag_summary(p)

    @app.get("/api/flight", **api)
    def flight(path: str = Query(...)):
        return br.load_flight(resolve(path))

    @app.get("/api/frame", **api)
    def frame(path: str, topic: str, t: float = 0.0):
        jpeg = br.load_camera_frame(resolve(path), topic, t)
        if jpeg is None:
            raise HTTPException(404, "No such camera topic in this bag")
        return Response(jpeg, media_type="image/jpeg",
                        headers={"Cache-Control": "max-age=3600"})

    @app.get("/api/config", **api)
    def config():
        """Map settings for the frontend. Only the Cesium ion token is handed
        out: Google Photorealistic 3D Tiles are streamed through ion, so the
        Google Maps key is deliberately NOT exposed to the browser."""
        local = (FRONTEND / "vendor" / "cesium" / "Cesium.js").exists()
        return {
            "cesium_ion_token": setting("CESIUM_ION_TOKEN"),
            "cesium_base": "/vendor/cesium/" if local else CESIUM_CDN,
            "cesium_local": local,
        }

    @app.get("/")
    def index():
        return FileResponse(FRONTEND / "index.html")

    app.mount("/", StaticFiles(directory=FRONTEND), name="frontend")
    return app


def exit_with_parent(pid: int) -> None:
    """Started by the desktop app: quit when it is gone (even after a crash),
    so no orphaned server keeps the port and the USB stick busy."""
    def watch() -> None:
        while True:
            time.sleep(2)
            try:
                os.kill(pid, 0)
            except OSError:
                os._exit(0)
    threading.Thread(target=watch, daemon=True).start()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bags", default=os.environ.get("ROSBAG_DIR", str(ROOT.parent / "bags")))
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--parent-pid", type=int, help="exit when this process ends")
    args = ap.parse_args()
    if args.parent_pid:
        exit_with_parent(args.parent_pid)
    uvicorn.run(make_app(Path(args.bags)), host="127.0.0.1", port=args.port, log_level="info")


if __name__ == "__main__":
    main()
