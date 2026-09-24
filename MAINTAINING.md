# Maintaining XDL_wgpu

How to release, test, and update the pinned libraries and the toolchain. [README.md](README.md) describes what the library and the releases contain.

## Workflows

| Workflow | Runs | Does |
|---|---|---|
| [Build](.github/workflows/build.yml) | On every pull request to `main`, and inside Release | Checks out the pinned libraries, installs the .NET 11 preview SDK and its `wasm-tools` workload, runs the Chromium smoke test, builds the release assets with `tools/build-release.sh`, links them into the .NET browser app in `test/dotnet`, and uploads them as the `release-assets` artifact |
| [Release](.github/workflows/release.yml) | By hand | Computes the tag, stops if it exists, runs Build, and publishes the artifact as a GitHub release. Only its last job has a token that can write |

A pull request's `release-assets` artifact is a preview: the archives are the ones a release of the merged commit would publish, so they can be downloaded and tried before merging. Its tag is the placeholder `pr-<number>`, and its `release-notes.md` names GitHub's test merge commit, so its release notes are not final.

## Release

1. Merge the pull request to `main`.
2. In GitHub: Actions → Release → Run workflow, on `main`.
3. The tag is the released commit's UTC commit time, `vYYYYMMDD.HHMMSS`. A commit can be released once; a second run stops at the existing tag. To release again, commit something.
4. The release notes list the Emscripten version, the flags, and the archives in link order with each library's version and commit. `tools/native-references.sh` prints a release's archives as MSBuild items. The archives download without credentials only when the repository is public.

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
dotnet publish test/dotnet -c Release -o build/dotnet-link -p:XdlArchiveDirectory="$PWD/build/release-assets" -p:WasmCachePath="$PWD/build/dotnet-em-cache"
node test/dotnet/run.mjs build/dotnet-link/wwwroot
```

Expected last line: `XDL_wgpu link test PASSED`. Keep `WasmCachePath` apart from `build/em-cache`: a .NET link into the cache that `tools/emsdk-env.sh` uses emptied it once, emdawnwebgpu port included, and the next smoke-test build failed.

## Update SDL_image, SDL_mixer or SDL_ttf

1. Before the update, record the current notices and license texts:

   ```
   tools/build-release.sh vTEST owner/repo
   cp build/release-assets/THIRD-PARTY-NOTICES.txt build/notices-before.txt
   xargs grep -l -i -E "licen[cs]e|copyright|permission to use|permission is hereby|freely granted|public domain|unicode\.org" \
     < build/release/compiled-sources.txt | sed "s|^$(pwd -P)/||" | sort > build/license-files-before.txt
   ```

2. Move the submodule to the new release tag:

   ```
   git -C external/SDL_image fetch --tags
   git -C external/SDL_image checkout release-X.Y.Z
   ```

   For SDL_ttf, also update its vendored libraries: `git -C external/SDL_ttf submodule update --init external/freetype external/harfbuzz external/plutosvg external/plutovg`.
3. Run `tools/build-release.sh vTEST owner/repo` again.
   - If a CMake option changed name, the configure fails or the codec summary (`SDL3_image backends`, `SDL3_mixer backends`) changes. Compare it with the codec lists in README, Releases.
   - If a copied notice moved or changed, the script fails with "the notice of … is missing or empty". Fix its start and end patterns in `tools/build-release.sh`.
4. Look for new third-party licenses. Run the `xargs grep` of step 1 into `build/license-files-after.txt`, then compare:

   ```
   diff build/notices-before.txt build/release-assets/THIRD-PARTY-NOTICES.txt
   diff build/license-files-before.txt build/license-files-after.txt
   ```

   The copyright lines in `THIRD-PARTY-NOTICES.txt` follow by themselves, and every compiled file's copyright holders are listed there. A new holder or a new file with a license text points to code to read. If its license asks to keep its notice or permission text in copies or documentation, and the library's own license does not cover it, add a `notice` or `notice_file` entry to `tools/build-release.sh`. Public-domain code and zlib-licensed code need no entry; their licenses ask for no notice in a binary. Code derived from Unicode data (tables generated from the Unicode Character Database or emoji data) is covered by `LICENSES/Unicode-3.0.txt`; extend that entry's title if a new library has such tables.
5. Run the .NET link test.
6. Update the versions in README: Releases ("Library versions") and the Layout table.
7. Commit the submodule and open a pull request. Build runs on it.

## Update SDL

As above, and also:

1. `src/SDL_gpu.c` and `src/SDL_sysgpu.h` are copies from the pinned SDL. Diff them against the new release's `src/gpu/`, take its changes, and keep this repository's: in `SDL_gpu.c`, the `XDL_wgpu.h` include, the backend table `{ &WebGPUDriver, NULL }` and the mapping of `SDL_GPU_SHADERFORMAT_WGSL` to `SDL_PROP_GPU_DEVICE_CREATE_SHADERS_WGSL_BOOLEAN`; in `SDL_sysgpu.h`, the `WebGPUDriver` declaration. `diff external/SDL/src/gpu/SDL_gpu.c src/SDL_gpu.c` shows them.
2. If the new SDL adds `SDL_GPU*` functions, the smoke test's link fails with `duplicate symbol` errors that name them. Add them to the backend.
3. SDL 3.4.x is the supported range. A new minor version (3.6) is a larger change: the vtable in `SDL_sysgpu.h` and the backend's functions may change.
4. Update the SDL version in README: SDL version, Releases, the Layout table and Changes to the backend.

## Change the codecs

The codecs of SDL_image and SDL_mixer are the `set(SDLIMAGE_…)` and `set(SDLMIXER_…)` lines under `XDL_BUILD_SDL_LIBRARIES` in `CMakeLists.txt`. The build uses only codecs that need no extra library, so each release has five archives. A codec that needs a library (libwebp, libopus and others) needs the library's submodule; its archive merged into `SDL3_image.a` or `SDL3_mixer.a` in `tools/build-release.sh`, as `SDL3_ttf.a` holds SDL_ttf's libraries; its row in the release notes; and its license and copyright lines in `THIRD-PARTY-NOTICES.txt`. Update the codec lists in README, Releases.

## .NET and Emscripten

- Build installs the newest .NET 11 preview SDK, so its Emscripten can change between two releases without a commit. The release notes of each release record the Emscripten version and the flags.
- The archives link only into a .NET runtime built with the same Emscripten. A new .NET major version needs a new `dotnet-version` in `build.yml` and a new `TargetFramework` in `test/dotnet`.
- Emscripten pins the Dawn version of the emdawnwebgpu port, so a new Emscripten can change the WebGPU header the backend compiles against. A compile error in `src/SDL_gpu_webgpu.c` after a .NET update is the first sign. Update the Emscripten and Dawn versions in README, Building.
- The release flags (`-fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0`) match the .NET runtime's. If .NET changes its exception handling, change `flags` in `tools/build-release.sh`.

## Where the pinned versions are written

| What | Where |
|---|---|
| SDL, SDL_image, SDL_mixer, SDL_ttf | Submodules under `external/`; README: SDL version, Releases, Layout, Changes to the backend |
| FreeType, HarfBuzz, plutosvg, plutovg | SDL_ttf's submodules; the release notes of each release |
| Emscripten, Dawn | The .NET workload; README: Building |
| playwright-core | `build.yml` |
