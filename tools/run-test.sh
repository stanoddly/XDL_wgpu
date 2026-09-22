#!/usr/bin/env bash
# Runs the built test page in headless Chromium (Playwright's build, or XDL_CHROMIUM) and exits 0 on "XDL_wgpu test PASSED".
# Usage: tools/run-test.sh [build-dir]
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
node_dir="$repo_root/build/node"

if [ ! -d "$node_dir/node_modules/playwright-core" ]; then
    mkdir -p "$node_dir"
    (cd "$node_dir" && [ -f package.json ] || npm init -y >/dev/null)
    (cd "$node_dir" && npm install playwright-core --no-audit --no-fund)
fi

exec node "$repo_root/tools/run-test.mjs" "$@"
