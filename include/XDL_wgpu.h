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
