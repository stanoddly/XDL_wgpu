#!/usr/bin/env bash
# Builds the release archives with the .NET browser runtime's exception flags and stages them with the header, the licenses,
# versions.json, SHA256SUMS and release-notes.md.
# Usage: tools/build-release.sh <tag> <owner/repo> [out-dir]   (emcc on PATH, for example from tools/emsdk-env.sh)
set -euo pipefail

tag=$1
repository=$2
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out_dir=${3:-$repo_root/build/release-assets}
build_dir=$repo_root/build/release
flags="-fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0"

emcmake cmake -S "$repo_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DXDL_BUILD_TEST=OFF -DXDL_BUILD_SDL_LIBRARIES=ON -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags"
cmake --build "$build_dir" --parallel --target XDL_wgpu SDL3-static SDL3_image-static SDL3_mixer-static SDL3_ttf-static freetype harfbuzz plutosvg plutovg

rm -rf "$out_dir"
mkdir -p "$out_dir"

# The SDL archives drop the lib prefix: the .NET wasm build registers each native reference's file name as the P/Invoke module that
# DllImport("SDL3") and the others bind to. The rest keep theirs, so no DllImport binds to them by accident.
cp "$build_dir/libXDL_wgpu.a" "$out_dir/"
cp "$build_dir/external/SDL/libSDL3.a" "$out_dir/SDL3.a"
cp "$build_dir/external/SDL_image/libSDL3_image.a" "$out_dir/SDL3_image.a"
cp "$build_dir/external/SDL_mixer/libSDL3_mixer.a" "$out_dir/SDL3_mixer.a"
cp "$build_dir/external/SDL_ttf/libSDL3_ttf.a" "$out_dir/SDL3_ttf.a"
for dependency in freetype harfbuzz plutosvg plutovg; do
    cp "$build_dir/external/SDL_ttf/external/$dependency/lib$dependency.a" "$out_dir/"
done
cp "$repo_root/include/XDL_wgpu.h" "$out_dir/"

cp "$repo_root/LICENSE" "$out_dir/LICENSE-XDL_wgpu.txt"
cp "$repo_root/external/SDL/LICENSE.txt" "$out_dir/LICENSE-SDL.txt"
cp "$repo_root/external/SDL_image/LICENSE.txt" "$out_dir/LICENSE-SDL_image.txt"
cp "$repo_root/external/SDL_mixer/LICENSE.txt" "$out_dir/LICENSE-SDL_mixer.txt"
cp "$repo_root/external/SDL_mixer/src/timidity/COPYING" "$out_dir/LICENSE-timidity.txt"
cp "$repo_root/external/SDL_mixer/src/dr_libs/LICENSE" "$out_dir/LICENSE-dr_libs.txt"
cp "$repo_root/external/SDL_ttf/LICENSE.txt" "$out_dir/LICENSE-SDL_ttf.txt"
cp "$repo_root/external/SDL_ttf/external/freetype/LICENSE.TXT" "$out_dir/LICENSE-freetype.txt"
cp "$repo_root/external/SDL_ttf/external/freetype/docs/FTL.TXT" "$out_dir/LICENSE-freetype-FTL.txt"
cp "$repo_root/external/SDL_ttf/external/harfbuzz/COPYING" "$out_dir/LICENSE-harfbuzz.txt"
cp "$repo_root/external/SDL_ttf/external/plutosvg/LICENSE" "$out_dir/LICENSE-plutosvg.txt"
cp "$repo_root/external/SDL_ttf/external/plutovg/LICENSE" "$out_dir/LICENSE-plutovg.txt"

emscripten_version=$(emcc --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
source_entry() {
    printf '    "%s": { "commit": "%s", "describe": "%s" }' "$1" "$(git -C "$repo_root/$2" rev-parse HEAD)" "$(git -C "$repo_root/$2" describe --tags --always)"
}
{
    printf '{\n  "version": "%s",\n  "emscripten": "%s",\n  "flags": "%s",\n  "sources": {\n' "$tag" "$emscripten_version" "$flags"
    source_entry XDL_wgpu . && printf ',\n'
    source_entry SDL external/SDL && printf ',\n'
    source_entry SDL_image external/SDL_image && printf ',\n'
    source_entry SDL_mixer external/SDL_mixer && printf ',\n'
    source_entry SDL_ttf external/SDL_ttf && printf ',\n'
    source_entry freetype external/SDL_ttf/external/freetype && printf ',\n'
    source_entry harfbuzz external/SDL_ttf/external/harfbuzz && printf ',\n'
    source_entry plutosvg external/SDL_ttf/external/plutosvg && printf ',\n'
    source_entry plutovg external/SDL_ttf/external/plutovg && printf '\n'
    printf '  }\n}\n'
} > "$out_dir/versions.json"

(cd "$out_dir" && sha256sum -- *.a *.h *.txt versions.json > SHA256SUMS)

# The reference order is the link order. libXDL_wgpu.a comes first so its SDL_GPU* definitions win over SDL3.a's.
hash_of() {
    grep -E " $1\$" "$out_dir/SHA256SUMS" | cut -d' ' -f1
}
url_reference() {
    printf '  <NativeUrlReference Include="https://github.com/%s/releases/download/%s/%s" Sha256="%s"%s />\n' "$repository" "$tag" "$1" "$(hash_of "$1")" "$2"
}
{
    printf 'Built with Emscripten %s and `%s`, for the .NET browser runtime of the same Emscripten.\n\n' "$emscripten_version" "$flags"
    printf '| Library | Source |\n|---|---|\n'
    for name in SDL SDL_image SDL_mixer SDL_ttf; do
        printf '| %s | `%s` |\n' "$name" "$(git -C "$repo_root/external/$name" describe --tags --always)"
    done
    printf '| FreeType, HarfBuzz, plutosvg, plutovg | vendored by SDL_ttf, commits in `versions.json` |\n\n'
    printf 'Pixely (`NativeUrlReference`), in this order:\n\n```xml\n<ItemGroup>\n'
    url_reference libXDL_wgpu.a ' ScanForPInvokes="false"'
    for archive in SDL3.a SDL3_image.a SDL3_mixer.a SDL3_ttf.a; do
        url_reference "$archive" ''
    done
    for dependency in freetype harfbuzz plutosvg plutovg; do
        url_reference "lib$dependency.a" ' ScanForPInvokes="false"'
    done
    printf '</ItemGroup>\n```\n'
} > "$out_dir/release-notes.md"
