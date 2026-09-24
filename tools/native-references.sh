#!/usr/bin/env bash
# Prints the archives of an XDL_wgpu release as MSBuild items, in link order.
# Usage: tools/native-references.sh [--file <dir> | --url] [tag]
#   --file <dir>  downloads the archives into <dir>, checks their SHA-256, and prints NativeFileReference items with absolute paths (the default, into native)
#   --url         prints NativeUrlReference items with the download URL and SHA-256 of each archive, for a custom MSBuild target
#   tag           the release tag, such as v20260924.122755; without one, the latest release
# Needs gh, authenticated for a private repository. GH_REPO selects another repository than stanoddly/XDL_wgpu.
set -euo pipefail

usage() {
    sed -n '3,6s/^# //p' "$0" >&2
    exit "$1"
}

mode=file
dir=native
tag=""
while [ $# -gt 0 ]; do
    case $1 in
        --url) mode=url; shift ;;
        --file) [ $# -ge 2 ] && [ -n "$2" ] || usage 2; mode=file; dir=$2; shift 2 ;;
        -h | --help) usage 0 ;;
        -*) usage 2 ;;
        *) [ -z "$tag" ] || usage 2; tag=$1; shift ;;
    esac
done
repository=${GH_REPO:-stanoddly/XDL_wgpu}

if [ -n "$tag" ]; then
    endpoint=repos/$repository/releases/tags/$tag
else
    endpoint=repos/$repository/releases/latest
fi
# The first line is the tag, then one line per asset: name, URL and digest, separated by tabs.
release=$(gh api "$endpoint" --jq '.tag_name, (.assets[] | [.name, .browser_download_url, (.digest // "")] | @tsv)')
tag=$(head -1 <<<"$release")

# Absolute, because MSBuild resolves a relative path against the project directory, not the directory the script ran in.
if [ "$mode" = file ]; then
    mkdir -p "$dir"
    dir=$(realpath "$dir")
    dir=${dir%/}
fi
# Printed only when complete, so a failure leaves no partial item group to copy.
items=""
# libXDL_wgpu.a comes first so its SDL_GPU* definitions win over those of SDL3.a.
for archive in libXDL_wgpu.a SDL3.a SDL3_image.a SDL3_mixer.a SDL3_ttf.a; do
    IFS=$'\t' read -r _ url digest < <(awk -F '\t' -v name="$archive" 'NR > 1 && $1 == name' <<<"$release") || {
        echo "native-references: release $tag has no $archive" >&2
        exit 1
    }
    if [[ $digest != sha256:* ]]; then
        echo "native-references: GitHub has no SHA-256 of $archive in release $tag" >&2
        exit 1
    fi
    sha256=${digest#sha256:}
    if [ "$mode" = url ]; then
        items+=$(printf '  <NativeUrlReference Include="%s" Sha256="%s" />' "$url" "$sha256")$'\n'
    else
        gh release download "$tag" --repo "$repository" --pattern "$archive" --dir "$dir" --clobber
        if [ "$(sha256sum "$dir/$archive" | cut -d ' ' -f 1)" != "$sha256" ]; then
            rm -f "$dir/$archive"
            echo "native-references: the SHA-256 of the downloaded $archive does not match the release" >&2
            exit 1
        fi
        items+=$(printf '  <NativeFileReference Include="%s/%s" />' "$dir" "$archive")$'\n'
    fi
done
printf '<ItemGroup>\n  <!-- XDL_wgpu %s -->\n%s</ItemGroup>\n' "$tag" "$items"
