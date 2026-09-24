# Maintaining XDL_wgpu

How to release, test, and update the pinned libraries and the toolchain. [README.md](README.md) describes what the library and the releases contain.

## Workflows

| Workflow | Runs | Does |
|---|---|---|
| [Build](.github/workflows/build.yml) | On every pull request to `main`, and inside Release | Checks out the pinned libraries, installs the .NET 11 preview SDK and its `wasm-tools` workload, runs the Chromium smoke test, builds the release assets with `tools/build-release.sh`, links them into the .NET browser app in `test/dotnet`, and uploads them as the `release-assets` artifact |
| [Release](.github/workflows/release.yml) | By hand | Computes the tag, stops if it exists, runs Build, and publishes the artifact as a GitHub release. Only its last job has a token that can write |

A pull request's `release-assets` artifact is what a release of that commit would publish, so it can be downloaded and tried before merging.

## Release

1. Merge the pull request to `main`.
2. In GitHub: Actions → Release → Run workflow, on `main`.
3. The tag is the released commit's UTC commit time, `vYYYYMMDD.HHMMSS`. A commit can be released once; a second run stops at the existing tag. To release again, commit something.
4. The release notes list each archive as a Pixely `NativeUrlReference` with its SHA-256. They download without credentials only when the repository is public.

A failed run publishes nothing. Fix the cause on a branch and run Release again after merging.

## Run the checks locally

Needs the .NET 11 `wasm-tools` workload, Node.js and a Chromium (see README, Building).

```
git submodule update --init
git -C external/SDL_ttf submodule update --init external/freetype external/harfbuzz external/plutosvg external/plutovg

# Smoke test
source tools/emsdk-env.sh
emcmake cmake -S . -B build/out -DCMAKE_BUILD_TYPE=Release
cmake --build build/out --parallel "$(nproc)"
tools/run-test.sh

# Release assets, into build/release-assets
tools/build-release.sh vTEST owner/repo
```

Then, in a new shell without `tools/emsdk-env.sh`, the .NET link test:

```
dotnet publish test/dotnet -c Release -p:XdlArchiveDirectory="$PWD/build/release-assets" -p:WasmCachePath="$PWD/build/dotnet-em-cache"
node test/dotnet/run.mjs test/dotnet/bin/Release/net11.0/publish/wwwroot
```

Expected last line: `XDL_wgpu link test PASSED`. Keep `WasmCachePath` apart from `build/em-cache`: a .NET link into the cache that `tools/emsdk-env.sh` uses emptied it once, emdawnwebgpu port included, and the next smoke-test build failed.

## Update SDL_image, SDL_mixer or SDL_ttf

1. Move the submodule to the new release tag:

   ```
   git -C external/SDL_image fetch --tags
   git -C external/SDL_image checkout release-X.Y.Z
   ```

   For SDL_ttf, also update its vendored libraries: `git -C external/SDL_ttf submodule update --init external/freetype external/harfbuzz external/plutosvg external/plutovg`.
2. Run `tools/build-release.sh vTEST owner/repo`.
   - If a CMake option changed name, the configure fails or the codec summary (`SDL3_image backends`, `SDL3_mixer backends`) changes. Compare it with the codec lists in README, Releases.
   - If a copied notice moved or changed, the script fails with "the notice of … is missing or empty". Fix its start and end patterns in `tools/build-release.sh`.
3. Look for new third-party licenses. The copyright lines in `THIRD-PARTY-NOTICES.txt` follow by themselves, but a new license text or new code derived from Unicode data needs its own entry in `tools/build-release.sh`. `build/release/compiled-sources.txt` lists every compiled source and header. This lists the ones with a license text other than their library's standard header:

   ```
   xargs grep -l -i -E "licen[cs]e|permission is hereby|public domain|unicode, inc|unicode\.org" < build/release/compiled-sources.txt \
     | xargs grep -L -E "Sam Lantinga|the FreeType project|without written agreement and without" | sed "s|$PWD/||" | sort
   ```

   Save its output before the update and compare it with the output after; review only the new files. At the current pins it lists 83 files, all accounted for: the notices in `tools/build-release.sh`, the libraries' own licenses (plutovg, plutosvg, TiMidity, FreeType's Adobe code under the FreeType License), and public-domain code (stb, dr_libs, miniz, tiny_jpeg). Public-domain code and zlib-licensed code need no entry; their licenses ask for no notice in a binary.
4. Run the .NET link test.
5. Update the versions in README: Releases ("Library versions") and the Layout table.
6. Commit the submodule and open a pull request. Build runs on it.

## Update SDL

As above, and also:

1. `src/SDL_gpu.c` and `src/SDL_sysgpu.h` are copies from the pinned SDL with one change each (see README, Layout). Diff them against the new release's `src/gpu/` and carry over its changes.
2. If the new SDL adds `SDL_GPU*` functions, the smoke test's link fails with `duplicate symbol` errors that name them. Add them to the backend.
3. SDL 3.4.x is the supported range. A new minor version (3.6) is a larger change: the vtable in `SDL_sysgpu.h` and the backend's functions may change.
4. Update the SDL version in README: SDL version, Releases, the Layout table and Changes to the backend.

## Change the codecs

The codecs of SDL_image and SDL_mixer are the `set(SDLIMAGE_…)` and `set(SDLMIXER_…)` lines under `XDL_BUILD_SDL_LIBRARIES` in `CMakeLists.txt`. The build uses only codecs that need no extra library, so each release has nine archives. A codec that needs a library (libwebp, libopus and others) needs the library's submodule, its archive in `tools/build-release.sh`, the test project and the release notes, and its license. Update the codec lists in README, Releases.

## .NET and Emscripten

- Build installs the newest .NET 11 preview SDK, so its Emscripten can change between two releases without a commit. `versions.json` in each release records the Emscripten version and the flags.
- The archives link only into a .NET runtime built with the same Emscripten. A new .NET major version needs a new `dotnet-version` in `build.yml` and a new `TargetFramework` in `test/dotnet`.
- Emscripten pins the Dawn version of the emdawnwebgpu port, so a new Emscripten can change the WebGPU header the backend compiles against. A compile error in `src/SDL_gpu_webgpu.c` after a .NET update is the first sign. Update the Emscripten and Dawn versions in README, Building.
- The release flags (`-fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0`) match the .NET runtime's. If .NET changes its exception handling, change `flags` in `tools/build-release.sh`.

## Where the pinned versions are written

| What | Where |
|---|---|
| SDL, SDL_image, SDL_mixer, SDL_ttf | Submodules under `external/`; README: SDL version, Releases, Layout, Changes to the backend |
| FreeType, HarfBuzz, plutosvg, plutovg | SDL_ttf's submodules; `versions.json` of each release |
| Emscripten, Dawn | The .NET workload; README: Building |
| playwright-core | `build.yml` |
