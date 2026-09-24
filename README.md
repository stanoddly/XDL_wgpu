# XDL_wgpu

A WebGPU implementation of SDL3's GPU API (`SDL_GPU*`) for `wasm32-emscripten`, shipped as a static library that replaces stock SDL's empty GPU backend at link time.

Stock SDL3 has no browser GPU backend: on Emscripten `SDL_GetNumGPUDrivers()` returns 0 and `SDL_CreateGPUDevice()` fails. `libXDL_wgpu.a` defines every public `SDL_GPU*` function on top of the [emdawnwebgpu](https://github.com/google/dawn/blob/main/src/emdawnwebgpu/README.md) port. Your application keeps stock SDL and its existing calls; native platforms are untouched.

The backend is `src/SDL_gpu_webgpu.c`: the original WebGPU backend by [The Stickmahn](https://github.com/TheeStickmahn) ([TheeStickmahn/SDL_wgpu](https://github.com/TheeStickmahn/SDL_wgpu), a fork of SDL), with substantial further changes by Stan (stanoddly), adapted here to build outside the SDL tree. The same backend is under review upstream in [libsdl-org/SDL#16020](https://github.com/libsdl-org/SDL/pull/16020). This repository is a standalone library, not a fork of SDL and not tracking one.

## Link order rule

`libXDL_wgpu.a` must come before `libSDL3.a` on the link line:

```
emcc app.c -lXDL_wgpu -lSDL3 --use-port=emdawnwebgpu -sASYNCIFY -o app.html
```

Why it works: on Emscripten SDL's dynamic API is off, so `SDL_GPU*` are plain symbols, and `libSDL3.a(SDL_gpu.c.o)` is the only object that defines them. `wasm-ld` extracts an archive member only to satisfy an undefined reference, so when `libXDL_wgpu.a` has already defined every `SDL_GPU*` symbol, the stock member is never pulled in.

The wrong order links silently, with stock SDL winning and `SDL_CreateGPUDevice()` failing at runtime. To verify the order, add `-Wl,--trace-symbol=SDL_CreateGPUDevice` and check that the definition comes from `libXDL_wgpu.a(SDL_gpu.c.o)`, or add `-Wl,-Map=app.map` and check that `libSDL3.a(SDL_gpu.c.o)` is absent.

If a future SDL release adds `SDL_GPU*` functions, the link fails with `duplicate symbol` errors naming them. That is the intended signal to add the functions here.

## SDL version

The library is built against SDL `release-3.4.16` (git submodule `external/SDL`) and links against any 3.4.x `libSDL3.a`: the public GPU function set and the internal helpers the backend uses (`SDL_CreateHashTable` family, `SDL_GetVideoDevice`, `SDL_DebugLogBackend`) are unchanged from 3.4.0 to 3.4.16. SDL 3.2.x is not supported.

`src/SDL_gpu.c` and `src/SDL_sysgpu.h` are copies from the pinned SDL and are owned by this repository. `SDL_gpu.c` has three changes: it includes `XDL_wgpu.h`, its backend table is `{ &WebGPUDriver, NULL }`, and it maps `SDL_GPU_SHADERFORMAT_WGSL` to `SDL_PROP_GPU_DEVICE_CREATE_SHADERS_WGSL_BOOLEAN`. `SDL_sysgpu.h` has one: it declares `WebGPUDriver`.

## Shader format: WGSL

Stock `SDL_gpu.h` has no WGSL shader format. `include/XDL_wgpu.h` adds `SDL_GPU_SHADERFORMAT_WGSL` (`1u << 6`, the value used by the SDL_wgpu fork and the upstream PR), and this library's `SDL_gpu.c` understands it:

```c
#include <XDL_wgpu.h>

SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_WGSL, debug, NULL);

SDL_GPUShaderCreateInfo info = { .code = wgsl_source, .code_size = wgsl_size, .format = SDL_GPU_SHADERFORMAT_WGSL, .entrypoint = "main", ... };
```

`SDL_GPU_SHADERFORMAT_PRIVATE` keeps its stock meaning and is not accepted by this backend. Bindings that cannot include the header define the bit themselves (`1u << 6`).

`SDL_HINT_GPU_DRIVER` works as usual; the driver name is `webgpu`.

## Constraints

### Blocking calls need asyncify (or JSPI)

WebGPU is asynchronous. The backend's adapter and device requests, `SDL_WaitForGPUFences`, `SDL_WaitForGPUIdle` and `SDL_WaitAndAcquireGPUSwapchainTexture` spin on `SDL_DelayNS`, which yields to the browser event loop only under `-sASYNCIFY` (or `-sJSPI`). Without it these calls never return. Link your application with `-sASYNCIFY`.

### Non-blocking alternative

If you cannot use asyncify (for example the .NET browser runtime):

1. Obtain the `WGPUInstance`, `WGPUAdapter` and `WGPUDevice` yourself, asynchronously, and hand all three to `SDL_CreateGPUDeviceWithProperties()` together with `SDL_PROP_GPU_DEVICE_CREATE_SHADERS_WGSL_BOOLEAN`. This skips the request loops. The property names are in `include/XDL_wgpu.h`:

   | Property | Type | String |
   |---|---|---|
   | instance | pointer | `SDL.gpu.device.create.webgpu.instance` |
   | adapter | pointer | `SDL.gpu.device.create.webgpu.adapter` |
   | device | pointer | `SDL.gpu.device.create.webgpu.device` |
   | bind group cache expiry | number | `SDL.gpu.device.create.webgpu.bindgroupexpiry` |

   The device requires the adapter and the adapter requires the instance. The backend takes its own reference to each and releases only that reference on `SDL_DestroyGPUDevice()`.

2. Per frame, use only `SDL_AcquireGPUSwapchainTexture()` (returns a NULL texture instead of blocking) and `SDL_QueryGPUFence()`. Do not call the `Wait*` functions.

Under asyncify, note that `WaitAndAcquire` inside a `requestAnimationFrame` callback pauses that callback until the wait completes; this is how SDL's main callbacks behave on Emscripten.

## Building

Requirements: CMake 3.24+, Emscripten 6.0.3 (the version pinned to Dawn `v20260423.175430` for the `emdawnwebgpu` port), a network connection for the first build (the port is downloaded), and Node.js plus a Chromium for the test.

If you have the .NET 11 `wasm-tools` workload, `tools/emsdk-env.sh` puts its bundled Emscripten on `PATH` and redirects the (read-only) Emscripten cache to `build/em-cache`:

```
git submodule update --init
source tools/emsdk-env.sh        # or use your own emsdk
emcmake cmake -S . -B build/out -DCMAKE_BUILD_TYPE=Release
cmake --build build/out
```

Outputs: `build/out/libXDL_wgpu.a`, the stock `build/out/external/SDL/libSDL3.a` it was built against, and `build/out/test/test.html`.

Options:

1. `XDL_SDL_SOURCE_DIR`: another SDL 3.4.x source tree instead of the submodule.
2. `XDL_EMSCRIPTEN_FLAGS`: extra emcc flags for compiling the library, for example `-DXDL_EMSCRIPTEN_FLAGS="-fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0"` for the .NET browser runtime. Pass `-DXDL_BUILD_TEST=OFF` with it; the test needs asyncify, which is incompatible with `-fwasm-exceptions`. Set `CMAKE_C_FLAGS` too if the SDL build needs the same flags.
3. `XDL_BUILD_SDL_LIBRARIES`: also configure SDL3_image, SDL3_mixer and SDL3_ttf from `external/` against the same SDL (targets `SDL3_image-static`, `SDL3_mixer-static`, `SDL3_ttf-static`, `freetype`, `harfbuzz`, `plutosvg`, `plutovg`). SDL_image and SDL_mixer use only their built-in codecs, so they need no submodules of their own; SDL_ttf needs `git -C external/SDL_ttf submodule update --init external/freetype external/harfbuzz external/plutosvg external/plutovg`.

### Test

`test/test.c` uses only public headers (SDL3 and `XDL_wgpu.h`). It prints the driver list, checks that `SDL_GPU_SHADERFORMAT_PRIVATE` is rejected, creates a device with `SDL_GPU_SHADERFORMAT_WGSL`, claims a window, clears textures to (1, 0.5, 0.25, 1) and reads them back (64x4 RGBA8 direct; 5x3 RGBA8, 3x2 R8 and a 64x1 RGBA8 at buffer offset 2 through the padded staging path), then submits three swapchain frames.

```
tools/run-test.sh
```

The script serves `build/out/test` and drives headless Chromium through `playwright-core` (installed into `build/node` on first use), relaying the page's console. Expected:

```
SDL_GetNumGPUDrivers() = 1
GPU driver 0: webgpu
SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_PRIVATE) rejected: No supported SDL_GPU backend found!
Device shader formats: 0x40
Device driver: webgpu
Readback 64x4: 255 128 64 255 (uniform: yes)
Readback 5x3 last pixel: 255 128 64 255 (uniform: yes)
Readback R8 3x2: 255 255 255 / 255 255 255
Readback 64x1 at offset 2: 255 128 64 255 (uniform: yes)
Frame 1 submitted
Frame 2 submitted
Frame 3 submitted
XDL_wgpu test PASSED
```

It uses the Chromium from `~/.cache/ms-playwright` or `XDL_CHROMIUM`. Do not use Chromium's `--virtual-time-budget` for this page: virtual time runs pending timers ahead of `requestAnimationFrame`, so the asyncify poll loop starves frame presentation and any wait after a swapchain submit never completes.

The link also writes `build/out/test/test.map` and traces `SDL_CreateGPUDevice` so the link order can be checked as described above.

## Releases

The [Build workflow](.github/workflows/build.yml) runs on every pull request. It builds the archives with the Emscripten of the .NET 11 `wasm-tools` workload and the runtime's flags (`-fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0`), runs the smoke test and links the archives into a .NET browser app (`test/dotnet`). The [Release workflow](.github/workflows/release.yml) is started by hand; it runs Build and publishes its archives as a GitHub release. [MAINTAINING.md](MAINTAINING.md) describes releasing and updating the libraries and the toolchain. `tools/build-release.sh <tag> <owner/repo>` does the build and staging locally: it configures `build/release` from scratch and replaces `build/release-assets`.

- The version is the released commit's UTC commit time, `vYYYYMMDD.HHMMSS`, as Dawn tags its releases. A run stops when the tag exists, so a second run for the same commit publishes nothing.
- Assets: `libXDL_wgpu.a`, `SDL3.a`, `SDL3_image.a`, `SDL3_mixer.a`, `SDL3_ttf.a` and `THIRD-PARTY-NOTICES.txt`. `SDL3_ttf.a` also contains the libraries SDL_ttf vendors (FreeType, HarfBuzz, plutosvg and plutovg), so it links on its own. GitHub shows the SHA-256 of each asset.
- Library versions: SDL `release-3.4.16`, SDL_image `release-3.4.6`, SDL_mixer `release-3.2.4`, SDL_ttf `release-3.2.2`, each a submodule under `external/`.
- SDL_image loads ANI, BMP, GIF, JPEG (stb_image), LBM, PCX, PNG (SDL's codec), PNM, QOI, SVG, TGA, XCF, XPM and XV; AVIF, JXL, TIFF and WebP are off. SDL_mixer plays WAVE, AIFF, VOC, AU, FLAC (dr_flac), MP3 (dr_mp3), Ogg Vorbis (stb_vorbis) and MIDI (TiMidity); Opus, MOD, GME and WavPack are off.
- `THIRD-PARTY-NOTICES.txt` holds the license of each library; the FreeType acknowledgment; the notices of third-party code compiled into the libraries that their own licenses do not cover, copied verbatim from the pinned sources; and, per library, every distinct copyright line of the sources and headers the compilers read (from the `.o.d` files under `build/release`), which licenses such as HarfBuzz's ask to keep and which a summary such as HarfBuzz's `COPYING` does not list in full. The build fails when a notice is missing or empty. After updating a submodule, look for new license texts as MAINTAINING.md describes; the copyright lines follow by themselves. `LICENSES/Unicode-3.0.txt` is a copy of the Unicode license, which no pinned source carries.
- The release notes list the Emscripten version, the flags, and the archives in link order with the version and commit of each library's source.

## Using from .NET (browser-wasm)

Reference the archives with `NativeFileReference`, XDL_wgpu first. Keep the stock archive's file name `SDL3.a`: the .NET WASM toolchain derives the P/Invoke module name from the file name, so `SDL3.a` registers the `"SDL3"` module your `DllImport("SDL3")` calls bind to. `libXDL_wgpu.a` registers nothing under its own name, which is fine, because its symbols are resolved into the same static link.

```xml
<ItemGroup>
  <NativeFileReference Include="native/libXDL_wgpu.a" />
  <NativeFileReference Include="native/SDL3.a" />
</ItemGroup>
<PropertyGroup>
  <EmccExtraLDFlags>--use-port=emdawnwebgpu</EmccExtraLDFlags>
</PropertyGroup>
```

`tools/native-references.sh [--url | --file <dir>] [tag]` prints the archives of a release, the latest without a tag, as these items in link order. `--file <dir>` downloads them into `<dir>` first and checks their SHA-256; `--url` prints `NativeUrlReference` items with each archive's URL and SHA-256, for an MSBuild target that downloads them. It needs `gh`, authenticated while the repository is private.

The release archives are built for this. To build them yourself, use the same exception flags as the runtime (`-DXDL_EMSCRIPTEN_FLAGS="-fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0"`). Since the .NET runtime does not use asyncify, create the device through the adopt path and use the non-blocking per-frame calls described above.

## Layout

| Path | Contents |
|---|---|
| `include/XDL_wgpu.h` | `SDL_GPU_SHADERFORMAT_WGSL` and the WebGPU device-creation properties |
| `src/SDL_gpu.c`, `src/SDL_sysgpu.h` | SDL 3.4.16's GPU front end; backend table is `{ &WebGPUDriver, NULL }`, WGSL format mapped to its property |
| `src/SDL_gpu_webgpu.c` | The WebGPU backend, adapted to the port header and the pinned vtable (see [Changes to the backend](#changes-to-the-backend)) |
| `src/XDL_wgpu_surface.c` | `WGPUSurface` from the window's Emscripten canvas selector |
| `test/` | Smoke test page |
| `test/dotnet/` | .NET browser app that links the release archives and binds each library |
| `tools/` | Emscripten environment, test runner, release build script and `native-references.sh` |
| `external/SDL` | SDL submodule at `release-3.4.16` |
| `external/SDL_image`, `external/SDL_mixer`, `external/SDL_ttf` | Submodules at `release-3.4.6`, `release-3.2.4` and `release-3.2.2`, built with `XDL_BUILD_SDL_LIBRARIES` |
| `.github/workflows/build.yml`, `.github/workflows/release.yml` | The build-and-test workflow for pull requests and releases, and the release workflow |
| `MAINTAINING.md` | Releasing, and updating the pinned libraries and the toolchain |
| `LICENSES/Unicode-3.0.txt` | The Unicode license, for HarfBuzz's tables generated from Unicode data |

## Changes to the backend

Relative to the backend as copied into this repository:

1. Includes the Emscripten port's `<webgpu/webgpu.h>` and this repository's `XDL_wgpu.h` instead of the fork's bundled `webgpu.h` and SDL header additions.
2. Surface creation calls `XDL_WGPU_CreateSurface()` (in `src/XDL_wgpu_surface.c`) instead of the `SDL_WGPU_CreateSurface()` video-backend hook, which does not exist outside the fork, and sets `selector.length`, which the original leaves unset.
3. The four OpenXR vtable functions are removed, because SDL 3.4.16's `SDL_sysgpu.h` has no XR slots, and the `WGPUFeatureName_SubgroupSizeControl` case is dropped, because the pinned port header does not declare it.
4. `WEBGPU_PrepareDriver()` requires `SDL_PROP_GPU_DEVICE_CREATE_SHADERS_WGSL_BOOLEAN`, like the other SDL backends require their own shader format.
5. `WEBGPU_DownloadFromTexture()` was rewritten: it passed `source->layer` as `depthOrArrayLayers` (0 for 2D, copying nothing), ignored `mip_level` and `layer`, ignored `pixels_per_row` / `rows_per_layer` and wrote 256-byte-padded rows into the app's buffer, and did not reference-count the resources it used. Downloads now copy into a staging buffer and are repacked into the app's layout when the transfer buffer is mapped; `WEBGPU_DownloadFromBuffer()` goes through the same path so downloads replay in submission order.

## License

zlib, the same as SDL (see `LICENSE`).

`src/SDL_gpu.c`, `src/SDL_sysgpu.h` and `src/SDL_gpu_webgpu.c` are derived works and keep their notices. The first two are copied from [libsdl-org/SDL](https://github.com/libsdl-org/SDL). `src/SDL_gpu_webgpu.c` is the original WebGPU backend by The Stickmahn ([TheeStickmahn/SDL_wgpu](https://github.com/TheeStickmahn/SDL_wgpu)) with substantial further changes by Stan (stanoddly); it keeps its upstream file name so it can still be diffed against the original and against [libsdl-org/SDL#16020](https://github.com/libsdl-org/SDL/pull/16020).

## AI disclosure

Except for the code derived from SDL and the original WebGPU backend (see [License](#license)), the work in this repository was produced with the help of LLM-based coding tools, under human direction and review. They wrote and reviewed code, and their review findings shaped the design.
