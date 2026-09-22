# XDL_wgpu

A WebGPU implementation of SDL3's GPU API (`SDL_GPU*`) for `wasm32-emscripten`, shipped as a static library that replaces stock SDL's empty GPU backend at link time.

Stock SDL3 has no browser GPU backend: on Emscripten `SDL_GetNumGPUDrivers()` returns 0 and `SDL_CreateGPUDevice()` fails. `libXDL_wgpu.a` defines every public `SDL_GPU*` function on top of the [emdawnwebgpu](https://github.com/google/dawn/blob/main/src/emdawnwebgpu/README.md) port. Your application keeps stock SDL and its existing calls; native platforms are untouched.

The backend is `src/SDL_gpu_webgpu.c` from [stanoddly/SDL_wgpu](https://github.com/stanoddly/SDL_wgpu) (commit a85f6ce6c), which is the backend under review upstream in [libsdl-org/SDL#16020](https://github.com/libsdl-org/SDL/pull/16020).

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

`src/SDL_gpu.c` and `src/SDL_sysgpu.h` are copies from the pinned SDL with one change (the backend table) and are owned by this repository.

## Shader format: PRIVATE means WGSL

Stock `SDL_gpu.h` has no WGSL shader format, so this backend uses `SDL_GPU_SHADERFORMAT_PRIVATE` for WGSL:

```c
SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_PRIVATE, debug, NULL);

SDL_GPUShaderCreateInfo info = { .code = wgsl_source, .code_size = wgsl_size, .format = SDL_GPU_SHADERFORMAT_PRIVATE, .entrypoint = "main", ... };
```

`SDL_HINT_GPU_DRIVER` works as usual; the driver name is `webgpu`.

## Constraints

### Blocking calls need asyncify (or JSPI)

WebGPU is asynchronous. The backend's adapter and device requests, `SDL_WaitForGPUFences`, `SDL_WaitForGPUIdle` and `SDL_WaitAndAcquireGPUSwapchainTexture` spin on `SDL_DelayNS`, which yields to the browser event loop only under `-sASYNCIFY` (or `-sJSPI`). Without it these calls never return. Link your application with `-sASYNCIFY`.

### Non-blocking alternative

If you cannot use asyncify (for example the .NET browser runtime):

1. Obtain the `WGPUInstance`, `WGPUAdapter` and `WGPUDevice` yourself, asynchronously, and hand all three to `SDL_CreateGPUDeviceWithProperties()`. This skips the request loops. The property names are not in stock `SDL_gpu.h`; use the strings directly:

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

### Test

`test/test.c` uses only public SDL headers. It prints the driver list, creates a device with `SDL_GPU_SHADERFORMAT_PRIVATE`, claims a window, clears a 64x4 (and a 5x3) RGBA8 texture to (1, 0.5, 0.25, 1) and reads it back, then submits three swapchain frames.

```
tools/run-test.sh
```

The script serves `build/out/test` and drives headless Chromium through `playwright-core` (installed into `build/node` on first use), relaying the page's console. Expected:

```
SDL_GetNumGPUDrivers() = 1
GPU driver 0: webgpu
Device driver: webgpu
Readback 64x4: 255 128 64 255 (uniform: yes)
Readback 5x3 last pixel: 255 128 64 255 (uniform: yes)
Frame 1 submitted
Frame 2 submitted
Frame 3 submitted
XDL_wgpu test PASSED
```

It uses the Chromium from `~/.cache/ms-playwright` or `XDL_CHROMIUM`. Do not use Chromium's `--virtual-time-budget` for this page: virtual time runs pending timers ahead of `requestAnimationFrame`, so the asyncify poll loop starves frame presentation and any wait after a swapchain submit never completes.

The link also writes `build/out/test/test.map` and traces `SDL_CreateGPUDevice` so the link order can be checked as described above.

## Using from .NET (browser-wasm)

Reference both archives with `NativeFileReference`, XDL_wgpu first. Keep the stock archive's file name `SDL3.a`: the .NET WASM toolchain derives the P/Invoke module name from the file name, so `SDL3.a` registers the `"SDL3"` module your `DllImport("SDL3")` calls bind to. `libXDL_wgpu.a` registers nothing under its own name, which is fine, because its symbols are resolved into the same static link.

```xml
<ItemGroup>
  <NativeFileReference Include="native/libXDL_wgpu.a" />
  <NativeFileReference Include="native/SDL3.a" />
</ItemGroup>
<PropertyGroup>
  <EmccExtraLDFlags>--use-port=emdawnwebgpu</EmccExtraLDFlags>
</PropertyGroup>
```

Build the library with the same exception flags as the runtime (`-DXDL_EMSCRIPTEN_FLAGS="-fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0"`), and since the .NET runtime does not use asyncify, create the device through the adopt path and use the non-blocking per-frame calls described above.

## Layout

| Path | Contents |
|---|---|
| `src/SDL_gpu.c`, `src/SDL_sysgpu.h` | SDL 3.4.16's GPU front end; backend table is `{ &WebGPUDriver, NULL }` |
| `src/SDL_gpu_webgpu.c` | The WebGPU backend from SDL_wgpu, adapted to the port header and the pinned vtable |
| `src/SDL_wgpu_surface.c` | `WGPUSurface` from the window's Emscripten canvas selector |
| `test/` | Smoke test page |
| `tools/` | Emscripten environment and test runner scripts |
| `external/SDL` | SDL submodule at `release-3.4.16` |

## License

MIT (see `LICENSE`). `src/SDL_gpu.c` and `src/SDL_sysgpu.h` are copied from SDL and keep their zlib notices; `src/SDL_gpu_webgpu.c` comes from the SDL_wgpu fork of SDL and is zlib as well.
