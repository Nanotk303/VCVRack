#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGIN_DIR="$ROOT_DIR/Nanotk/mts-midi-cv"
RACK_SDK="$ROOT_DIR/Rack-SDK"
RACK_PLUGINS_DIR="$HOME/Library/Application Support/Rack2/plugins-mac-arm64"
INSTALL_DIR="$RACK_PLUGINS_DIR/mts-midi-cv"

if [[ ! -d "$RACK_SDK" ]]; then
  echo "Rack-SDK introuvable: $RACK_SDK" >&2
  exit 1
fi

echo "Compilation de mts-midi-cv..."
make -C "$PLUGIN_DIR" clean dist

echo "Installation dans VCV Rack..."
mkdir -p "$INSTALL_DIR"
find "$RACK_PLUGINS_DIR" -maxdepth 1 -name "mts-midi-cv*.vcvplugin" -delete
ditto "$PLUGIN_DIR/dist/mts-midi-cv" "$INSTALL_DIR"

echo "Installe: $INSTALL_DIR"
