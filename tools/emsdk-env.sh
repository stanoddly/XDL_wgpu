#!/usr/bin/env bash
# Source this file to put the Emscripten SDK bundled with the .NET wasm-tools workload on PATH.
# Usage: source tools/emsdk-env.sh [dotnet-root]
# The pack's cache is read-only (FROZEN_CACHE), so system libraries and ports are built into $XDL_EM_CACHE (default: build/em-cache).

_xdl_dotnet_root="${1:-${DOTNET_ROOT:-$(dirname "$(readlink -f "$(command -v dotnet)")")}}"
_xdl_packs="$_xdl_dotnet_root/packs"
_xdl_sdk_pack=$(ls -d "$_xdl_packs"/Microsoft.NET.Runtime.Emscripten.*.Sdk.linux-x64/* 2>/dev/null | sort -V | tail -1)
_xdl_node_pack=$(ls -d "$_xdl_packs"/Microsoft.NET.Runtime.Emscripten.*.Node.linux-x64/* 2>/dev/null | sort -V | tail -1)

if [ -z "$_xdl_sdk_pack" ] || [ -z "$_xdl_node_pack" ]; then
    echo "emsdk-env: Emscripten packs not found under $_xdl_packs (install the wasm-tools workload)" >&2
    return 1 2>/dev/null || exit 1
fi

_xdl_repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export XDL_EM_CACHE="${XDL_EM_CACHE:-$_xdl_repo_root/build/em-cache}"
mkdir -p "$XDL_EM_CACHE"

export DOTNET_EMSCRIPTEN_LLVM_ROOT="$_xdl_sdk_pack/tools/bin"
export DOTNET_EMSCRIPTEN_BINARYEN_ROOT="$_xdl_sdk_pack/tools"
export DOTNET_EMSCRIPTEN_NODE_JS="$_xdl_node_pack/tools/bin/node"
export EM_CACHE="$XDL_EM_CACHE"
export FROZEN_CACHE=
export EMSDK="$_xdl_sdk_pack/tools"
export EMSCRIPTEN="$_xdl_sdk_pack/tools/emscripten"
export PATH="$EMSCRIPTEN:$(dirname "$DOTNET_EMSCRIPTEN_NODE_JS"):$PATH"

unset _xdl_dotnet_root _xdl_packs _xdl_sdk_pack _xdl_node_pack _xdl_repo_root
