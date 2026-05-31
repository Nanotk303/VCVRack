#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RACK_SDK="$ROOT_DIR/Rack-SDK"
RACK_PLUGINS_DIR="$HOME/Library/Application Support/Rack2/plugins-mac-arm64"

if [[ ! -d "$RACK_SDK" ]]; then
  echo "Rack-SDK introuvable: $RACK_SDK" >&2
  exit 1
fi

install_plugin() {
  local slug="$1"
  local plugin_dir="$ROOT_DIR/Nanotk/$slug"
  local install_dir="$RACK_PLUGINS_DIR/$slug"

  echo "Compilation de $slug..."
  make -C "$plugin_dir" clean dist

  echo "Installation de $slug dans VCV Rack..."
  mkdir -p "$install_dir"
  find "$RACK_PLUGINS_DIR" -maxdepth 1 -name "$slug*.vcvplugin" -delete
  ditto "$plugin_dir/dist/$slug" "$install_dir"
  echo "Installe: $install_dir"
}

install_plugin "rnd-sample"
install_plugin "stf-pad1"
