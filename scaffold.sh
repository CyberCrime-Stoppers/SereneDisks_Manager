#!/usr/bin/env bash
# SereneDisk Manager — directory scaffold
# Creates the M0 folder structure so source files can be added.
# Run once inside the repo root:  chmod +x scaffold.sh && ./scaffold.sh
set -euo pipefail
cd "$(dirname "$0")"

echo "==> Creating SereneDisk Manager directory structure"
mkdir -p src/backend
mkdir -p qml/pages
mkdir -p qml/components
mkdir -p deploy

echo "==> Done. Structure:"
find src qml deploy -type d
