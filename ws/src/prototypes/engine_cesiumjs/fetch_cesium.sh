#!/usr/bin/env bash
# Downloads the CesiumJS release into third_party/ (not stored in git).
set -euo pipefail
VERSION=1.147.0
cd "$(dirname "$0")"
[[ -f third_party/cesium/Cesium.js ]] && { echo "CesiumJS already present"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
curl -fsSL "https://registry.npmjs.org/cesium/-/cesium-${VERSION}.tgz" -o "$tmp/c.tgz"
tar -xzf "$tmp/c.tgz" -C "$tmp" package/Build/Cesium
mkdir -p third_party && rm -rf third_party/cesium && mv "$tmp/package/Build/Cesium" third_party/cesium
echo "CesiumJS ${VERSION} installed in third_party/cesium"
