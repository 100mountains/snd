#!/usr/bin/env bash
# Regenerate the website widget gallery (site/widgets/*.png) from real SND paint:
# build the headless renderer, dump RGBA through the actual OpenGLSurface, encode
# PNGs. Run after changing a widget's look so the site shows the current pixels.
#
#   tools/widget-shots/render.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
cmake --build build --config Debug --target snd-widget-shots
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
./build/snd-widget-shots "$tmp"
python3 tools/widget-shots/encode.py "$tmp" site/widgets
echo "site/widgets updated"
