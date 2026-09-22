/*
  XDL_wgpu
  Copyright (C) 2026 Stan

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
// Public additions of XDL_wgpu on top of stock SDL3's SDL_gpu.h. Values match the SDL_wgpu fork of SDL.
#ifndef XDL_wgpu_h_
#define XDL_wgpu_h_

#include <SDL3/SDL_gpu.h>

// WGSL shader source for the WebGPU backend; pass to SDL_CreateGPUDevice() and SDL_GPUShaderCreateInfo.format.
#define SDL_GPU_SHADERFORMAT_WGSL (1u << 6)

#define SDL_PROP_GPU_DEVICE_CREATE_SHADERS_WGSL_BOOLEAN "SDL.gpu.device.create.shaders.wgsl"

// Adopt an existing WGPUInstance/WGPUAdapter/WGPUDevice (all three) instead of requesting them; skips the blocking request loops.
#define SDL_PROP_GPU_DEVICE_CREATE_WEBGPU_INSTANCE_POINTER "SDL.gpu.device.create.webgpu.instance"
#define SDL_PROP_GPU_DEVICE_CREATE_WEBGPU_ADAPTER_POINTER  "SDL.gpu.device.create.webgpu.adapter"
#define SDL_PROP_GPU_DEVICE_CREATE_WEBGPU_DEVICE_POINTER   "SDL.gpu.device.create.webgpu.device"

// Number of submits after which an unused cached bind group is freed; 0 disables caching, -1 disables pruning.
#define SDL_PROP_GPU_DEVICE_CREATE_WEBGPU_BINDGROUP_EXPIRE_AFTER_N_SUBMITS "SDL.gpu.device.create.webgpu.bindgroupexpiry"

#endif // XDL_wgpu_h_
