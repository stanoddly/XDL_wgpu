#!/usr/bin/env bash
# Builds the release archives with the .NET browser runtime's exception flags and stages them in build/release-assets with
# THIRD-PARTY-NOTICES.txt and release-notes.md.
# Usage: tools/build-release.sh <tag> <owner/repo>   (emcc on PATH, for example from tools/emsdk-env.sh)
set -euo pipefail

tag=$1
repository=$2
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out_dir=$repo_root/build/release-assets
build_dir=$repo_root/build/release
flags="-fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0"

# A fresh configure, so the archives come from the emcc on PATH that the release notes name, not from a compiler an older configure cached.
rm -rf "$build_dir"
emcmake cmake -S "$repo_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DXDL_BUILD_TEST=OFF -DXDL_BUILD_SDL_LIBRARIES=ON -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags"
# A bare --parallel gives make no job limit, which starts every SDL compile at once.
cmake --build "$build_dir" --parallel "$(nproc)" --target XDL_wgpu SDL3-static SDL3_image-static SDL3_mixer-static SDL3_ttf-static

rm -rf "$out_dir"
mkdir -p "$out_dir"

# The SDL archives drop the lib prefix: the .NET wasm build registers each native reference's file name as the P/Invoke module that
# DllImport("SDL3") and the others bind to. libXDL_wgpu.a keeps its prefix, so no DllImport binds to it by accident.
cp "$build_dir/libXDL_wgpu.a" "$out_dir/"
cp "$build_dir/external/SDL/libSDL3.a" "$out_dir/SDL3.a"
cp "$build_dir/external/SDL_image/libSDL3_image.a" "$out_dir/SDL3_image.a"
cp "$build_dir/external/SDL_mixer/libSDL3_mixer.a" "$out_dir/SDL3_mixer.a"
# SDL_ttf's static build already puts the objects of its vendored libraries into its archive, so it links on its own.
cp "$build_dir/external/SDL_ttf/libSDL3_ttf.a" "$out_dir/SDL3_ttf.a"

freetype_year=$(grep -m1 -oE 'Copyright \(C\) 1996-[0-9]{4}' "$repo_root/external/SDL_ttf/external/freetype/include/freetype/freetype.h" | grep -oE '[0-9]{4}$')
freetype_acknowledgment="Portions of this software are copyright © $freetype_year The FreeType Project (www.freetype.org). All rights reserved."

# Writes one section of THIRD-PARTY-NOTICES.txt. An empty text fails the build like a missing one, so a library update cannot drop a
# notice silently.
section() {
    if [ -z "${3//[[:space:]]/}" ]; then
        echo "build-release: the notice of $1 is missing or empty in $2" >&2
        exit 1
    fi
    printf '==== %s (%s)\n\n%s\n\n' "$1" "$2" "$3"
}
# Copies a notice verbatim from the pinned source, from the first line matching <start> through the next line matching <end>.
notice() {
    local text=""
    text=$(NOTICE_START=$3 NOTICE_END=$4 awk '!found && $0 ~ ENVIRON["NOTICE_START"] { found = 1 } found { print } found && $0 ~ ENVIRON["NOTICE_END"] { done = 1; exit } END { exit !done }' "$repo_root/$2") || text=""
    section "$1" "$2" "$text"
}
notice_file() {
    local text=""
    text=$(cat "$repo_root/$2") || text=""
    section "$1" "$2" "$text"
}

