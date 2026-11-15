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
from pathlib import Path

import uvicorn
from fastapi import Depends, FastAPI, Header, HTTPException, Query
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
    bags_dir = bags_dir.resolve()
    app = FastAPI(title="Rosbag viewer")
    app.add_middleware(TrustedHostMiddleware, allowed_hosts=allowed_hosts or ALLOWED_HOSTS)
    token = os.environ.get("VIEWER_TOKEN")

    def check_token(x_viewer_token: str | None = Header(default=None),
                    token_param: str | None = Query(default=None, alias="token")) -> None:
        given = x_viewer_token or token_param or ""
        if token and not secrets.compare_digest(given.encode(), token.encode()):
            raise HTTPException(401, "Missing or wrong token")

    def resolve(path: str) -> Path:
        """Only serve bags that live under the configured bags directory."""
        p = Path(path).expanduser().resolve()
        if bags_dir != p and bags_dir not in p.parents:
            raise HTTPException(403, "Path is outside the bags directory")
        return p

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
        found = sorted(
            d for d in bags_dir.iterdir()
            if d.is_dir() and ((d / "metadata.yaml").exists() or list(d.glob("*.mcap"))
                               or list(d.glob("*.db3")))
        ) if bags_dir.exists() else []
        return {"dir": str(bags_dir), "bags": [br.bag_summary(d) for d in found]}

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


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bags", default=os.environ.get("ROSBAG_DIR", str(ROOT.parent / "bags")))
    ap.add_argument("--port", type=int, default=8765)
    args = ap.parse_args()
    uvicorn.run(make_app(Path(args.bags)), host="127.0.0.1", port=args.port, log_level="info")


if __name__ == "__main__":
    main()
