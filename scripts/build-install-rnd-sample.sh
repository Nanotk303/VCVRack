#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGIN_DIR="$ROOT_DIR/Nanotk/rnd-sample"
RACK_SDK="$ROOT_DIR/Rack-SDK"
RACK_PLUGINS_DIR="$HOME/Library/Application Support/Rack2/plugins-mac-arm64"
INSTALL_DIR="$RACK_PLUGINS_DIR/rnd-sample"

if [[ ! -d "$RACK_SDK" ]]; then
  echo "Rack-SDK introuvable: $RACK_SDK" >&2
  exit 1
fi

echo "Compilation de rnd-sample..."
make -C "$PLUGIN_DIR" clean dist

echo "Installation dans VCV Rack..."
mkdir -p "$INSTALL_DIR"
find "$RACK_PLUGINS_DIR" -maxdepth 1 -name "rnd-sample*.vcvplugin" -delete
ditto "$PLUGIN_DIR/dist/rnd-sample" "$INSTALL_DIR"

echo "Installe: $INSTALL_DIR"