# Every source and header the compilers read for the archives, from their dependency files, outside the build directory.
repo_real=$(realpath "$repo_root")
find "$build_dir" -name '*.o.d' -exec cat {} + | tr -s ' \\\t' '\n\n\n' | grep '^/' | sort -u | xargs realpath -m -- | grep "^$repo_real/" | grep -v "^$repo_real/build/" | sort -u > "$build_dir/compiled-sources.txt"
# The copyright lines of the given files, without comment decoration. A line ending in "by", "and" or a comma, or with years but no
# holder, continues on the next line of the same file.
# Lines with a double quote are string literals and documentation examples, not notices.
copyright_lines() {
    xargs -r awk '
        function clean(s) { sub(/^[[:space:]\/*#|]+/, "", s); sub(/[[:space:]\/*|]+$/, "", s); gsub(/[[:space:]]+/, " ", s); return s }
        function continues(s,    holder) {
            holder = s
            sub(/^[Cc][Oo][Pp][Yy][Rr][Ii][Gg][Hh][Tt][[:space:]]*(\([Cc]\)|©)?/, "", holder)
            return s ~ /(,| and| by)$/ || holder !~ /[A-Za-z]/
        }
        function emit(s) { if (continues(s)) { pending = s } else { print s } }
        FNR == 1 && pending != "" { print pending; pending = "" }
        pending != "" { line = pending " " clean($0); pending = ""; emit(line); next }
        tolower($0) ~ /copyright[[:space:]]*(\(c\)|©|[0-9][0-9][0-9][0-9])/ && $0 !~ /"/ { emit(clean($0)) }
        END { if (pending != "") { print pending } }' | sort -u
}
# The copyright lines of one library's compiled files: the notices its license asks to keep, which a summary such as HarfBuzz's COPYING
# does not list in full.
library_copyrights() {
    local files="" text=""
    files=$(grep "^$repo_real/$2" "$build_dir/compiled-sources.txt") || files=""
    if [ -n "${3:-}" ]; then
        files=$(printf '%s\n' "$files" | grep -v "^$repo_real/$3") || files=""
    fi
    text=$(printf '%s\n' "$files" | copyright_lines) || text=""
    section "Copyright lines of $1" "every distinct copyright line in its sources and headers compiled into this release" "$text"
}

{
    printf 'Licenses and notices for the archives of this release. SDL3_ttf.a also contains the libraries SDL_ttf vendors: FreeType,\n'
    printf 'HarfBuzz, plutosvg and plutovg. Below are the license of each library, the FreeType acknowledgment, the notices of third-party\n'
    printf 'code whose license the library'"'"'s own does not cover, and the copyright lines of every source and header compiled into each library.\n\n'
    notice_file "XDL_wgpu license (libXDL_wgpu.a)" LICENSE
    notice_file "SDL license (SDL3.a)" external/SDL/LICENSE.txt
    notice_file "SDL_image license (SDL3_image.a)" external/SDL_image/LICENSE.txt
    notice_file "SDL_mixer license (SDL3_mixer.a)" external/SDL_mixer/LICENSE.txt
    notice_file "SDL_mixer: TiMidity license (SDL3_mixer.a)" external/SDL_mixer/src/timidity/COPYING
    notice_file "SDL_mixer: dr_libs license (SDL3_mixer.a)" external/SDL_mixer/src/dr_libs/LICENSE
    notice_file "SDL_ttf license (SDL3_ttf.a)" external/SDL_ttf/LICENSE.txt
    notice_file "FreeType license (SDL3_ttf.a)" external/SDL_ttf/external/freetype/LICENSE.TXT
    notice_file "FreeType License, the FTL, under which this release uses FreeType (SDL3_ttf.a)" external/SDL_ttf/external/freetype/docs/FTL.TXT
    notice_file "HarfBuzz license (SDL3_ttf.a)" external/SDL_ttf/external/harfbuzz/COPYING
    notice_file "plutosvg license (SDL3_ttf.a)" external/SDL_ttf/external/plutosvg/LICENSE
    notice_file "plutovg license (SDL3_ttf.a)" external/SDL_ttf/external/plutovg/LICENSE
    printf '==== FreeType acknowledgment (SDL3_ttf.a)\n\n%s\n\n' "$freetype_acknowledgment"
    # Found with a search of every compiled source and header for copyright lines, license texts and references to Unicode data.
    # Public-domain code and zlib-licensed code, whose license asks for no notice in a binary, are not listed.
    notice "SDL: math functions from Sun's libm (SDL3.a)" external/SDL/src/libm/e_atan2.c 'Copyright \(C\) 1993 by Sun Microsystems' 'is preserved\.'
    notice_file "SDL: YUV to RGB conversion (SDL3.a)" external/SDL/src/video/yuv2rgb/LICENSE
    notice "SDL: keysym to UCS conversion (SDL3.a)" external/SDL/src/events/imKStoUCS.c 'Copyright \(C\) 2003-2006,2008 Jamey Sharp' 'DEALINGS IN THE SOFTWARE\.$'
    notice "SDL_image: GIF decoder adapted from XPaint (SDL3_image.a)" external/SDL_image/src/IMG_gif.c 'Copyright 1990, 1991, 1993 David Koblas' 'provided "as is"'
    notice "SDL_image: QOI codec (SDL3_image.a)" external/SDL_image/src/qoi.h 'Copyright\(c\) 2021 Dominic Szablewski' '^SOFTWARE\.$'
    notice "FreeType: BDF driver (SDL3_ttf.a)" external/SDL_ttf/external/freetype/src/bdf/README '^License$' '^THE USE OR OTHER DEALINGS IN THE SOFTWARE\.$'
    notice "FreeType: PCF driver (SDL3_ttf.a)" external/SDL_ttf/external/freetype/src/pcf/README '^License$' '^SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE\.$'
    notice "FreeType: PCF bitmap utilities (SDL3_ttf.a)" external/SDL_ttf/external/freetype/src/pcf/pcfutil.c 'Copyright 1990, 1994, 1998  The Open Group' 'written authorization from The Open Group\.'
    notice "FreeType: HarfBuzz glue of the auto-hinter (SDL3_ttf.a)" external/SDL_ttf/external/freetype/src/autofit/ft-hb.c 'Copyright © 2009, 2023  Red Hat' 'OR MODIFICATIONS\.$'
    notice_file "HarfBuzz: Universal Shaping Engine data (SDL3_ttf.a)" external/SDL_ttf/external/harfbuzz/src/ms-use/COPYING
    notice "HarfBuzz: Unicode character database functions (SDL3_ttf.a)" external/SDL_ttf/external/harfbuzz/src/hb-ucd.cc 'Copyright \(C\) 2012 Grigori Goronzy' 'USE OR PERFORMANCE OF THIS SOFTWARE\.'
    notice "HarfBuzz: fasthash (SDL3_ttf.a)" external/SDL_ttf/external/harfbuzz/src/hb-algs.hh 'Copyright \(C\) 2012 Zilong Tan' '^   SOFTWARE\.$'
    # No pinned source carries the license of the Unicode data HarfBuzz generates its tables from (hb-ucd-table.hh, hb-unicode-emoji-table.hh,
    # the shaper tables), so the repository keeps a copy of https://www.unicode.org/license.txt.
    notice_file "HarfBuzz: tables generated from Unicode data (SDL3_ttf.a)" LICENSES/Unicode-3.0.txt

    library_copyrights "XDL_wgpu (libXDL_wgpu.a)" "src/"
    library_copyrights "SDL (SDL3.a)" "external/SDL/"
    library_copyrights "SDL_image (SDL3_image.a)" "external/SDL_image/"
    library_copyrights "SDL_mixer (SDL3_mixer.a)" "external/SDL_mixer/"
    library_copyrights "SDL_ttf (SDL3_ttf.a)" "external/SDL_ttf/" "external/SDL_ttf/external/"
    library_copyrights "FreeType (SDL3_ttf.a)" "external/SDL_ttf/external/freetype/"
    library_copyrights "HarfBuzz (SDL3_ttf.a)" "external/SDL_ttf/external/harfbuzz/"
    library_copyrights "plutosvg (SDL3_ttf.a)" "external/SDL_ttf/external/plutosvg/"
    library_copyrights "plutovg (SDL3_ttf.a)" "external/SDL_ttf/external/plutovg/"
} > "$out_dir/THIRD-PARTY-NOTICES.txt"

emscripten_version=$(emcc --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
# One row of the release notes' source table. XDL_wgpu's version is the release tag. A submodule's version is the remote's tag of its commit: the workflow's shallow checkout
# fetches no tags. A commit without a tag has no version.
source_row() {
    local commit="" url="" version=${4:-}
    commit=$(git -C "$repo_root/$3" rev-parse HEAD)
    if [ "$3" = . ]; then
        url=https://github.com/$repository
    else
        url=$(git -C "$repo_root/$3" remote get-url origin)
        url=${url/#git@github.com:/https://github.com/}
        url=${url%.git}
        version=$(GIT_TERMINAL_PROMPT=0 git -C "$repo_root/$3" ls-remote --tags origin | awk -v commit="$commit" '$1 == commit { sub(/^refs\/tags\//, "", $2); sub(/\^\{\}$/, "", $2); print $2; exit }') || version=""
    fi
    printf '| %s | %s | %s | [%s](%s/commit/%s) |\n' "$1" "$2" "$version" "${commit:0:7}" "$url" "$commit"
}
{
    printf 'Built with Emscripten %s and `%s`, for the .NET browser runtime of the same Emscripten.\n\n' "$emscripten_version" "$flags"
    printf '| Archive | Library | Version | Commit |\n|---|---|---|---|\n'
    source_row libXDL_wgpu.a XDL_wgpu . "$tag"
    source_row SDL3.a SDL external/SDL
    source_row SDL3_image.a SDL_image external/SDL_image
    source_row SDL3_mixer.a SDL_mixer external/SDL_mixer
    source_row SDL3_ttf.a SDL_ttf external/SDL_ttf
    for dependency in FreeType:freetype HarfBuzz:harfbuzz plutosvg:plutosvg plutovg:plutovg; do
        source_row "" "${dependency%%:*}" "external/SDL_ttf/external/${dependency#*:}"
    done
    printf '\nLink the archives in the order of the table: `libXDL_wgpu.a` first, so its `SDL_GPU*` definitions win over those of `SDL3.a`. `SDL3_ttf.a` contains FreeType, HarfBuzz, plutosvg and plutovg. [`tools/native-references.sh`](https://github.com/%s/blob/%s/tools/native-references.sh) prints the archives as MSBuild items.\n\n' "$repository" "$tag"
    printf 'Licenses: `THIRD-PARTY-NOTICES.txt`. %s\n' "$freetype_acknowledgment"
} > "$out_dir/release-notes.md"
