#!/usr/bin/env bash
# Builds the release archives with the .NET browser runtime's exception flags and stages them in build/release-assets with the
# header, the licenses, THIRD-PARTY-NOTICES.txt, versions.json, SHA256SUMS and release-notes.md.
# Usage: tools/build-release.sh <tag> <owner/repo>   (emcc on PATH, for example from tools/emsdk-env.sh)
set -euo pipefail

tag=$1
repository=$2
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out_dir=$repo_root/build/release-assets
build_dir=$repo_root/build/release
flags="-fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0"

# A fresh configure, so the archives come from the emcc on PATH that versions.json names, not from a compiler an older configure cached.
rm -rf "$build_dir"
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

freetype_year=$(grep -m1 -oE 'Copyright \(C\) 1996-[0-9]{4}' "$repo_root/external/SDL_ttf/external/freetype/include/freetype/freetype.h" | grep -oE '[0-9]{4}$')
freetype_acknowledgment="Portions of this software are copyright © $freetype_year The FreeType Project (www.freetype.org). All rights reserved."

# Copies a notice verbatim from the pinned source, from the first line matching <start> through the next line matching <end>. A notice
# that is not found fails the build, so a library update cannot drop one silently.
notice() {
    local text
    if ! text=$(NOTICE_START=$3 NOTICE_END=$4 awk '!found && $0 ~ ENVIRON["NOTICE_START"] { found = 1 } found { print } found && $0 ~ ENVIRON["NOTICE_END"] { done = 1; exit } END { exit !done }' "$repo_root/$2"); then
        echo "build-release: the notice of $1 is missing from $2" >&2
        exit 1
    fi
    printf '==== %s (%s)\n\n%s\n\n' "$1" "$2" "$text"
}
notice_file() {
    local text
    if ! text=$(cat "$repo_root/$2"); then
        echo "build-release: the notice of $1 is missing: $2" >&2
        exit 1
    fi
    printf '==== %s (%s)\n\n%s\n\n' "$1" "$2" "$text"
}
# Notices of code compiled into the archives beyond each library's own license, found with a search of every compiled source and
# header (the .o.d files under build/release) for copyright lines, license texts and references to Unicode data. Public-domain code and zlib-licensed code, whose license asks for no notice in a binary, are not listed.
{
    printf 'Notices of third-party code compiled into the archives of this release, copied from the sources in versions.json.\n'
    printf 'The licenses of the libraries themselves are in the LICENSE-*.txt files.\n\n'
    printf '==== FreeType (libfreetype.a)\n\n%s\n\n' "$freetype_acknowledgment"
    notice "SDL: math functions from Sun's libm (SDL3.a)" external/SDL/src/libm/e_atan2.c 'Copyright \(C\) 1993 by Sun Microsystems' 'is preserved\.'
    notice_file "SDL: YUV to RGB conversion (SDL3.a)" external/SDL/src/video/yuv2rgb/LICENSE
    notice "SDL: keysym to UCS conversion (SDL3.a)" external/SDL/src/events/imKStoUCS.c 'Copyright \(C\) 2003-2006,2008 Jamey Sharp' 'DEALINGS IN THE SOFTWARE\.$'
    notice "SDL_image: GIF decoder adapted from XPaint (SDL3_image.a)" external/SDL_image/src/IMG_gif.c 'Copyright 1990, 1991, 1993 David Koblas' 'provided "as is"'
    notice "SDL_image: QOI codec (SDL3_image.a)" external/SDL_image/src/qoi.h 'Copyright\(c\) 2021 Dominic Szablewski' '^SOFTWARE\.$'
    notice "FreeType: BDF driver (libfreetype.a)" external/SDL_ttf/external/freetype/src/bdf/README '^License$' '^THE USE OR OTHER DEALINGS IN THE SOFTWARE\.$'
    notice "FreeType: PCF driver (libfreetype.a)" external/SDL_ttf/external/freetype/src/pcf/README '^License$' '^SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE\.$'
    notice "FreeType: PCF bitmap utilities (libfreetype.a)" external/SDL_ttf/external/freetype/src/pcf/pcfutil.c 'Copyright 1990, 1994, 1998  The Open Group' 'written authorization from The Open Group\.'
    notice "FreeType: HarfBuzz glue of the auto-hinter (libfreetype.a)" external/SDL_ttf/external/freetype/src/autofit/ft-hb.c 'Copyright © 2009, 2023  Red Hat' 'OR MODIFICATIONS\.$'
    notice_file "HarfBuzz: Universal Shaping Engine data (libharfbuzz.a)" external/SDL_ttf/external/harfbuzz/src/ms-use/COPYING
    notice "HarfBuzz: Unicode character database functions (libharfbuzz.a)" external/SDL_ttf/external/harfbuzz/src/hb-ucd.cc 'Copyright \(C\) 2012 Grigori Goronzy' 'USE OR PERFORMANCE OF THIS SOFTWARE\.'
    notice "HarfBuzz: fasthash (libharfbuzz.a)" external/SDL_ttf/external/harfbuzz/src/hb-algs.hh 'Copyright \(C\) 2012 Zilong Tan' '^   SOFTWARE\.$'
    # No pinned source carries the license of the Unicode data HarfBuzz generates its tables from (hb-ucd-table.hh, hb-unicode-emoji-table.hh,
    # the shaper tables), so the repository keeps a copy of https://www.unicode.org/license.txt.
    notice_file "HarfBuzz: tables generated from Unicode data (libharfbuzz.a)" LICENSES/Unicode-3.0.txt
} > "$out_dir/THIRD-PARTY-NOTICES.txt"

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
    printf 'Licenses: `LICENSE-*.txt` for each library, and `THIRD-PARTY-NOTICES.txt` for third-party code compiled into them. %s\n\n' "$freetype_acknowledgment"
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
