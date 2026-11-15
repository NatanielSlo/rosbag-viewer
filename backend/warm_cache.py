"""Pre-parse every bag in a folder so the viewer opens them instantly.

    python backend/warm_cache.py /media/me/ROSBAGS/IMAV-26

Reads each bag once (slow on a USB stick, minutes for 10+ GB) and stores a
small summary in ~/.cache/rosbag-viewer. Bags already cached are skipped.
Nothing is copied to the local disk except those summaries (a few 100 KB each).
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # works under python -I
import bag_reader as br  # noqa: E402


def main(root: str) -> int:
    bags = sorted(d for d in Path(root).iterdir() if d.is_dir() and
                  ((d / "metadata.yaml").exists() or list(d.glob("*.db3"))
                   or list(d.glob("*.mcap"))))
    print(f"{len(bags)} bags in {root}", flush=True)
    failed = 0
    for i, bag in enumerate(bags, 1):
        cache = br._cache_path(bag)
        if cache is not None and cache.exists():
            print(f"[{i}/{len(bags)}] cached   {bag.name}", flush=True)
            continue
        t0 = time.time()
        try:
            br.load_flight(bag)
            print(f"[{i}/{len(bags)}] parsed   {bag.name}  ({time.time() - t0:.0f}s)", flush=True)
        except br.BagError as exc:
            failed += 1
            print(f"[{i}/{len(bags)}] SKIPPED  {bag.name}: {exc}", flush=True)
    print("done" + (f" ({failed} skipped)" if failed else ""), flush=True)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
