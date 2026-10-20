/*
  XDL_wgpu
  Copyright (C) 2026 Stan (stanoddly)

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
// Smoke test for XDL_wgpu: uses only public SDL3 headers and must be linked with libXDL_wgpu.a before libSDL3.a.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <XDL_wgpu.h>
#include <emscripten.h>

#define TEST_WIDTH 64
#define TEST_HEIGHT 4
#define NARROW_WIDTH 5
#define NARROW_HEIGHT 3
// Enough warm-up frames to fill the backend's pools with its default of 2 frames in flight.
#define WARM_UP_FRAMES 4
#define COUNTED_FRAMES 3
#define FRAMES_TO_SUBMIT (WARM_UP_FRAMES + COUNTED_FRAMES)

typedef struct AppState
{
    SDL_Window *window;
    SDL_GPUDevice *device;
    int frames_submitted;
    int futures_after_init;
    int presented_frame_allocations;
    SDL_GPUTexture *frame_source;
} AppState;

static SDL_malloc_func original_malloc;
static SDL_calloc_func original_calloc;
static SDL_realloc_func original_realloc;
static bool counting_allocations;
static int allocation_count;

static void *SDLCALL CountingMalloc(size_t size)
{
    allocation_count += counting_allocations;
    return original_malloc(size);
}

static void *SDLCALL CountingCalloc(size_t nmemb, size_t size)
{
    allocation_count += counting_allocations;
    return original_calloc(nmemb, size);
}

static void *SDLCALL CountingRealloc(void *mem, size_t size)
{
    allocation_count += counting_allocations;
    return original_realloc(mem, size);
}

// Promises emdawnwebgpu keeps for futures under Asyncify; each one the backend does not drop stays for the life of the page.
static int CountTrackedFutures(void)
{
    return EM_ASM_INT({ return Object.keys(WebGPU.Internals.futures).length; });
}

// Bind group and pipeline layouts emdawnwebgpu tracks; each one the backend does not release stays for the life of the page.
static int CountLiveLayouts(void)
{
    return EM_ASM_INT({ return Object.values(WebGPU.Internals.jsObjects).filter(o => o instanceof GPUBindGroupLayout || o instanceof GPUPipelineLayout).length; });
}

static int CountLiveResources(void)
{
    return EM_ASM_INT({
        return Object.values(WebGPU.Internals.jsObjects).filter(o => o instanceof GPURenderPipeline || o instanceof GPUComputePipeline || o instanceof GPUTexture || o instanceof GPUBuffer).length;
    });
}

static Uint32 BytesPerPixel(SDL_GPUTextureFormat format)
{
    return format == SDL_GPU_TEXTUREFORMAT_R8_UNORM ? 1 : 4;
}

// Clears a width x height texture to (1, 0.5, 0.25, 1) and downloads it to out_pixels + offset, tightly packed.
static bool ClearAndDownload(SDL_GPUDevice *device, SDL_GPUTextureFormat format, Uint32 width, Uint32 height, Uint32 offset, Uint8 *out_pixels)
{
    SDL_GPUTextureCreateInfo texture_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = format,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &texture_info);
    if (!texture) {
        return SDL_SetError("SDL_CreateGPUTexture: %s", SDL_GetError());
    }

    Uint32 byte_count = offset + width * height * BytesPerPixel(format);
    SDL_GPUTransferBufferCreateInfo transfer_info = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = byte_count };
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (!transfer) {
        return SDL_SetError("SDL_CreateGPUTransferBuffer: %s", SDL_GetError());
    }

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUColorTargetInfo color_target = {
        .texture = texture,
        .clear_color = { 1.0f, 0.5f, 0.25f, 1.0f },
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };
    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, NULL);
    SDL_EndGPURenderPass(render_pass);

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region = { .texture = texture, .w = width, .h = height, .d = 1 };
    SDL_GPUTextureTransferInfo destination = { .transfer_buffer = transfer, .offset = offset };
    SDL_DownloadFromGPUTexture(copy_pass, &region, &destination);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence) {
        return SDL_SetError("SDL_SubmitGPUCommandBufferAndAcquireFence: %s", SDL_GetError());
    }
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);

    const Uint8 *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) {
        return SDL_SetError("SDL_MapGPUTransferBuffer: %s", SDL_GetError());
    }
    SDL_memcpy(out_pixels, mapped, byte_count);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    // A second map must see the same bytes.
    mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped || SDL_memcmp(out_pixels, mapped, byte_count) != 0) {
        return SDL_SetError("second map differs from the first");
    }
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, texture);
    return true;
}

// Uploads a width x height x depth RGBA8 3D texture from a transfer buffer whose slices are rows_per_layer rows apart,
// with filler in the extra rows, and downloads it tightly packed to out_pixels.
static bool UploadAndDownload3D(SDL_GPUDevice *device, Uint32 width, Uint32 height, Uint32 depth, Uint32 rows_per_layer, Uint8 *out_pixels)
{
    SDL_GPUTextureCreateInfo texture_info = {
        .type = SDL_GPU_TEXTURETYPE_3D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = width,
        .height = height,
        .layer_count_or_depth = depth,
        .num_levels = 1,
    };
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &texture_info);
    if (!texture) {
        return SDL_SetError("SDL_CreateGPUTexture: %s", SDL_GetError());
    }

    Uint32 row_bytes = width * 4;
    Uint32 upload_bytes = row_bytes * rows_per_layer * depth;
    SDL_GPUTransferBufferCreateInfo upload_info = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = upload_bytes };
    SDL_GPUTransferBuffer *upload = SDL_CreateGPUTransferBuffer(device, &upload_info);
    Uint32 download_bytes = row_bytes * height * depth;
    SDL_GPUTransferBufferCreateInfo download_info = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = download_bytes };
    SDL_GPUTransferBuffer *download = SDL_CreateGPUTransferBuffer(device, &download_info);
    if (!upload || !download) {
        return SDL_SetError("SDL_CreateGPUTransferBuffer: %s", SDL_GetError());
    }

    // Slice z is filled with byte z + 1 and the filler rows with 0xEE.
    Uint8 *mapped = SDL_MapGPUTransferBuffer(device, upload, false);
    if (!mapped) {
        return SDL_SetError("SDL_MapGPUTransferBuffer: %s", SDL_GetError());
    }
    SDL_memset(mapped, 0xEE, upload_bytes);
    for (Uint32 z = 0; z < depth; z++) {
        SDL_memset(mapped + z * rows_per_layer * row_bytes, (int)(z + 1), height * row_bytes);
    }
    SDL_UnmapGPUTransferBuffer(device, upload);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region = { .texture = texture, .w = width, .h = height, .d = depth };
    SDL_GPUTextureTransferInfo source = { .transfer_buffer = upload, .rows_per_layer = rows_per_layer };
    SDL_UploadToGPUTexture(copy_pass, &source, &region, false);
    SDL_GPUTextureTransferInfo destination = { .transfer_buffer = download };
    SDL_DownloadFromGPUTexture(copy_pass, &region, &destination);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence) {
        return SDL_SetError("SDL_SubmitGPUCommandBufferAndAcquireFence: %s", SDL_GetError());
    }
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);

    mapped = SDL_MapGPUTransferBuffer(device, download, false);
    if (!mapped) {
        return SDL_SetError("SDL_MapGPUTransferBuffer: %s", SDL_GetError());
    }
    SDL_memcpy(out_pixels, mapped, download_bytes);
    SDL_UnmapGPUTransferBuffer(device, download);

    SDL_ReleaseGPUTransferBuffer(device, upload);
    SDL_ReleaseGPUTransferBuffer(device, download);
    SDL_ReleaseGPUTexture(device, texture);
    return true;
}

// Records a frame like an app's render loop: uniform pushes, a render pass with color and depth targets, a submit with a fence.
static bool SubmitOffscreenFrame(SDL_GPUDevice *device, SDL_GPUTexture *color, SDL_GPUTexture *depth)
{
    static const float uniforms[16] = { 0 };

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) {
        return SDL_SetError("SDL_AcquireGPUCommandBuffer: %s", SDL_GetError());
    }
    for (int i = 0; i < 3; i++) {
        SDL_PushGPUVertexUniformData(cmd, 0, uniforms, sizeof(uniforms));
        SDL_PushGPUFragmentUniformData(cmd, 0, uniforms, sizeof(uniforms));
        SDL_PushGPUComputeUniformData(cmd, 0, uniforms, sizeof(uniforms));
    }

    SDL_GPUColorTargetInfo color_target = { .texture = color, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE };
    SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth,
        .clear_depth = 1.0f,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_DONT_CARE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
    };
    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target));

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence) {
        return SDL_SetError("SDL_SubmitGPUCommandBufferAndAcquireFence: %s", SDL_GetError());
    }
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    return true;
}

// Counts the SDL allocator calls of COUNTED_FRAMES frames that follow WARM_UP_FRAMES frames.
static bool CountFrameAllocations(SDL_GPUDevice *device, int *out_count)
{
    SDL_GPUTextureCreateInfo color_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = TEST_WIDTH,
        .height = TEST_HEIGHT,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    SDL_GPUTextureCreateInfo depth_info = color_info;
    depth_info.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    depth_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    SDL_GPUTexture *color = SDL_CreateGPUTexture(device, &color_info);
    SDL_GPUTexture *depth = SDL_CreateGPUTexture(device, &depth_info);
    if (!color || !depth) {
        return SDL_SetError("SDL_CreateGPUTexture: %s", SDL_GetError());
    }

    bool success = true;
    for (int i = 0; i < WARM_UP_FRAMES && success; i++) {
        success = SubmitOffscreenFrame(device, color, depth);
    }
    allocation_count = 0;
    counting_allocations = true;
    for (int i = 0; i < COUNTED_FRAMES && success; i++) {
        success = SubmitOffscreenFrame(device, color, depth);
    }
    counting_allocations = false;
    *out_count = allocation_count;

    SDL_ReleaseGPUTexture(device, color);
    SDL_ReleaseGPUTexture(device, depth);
    return success;
}

// Source of the copies and the blit in every presented frame. Copies need the swapchain format, so a change of composition needs a new source.
static SDL_GPUTexture *CreateFrameSource(SDL_GPUDevice *device, SDL_GPUTextureFormat format)
{
    SDL_GPUTextureCreateInfo info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = format,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = 1,
        .height = 1,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    return SDL_CreateGPUTexture(device, &info);
}

// Waiting for all of a finished fence and a pending one must not return before the pending one finishes.
static bool WaitForFinishedAndPendingFences(SDL_GPUDevice *device, bool *out_pending_finished)
{
    SDL_GPUBufferCreateInfo buffer_info = { .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, .size = 64 * 1024 * 1024 };
    SDL_GPUBuffer *source = SDL_CreateGPUBuffer(device, &buffer_info);
    SDL_GPUBuffer *destination = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!source || !destination) {
        return SDL_SetError("SDL_CreateGPUBuffer: %s", SDL_GetError());
    }

    SDL_GPUFence *fences[2];
    for (int i = 0; i < 2; i++) {
        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
        if (!cmd) {
            return SDL_SetError("SDL_AcquireGPUCommandBuffer: %s", SDL_GetError());
        }
        // Copying a large buffer many times keeps the second fence pending while the first one is polled again.
        if (i == 1) {
            SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
            for (int copy = 0; copy < 256; copy++) {
                SDL_CopyGPUBufferToBuffer(copy_pass, &(SDL_GPUBufferLocation){ .buffer = source }, &(SDL_GPUBufferLocation){ .buffer = destination }, buffer_info.size, false);
            }
            SDL_EndGPUCopyPass(copy_pass);
        }
        fences[i] = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (!fences[i]) {
            return SDL_SetError("SDL_SubmitGPUCommandBufferAndAcquireFence: %s", SDL_GetError());
        }
        if (i == 0) {
            SDL_WaitForGPUFences(device, true, fences, 1);
        }
    }

    SDL_WaitForGPUFences(device, true, fences, 2);
    *out_pending_finished = SDL_QueryGPUFence(device, fences[1]);
    SDL_WaitForGPUIdle(device);
    SDL_ReleaseGPUFence(device, fences[0]);
    SDL_ReleaseGPUFence(device, fences[1]);
    SDL_ReleaseGPUBuffer(device, source);
    SDL_ReleaseGPUBuffer(device, destination);
    return true;
}

static SDL_GPUShader *CreateShader(SDL_GPUDevice *device, const char *code, SDL_GPUShaderStage stage, Uint32 num_samplers)
{
    SDL_GPUShaderCreateInfo info = {
        .code = (const Uint8 *)code,
        .code_size = SDL_strlen(code),
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_WGSL,
        .stage = stage,
        .num_samplers = num_samplers,
    };
    return SDL_CreateGPUShader(device, &info);
}

static SDL_GPUGraphicsPipeline *CreatePipeline(SDL_GPUDevice *device, const char *vertex_code, Uint32 vertex_samplers, const char *fragment_code)
{
    SDL_GPUShader *vertex_shader = CreateShader(device, vertex_code, SDL_GPU_SHADERSTAGE_VERTEX, vertex_samplers);
    SDL_GPUShader *fragment_shader = CreateShader(device, fragment_code, SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
    if (!vertex_shader || !fragment_shader) {
        return NULL;
    }

    SDL_GPUColorTargetDescription color_target_description = { .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM };
    SDL_GPUGraphicsPipelineCreateInfo info = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info = { .color_target_descriptions = &color_target_description, .num_color_targets = 1 },
    };
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseGPUShader(device, fragment_shader);
    return pipeline;
}

// Draws once with each pipeline in one submission and binds the same texture and sampler before each draw.
static bool DrawWithPipelines(SDL_GPUDevice *device, SDL_GPUGraphicsPipeline *const *pipelines, int count, bool bind_vertex, SDL_GPUTexture *target, const SDL_GPUTextureSamplerBinding *binding)
{
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) {
        return SDL_SetError("SDL_AcquireGPUCommandBuffer: %s", SDL_GetError());
    }
    SDL_GPUColorTargetInfo color_target = { .texture = target, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE };
    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, NULL);
    for (int i = 0; i < count; i++) {
        SDL_BindGPUGraphicsPipeline(render_pass, pipelines[i]);
        if (bind_vertex) {
            SDL_BindGPUVertexSamplers(render_pass, 0, binding, 1);
        }
        SDL_BindGPUFragmentSamplers(render_pass, 0, binding, 1);
        SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
    }
    SDL_EndGPURenderPass(render_pass);
    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        return SDL_SetError("SDL_SubmitGPUCommandBuffer: %s", SDL_GetError());
    }
    SDL_WaitForGPUIdle(device);
    return true;
}

#define SAMPLING_FRAGMENT_CODE \
    "@group(2) @binding(0) var fragment_texture: texture_2d<f32>;\n" \
    "@group(2) @binding(1) var fragment_sampler: sampler;\n" \
    "@fragment fn main() -> @location(0) vec4f {\n" \
    "    return textureSample(fragment_texture, fragment_sampler, vec2f(0.5));\n" \
    "}\n"

// Bind groups for the same texture and sampler must follow the layout of each pipeline and stage.
static bool DrawWithSharedSamplers(SDL_GPUDevice *device)
{
    static const char vertex_code[] =
        "@vertex fn main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {\n"
        "    return vec4f(vec2f(f32(index & 1u), f32(index >> 1u)) * 4.0 - 1.0, 0.0, 1.0);\n"
        "}\n";
    static const char sampling_vertex_code[] =
        "@group(0) @binding(0) var vertex_texture: texture_2d<f32>;\n"
        "@group(0) @binding(1) var vertex_sampler: sampler;\n"
        "@vertex fn main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {\n"
        "    let uv = vec2f(f32(index & 1u), f32(index >> 1u));\n"
        "    return vec4f(uv * 4.0 - 1.0, textureSampleLevel(vertex_texture, vertex_sampler, uv, 0.0).r, 1.0);\n"
        "}\n";
    // The directive makes the fragment texture unfilterable, so this layout differs from the plain one only in its sample type.
    static const char unfilterable_fragment_code[] = "// SDLGPU_ForceAllowSamplingForTexture(2, 0)\n" SAMPLING_FRAGMENT_CODE;

    SDL_GPUGraphicsPipeline *fragment_pipelines[2] = {
        CreatePipeline(device, vertex_code, 0, SAMPLING_FRAGMENT_CODE),
        CreatePipeline(device, vertex_code, 0, unfilterable_fragment_code),
    };
    SDL_GPUGraphicsPipeline *both_stages_pipeline = CreatePipeline(device, sampling_vertex_code, 1, SAMPLING_FRAGMENT_CODE);
    if (!fragment_pipelines[0] || !fragment_pipelines[1] || !both_stages_pipeline) {
        return SDL_SetError("Creating the pipelines failed: %s", SDL_GetError());
    }

    SDL_GPUTextureCreateInfo texture_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = 4,
        .height = 4,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    SDL_GPUTexture *source = SDL_CreateGPUTexture(device, &texture_info);
    SDL_GPUTexture *target = SDL_CreateGPUTexture(device, &texture_info);
    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &(SDL_GPUSamplerCreateInfo){ 0 });
    if (!source || !target || !sampler) {
        return SDL_SetError("Creating the draw resources failed: %s", SDL_GetError());
    }

    // Two pipelines whose fragment layouts differ, then one pipeline whose vertex and fragment layouts differ in visibility.
    SDL_GPUTextureSamplerBinding binding = { .texture = source, .sampler = sampler };
    if (!DrawWithPipelines(device, fragment_pipelines, 2, false, target, &binding) || !DrawWithPipelines(device, &both_stages_pipeline, 1, true, target, &binding)) {
        return false;
    }

    SDL_ReleaseGPUSampler(device, sampler);
    SDL_ReleaseGPUTexture(device, target);
    SDL_ReleaseGPUTexture(device, source);
    SDL_ReleaseGPUGraphicsPipeline(device, both_stages_pipeline);
    SDL_ReleaseGPUGraphicsPipeline(device, fragment_pipelines[1]);
    SDL_ReleaseGPUGraphicsPipeline(device, fragment_pipelines[0]);
    return true;
}

static bool PixelsAreUniform(const Uint8 *pixels, Uint32 count, Uint32 bytes_per_pixel)
{
    for (Uint32 i = 1; i < count; i++) {
        if (SDL_memcmp(pixels, pixels + i * bytes_per_pixel, bytes_per_pixel) != 0) {
            return false;
        }
    }
    return true;
}

#define ORIGIN_VERTEX_CODE \
    "@vertex fn main() -> @builtin(position) vec4f {\n" \
    "    return vec4f(0.0);\n" \
    "}\n"

// Writes to the read-write storage buffer in slot 0.
static SDL_GPUComputePipeline *CreateComputePipeline(SDL_GPUDevice *device)
{
    static const char code[] =
        "@group(1) @binding(0) var<storage, read_write> values: array<u32>;\n"
        "@compute @workgroup_size(1) fn main() {\n"
        "    values[0] = 1u;\n"
        "}\n";

    SDL_GPUComputePipelineCreateInfo info = {
        .code = (const Uint8 *)code,
        .code_size = sizeof(code) - 1,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_WGSL,
        .num_readwrite_storage_buffers = 1,
        .threadcount_x = 1,
        .threadcount_y = 1,
        .threadcount_z = 1,
    };
    return SDL_CreateGPUComputePipeline(device, &info);
}

// Creates a graphics and a compute pipeline, counts the live layouts while both exist, then releases them.
static bool CreateAndReleasePipelines(SDL_GPUDevice *device, int *out_live_layouts)
{
    SDL_GPUGraphicsPipeline *graphics_pipeline = CreatePipeline(device, ORIGIN_VERTEX_CODE, 0, SAMPLING_FRAGMENT_CODE);
    SDL_GPUComputePipeline *compute_pipeline = CreateComputePipeline(device);
    if (!graphics_pipeline || !compute_pipeline) {
        return SDL_SetError("Creating the pipelines failed: %s", SDL_GetError());
    }
    *out_live_layouts = CountLiveLayouts();
    SDL_ReleaseGPUGraphicsPipeline(device, graphics_pipeline);
    SDL_ReleaseGPUComputePipeline(device, compute_pipeline);
    // Released pipelines are destroyed once no command buffer uses them, which the wait checks for.
    SDL_WaitForGPUIdle(device);
    return true;
}

// Releases two pipelines, a texture and a buffer while they are bound in a recording command buffer, runs many destroy passes,
// then uses them. Counts the live resources before the release, after the passes while recording, and after the submission completes.
static bool UseReleasedResources(SDL_GPUDevice *device, int out_live_resources[3])
{
    SDL_GPUGraphicsPipeline *graphics_pipeline = CreatePipeline(device, ORIGIN_VERTEX_CODE, 0, SAMPLING_FRAGMENT_CODE);
    SDL_GPUComputePipeline *compute_pipeline = CreateComputePipeline(device);
    SDL_GPUTextureCreateInfo texture_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = 4,
        .height = 4,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    SDL_GPUTexture *source = SDL_CreateGPUTexture(device, &texture_info);
    SDL_GPUTexture *target = SDL_CreateGPUTexture(device, &texture_info);
    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &(SDL_GPUSamplerCreateInfo){ 0 });
    SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(device, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE, .size = 4 });
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!graphics_pipeline || !compute_pipeline || !source || !target || !sampler || !buffer || !cmd) {
        return SDL_SetError("Creating the resources failed: %s", SDL_GetError());
    }
    out_live_resources[0] = CountLiveResources();

    SDL_GPUColorTargetInfo color_target = { .texture = target, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE };
    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, NULL);
    SDL_BindGPUGraphicsPipeline(render_pass, graphics_pipeline);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &(SDL_GPUTextureSamplerBinding){ .texture = source, .sampler = sampler }, 1);
    SDL_ReleaseGPUGraphicsPipeline(device, graphics_pipeline);
    SDL_ReleaseGPUTexture(device, source);
    SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(render_pass);

    SDL_GPUComputePass *compute_pass = SDL_BeginGPUComputePass(cmd, NULL, 0, &(SDL_GPUStorageBufferReadWriteBinding){ .buffer = buffer }, 1);
    SDL_BindGPUComputePipeline(compute_pass, compute_pipeline);
    SDL_ReleaseGPUComputePipeline(device, compute_pipeline);
    SDL_ReleaseGPUBuffer(device, buffer);

    // Each wait runs a destroy pass. The backend used to force a destroy after 10000 failed attempts, even while referenced.
    // The passes warn every 100 attempts, which is expected here.
    SDL_LogPriority gpu_log_priority = SDL_GetLogPriority(SDL_LOG_CATEGORY_GPU);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_ERROR);
    for (int i = 0; i <= 10000; i++) {
        SDL_WaitForGPUIdle(device);
    }
    SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, gpu_log_priority);
    out_live_resources[1] = CountLiveResources();
    if (out_live_resources[1] != out_live_resources[0]) {
        return SDL_SetError("A destroy pass freed a resource that the recording command buffer still uses");
    }

    SDL_DispatchGPUCompute(compute_pass, 1, 1, 1);
    SDL_EndGPUComputePass(compute_pass);
    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        return SDL_SetError("SDL_SubmitGPUCommandBuffer: %s", SDL_GetError());
    }
    SDL_WaitForGPUIdle(device);
    out_live_resources[2] = CountLiveResources();

    SDL_ReleaseGPUSampler(device, sampler);
    SDL_ReleaseGPUTexture(device, target);
    return true;
}

// Draws, caches bind groups and blits on a new device, then destroys it.
static bool UseAndDestroyDevice(void)
{
    SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_WGSL, true, NULL);
    if (!device) {
        return SDL_SetError("SDL_CreateGPUDevice: %s", SDL_GetError());
    }
    if (!DrawWithSharedSamplers(device)) {
        return false;
    }

    SDL_GPUTextureCreateInfo texture_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = 4,
        .height = 4,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    SDL_GPUTexture *source = SDL_CreateGPUTexture(device, &texture_info);
    SDL_GPUTexture *destination = SDL_CreateGPUTexture(device, &texture_info);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!source || !destination || !cmd) {
        return SDL_SetError("Creating the blit resources failed: %s", SDL_GetError());
    }
    SDL_GPUBlitInfo blit = {
        .source = { .texture = source, .w = 4, .h = 4 },
        .destination = { .texture = destination, .w = 4, .h = 4 },
        .filter = SDL_GPU_FILTER_NEAREST,
    };
    SDL_BlitGPUTexture(cmd, &blit);
    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        return SDL_SetError("SDL_SubmitGPUCommandBuffer: %s", SDL_GetError());
    }
    SDL_WaitForGPUIdle(device);

    SDL_ReleaseGPUTexture(device, destination);
    SDL_ReleaseGPUTexture(device, source);
    SDL_DestroyGPUDevice(device);
    return true;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    SDL_free_func original_free;
    SDL_GetMemoryFunctions(&original_malloc, &original_calloc, &original_realloc, &original_free);
    SDL_SetMemoryFunctions(CountingMalloc, CountingCalloc, CountingRealloc, original_free);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    int driver_count = SDL_GetNumGPUDrivers();
    SDL_Log("SDL_GetNumGPUDrivers() = %d", driver_count);
    for (int i = 0; i < driver_count; i++) {
        SDL_Log("GPU driver %d: %s", i, SDL_GetGPUDriver(i));
    }

    AppState *state = SDL_calloc(1, sizeof(*state));
    *appstate = state;

    // PRIVATE keeps its stock meaning (a platform-private format) and must not select this backend.
    if (SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_PRIVATE, true, NULL) != NULL) {
        SDL_Log("SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_PRIVATE) unexpectedly succeeded");
        return SDL_APP_FAILURE;
    }
    SDL_Log("SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_PRIVATE) rejected: %s", SDL_GetError());

    state->window = SDL_CreateWindow("XDL_wgpu test", 256, 256, 0);
    if (!state->window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Destroying a device must release the windows it still claims, so the window can be claimed again below.
    SDL_GPUDevice *transient_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_WGSL, true, NULL);
    if (!transient_device || !SDL_ClaimWindowForGPUDevice(transient_device, state->window)) {
        SDL_Log("Transient device setup failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_DestroyGPUDevice(transient_device);

    state->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_WGSL, true, NULL);
    if (!state->device) {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("Device shader formats: 0x%x", (unsigned)SDL_GetGPUShaderFormats(state->device));
    SDL_Log("Device driver: %s", SDL_GetGPUDeviceDriver(state->device));

    if (!SDL_ClaimWindowForGPUDevice(state->device, state->window)) {
        SDL_Log("Claiming the window after destroying its previous device failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // A window claimed by one device must not be claimable by another.
    SDL_GPUDevice *other_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_WGSL, true, NULL);
    if (!other_device) {
        SDL_Log("Second SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (SDL_ClaimWindowForGPUDevice(other_device, state->window)) {
        SDL_Log("A second device claimed an already claimed window");
        return SDL_APP_FAILURE;
    }
    SDL_Log("Second claim rejected: %s", SDL_GetError());
    SDL_DestroyGPUDevice(other_device);

    // 256-byte rows at offset 0: WebGPU writes the app layout directly.
    static Uint8 pixels[TEST_WIDTH * TEST_HEIGHT * 4];
    if (!ClearAndDownload(state->device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, TEST_WIDTH, TEST_HEIGHT, 0, pixels)) {
        SDL_Log("Clear and download failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("Readback %dx%d: %u %u %u %u (uniform: %s)", TEST_WIDTH, TEST_HEIGHT, pixels[0], pixels[1], pixels[2], pixels[3], PixelsAreUniform(pixels, TEST_WIDTH * TEST_HEIGHT, 4) ? "yes" : "no");

    // Rows narrower than 256 bytes go through the padded staging buffer and are repacked on map.
    static Uint8 narrow_pixels[NARROW_WIDTH * NARROW_HEIGHT * 4];
    if (!ClearAndDownload(state->device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, NARROW_WIDTH, NARROW_HEIGHT, 0, narrow_pixels)) {
        SDL_Log("Narrow clear and download failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    const Uint8 *last = narrow_pixels + (NARROW_WIDTH * NARROW_HEIGHT - 1) * 4;
    SDL_Log("Readback %dx%d last pixel: %u %u %u %u (uniform: %s)", NARROW_WIDTH, NARROW_HEIGHT, last[0], last[1], last[2], last[3], PixelsAreUniform(narrow_pixels, NARROW_WIDTH * NARROW_HEIGHT, 4) ? "yes" : "no");

    // 3-byte rows of a one-byte format: neither the rows nor the row offsets are 4-byte aligned.
    static Uint8 r8_pixels[3 * 2];
    if (!ClearAndDownload(state->device, SDL_GPU_TEXTUREFORMAT_R8_UNORM, 3, 2, 0, r8_pixels)) {
        SDL_Log("R8 clear and download failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("Readback R8 3x2: %u %u %u / %u %u %u", r8_pixels[0], r8_pixels[1], r8_pixels[2], r8_pixels[3], r8_pixels[4], r8_pixels[5]);

    // 256-byte rows but a destination offset that is not texel aligned.
    static Uint8 offset_pixels[2 + TEST_WIDTH * 4];
    SDL_memset(offset_pixels, 7, sizeof(offset_pixels));
    if (!ClearAndDownload(state->device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, TEST_WIDTH, 1, 2, offset_pixels)) {
        SDL_Log("Offset clear and download failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("Readback %dx1 at offset 2: %u %u %u %u (uniform: %s)", TEST_WIDTH, offset_pixels[2], offset_pixels[3], offset_pixels[4], offset_pixels[5], PixelsAreUniform(offset_pixels + 2, TEST_WIDTH, 4) ? "yes" : "no");

    // Slices one row apart more than their height: every slice after the first must skip the filler row.
    static Uint8 volume_pixels[4 * 2 * 2 * 4];
    if (!UploadAndDownload3D(state->device, 4, 2, 2, 3, volume_pixels)) {
        SDL_Log("3D upload and download failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    bool slices_match = true;
    for (Uint32 i = 0; i < sizeof(volume_pixels); i++) {
        slices_match = slices_match && volume_pixels[i] == (i < sizeof(volume_pixels) / 2 ? 1 : 2);
    }
    SDL_Log("Upload 4x2x2 with rows_per_layer 3: slice 0 = %u, slice 1 = %u (match: %s)", volume_pixels[0], volume_pixels[sizeof(volume_pixels) / 2], slices_match ? "yes" : "no");
    if (!slices_match) {
        return SDL_APP_FAILURE;
    }

    bool pending_fence_finished = false;
    if (!WaitForFinishedAndPendingFences(state->device, &pending_fence_finished)) {
        SDL_Log("Fence wait test failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("Waiting for all of a finished and a pending fence: pending fence finished: %s", pending_fence_finished ? "yes" : "no");
    if (!pending_fence_finished) {
        return SDL_APP_FAILURE;
    }

    // Pipelines keep their layouts after their shaders are released: 2 per graphics shader stage and 3 for compute.
    int layouts_before_pipelines = CountLiveLayouts();
    int layouts_with_pipelines = 0;
    if (!CreateAndReleasePipelines(state->device, &layouts_with_pipelines)) {
        SDL_Log("Creating and releasing pipelines failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    int layouts_after_pipelines = CountLiveLayouts();
    SDL_Log("Live layouts: %d before creating pipelines, %d while they exist, %d after releasing them", layouts_before_pipelines, layouts_with_pipelines, layouts_after_pipelines);
    if (layouts_with_pipelines != layouts_before_pipelines + 7 || layouts_after_pipelines != layouts_before_pipelines) {
        return SDL_APP_FAILURE;
    }

    // A released resource stays alive while a command buffer that bound it records or runs.
    int live_resources[3] = { 0 };
    if (!UseReleasedResources(state->device, live_resources)) {
        SDL_Log("Using released resources failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("Live resources: %d before releasing 4 bound ones, %d while recording, %d after the submission", live_resources[0], live_resources[1], live_resources[2]);
    if (live_resources[2] != live_resources[0] - 4) {
        return SDL_APP_FAILURE;
    }

    // Destroying a device must release every layout it created, including those of its blit pipelines and cached bind groups.
    int layouts_before_device = CountLiveLayouts();
    if (!UseAndDestroyDevice()) {
        SDL_Log("Using and destroying a device failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    int layouts_after_device = CountLiveLayouts();
    SDL_Log("Live layouts: %d before creating a device, %d after destroying it", layouts_before_device, layouts_after_device);
    if (layouts_after_device != layouts_before_device) {
        return SDL_APP_FAILURE;
    }

    if (!DrawWithSharedSamplers(state->device)) {
        SDL_Log("Drawing with shared samplers failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("Drew with the same texture and sampler under different bind group layouts");

    // The backend reuses command buffers, fences and uniform staging memory, so a frame after warm-up allocates nothing.
    int frame_allocations = 0;
    if (!CountFrameAllocations(state->device, &frame_allocations)) {
        SDL_Log("Frame allocation test failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("SDL allocations in %d frames after %d warm-up frames: %d", COUNTED_FRAMES, WARM_UP_FRAMES, frame_allocations);
    if (frame_allocations != 0) {
        return SDL_APP_FAILURE;
    }

    state->frame_source = CreateFrameSource(state->device, SDL_GetGPUSwapchainTextureFormat(state->device, state->window));
    if (!state->frame_source) {
        SDL_Log("SDL_CreateGPUTexture for the frame source failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_WaitForGPUIdle(state->device);
    state->futures_after_init = CountTrackedFutures();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    AppState *state = appstate;

    // Halfway through warm-up, switch to SDR_LINEAR. A canvas has no sRGB format, so the backend renders through an sRGB view.
    // Asking whether a composition is supported must not change the current swapchain format.
    if (state->frames_submitted == WARM_UP_FRAMES / 2) {
        SDL_GPUTextureFormat sdr_format = SDL_GetGPUSwapchainTextureFormat(state->device, state->window);
        bool supported = SDL_WindowSupportsGPUSwapchainComposition(state->device, state->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR);
        if (!supported || SDL_GetGPUSwapchainTextureFormat(state->device, state->window) != sdr_format) {
            SDL_Log("SDR_LINEAR query failed: supported %s, format %d before and %d after", supported ? "yes" : "no", sdr_format, SDL_GetGPUSwapchainTextureFormat(state->device, state->window));
            return SDL_APP_FAILURE;
        }
        if (!SDL_SetGPUSwapchainParameters(state->device, state->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR, SDL_GPU_PRESENTMODE_VSYNC)) {
            SDL_Log("SDL_SetGPUSwapchainParameters(SDR_LINEAR) failed: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
        SDL_GPUTextureFormat linear_format = SDL_GetGPUSwapchainTextureFormat(state->device, state->window);
        SDL_Log("SDR_LINEAR swapchain format: %d (sRGB: %s)", linear_format, linear_format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB || linear_format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB ? "yes" : "no");
        if (linear_format != SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB && linear_format != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB) {
            return SDL_APP_FAILURE;
        }
        SDL_ReleaseGPUTexture(state->device, state->frame_source);
        state->frame_source = CreateFrameSource(state->device, linear_format);
        if (!state->frame_source) {
            SDL_Log("SDL_CreateGPUTexture for the SDR_LINEAR frame source failed: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
    }

    allocation_count = 0;
    counting_allocations = state->frames_submitted >= WARM_UP_FRAMES;
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(state->device);
    SDL_GPUTexture *swapchain_texture = NULL;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, state->window, &swapchain_texture, NULL, NULL)) {
        SDL_Log("SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (swapchain_texture) {
        SDL_GPUColorTargetInfo color_target = {
            .texture = swapchain_texture,
            .clear_color = { 0.1f, 0.2f, 0.3f, 1.0f },
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
        };
        SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, &color_target, 1, NULL));

        // A swapchain texture cannot be cycled. The second copy finds it in use by the first, so if it were cycled,
        // the frame would go to a new offscreen texture and WebGPU would report a destroyed texture used in a submit.
        SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureLocation source = { .texture = state->frame_source };
        SDL_GPUTextureLocation destination = { .texture = swapchain_texture };
        SDL_CopyGPUTextureToTexture(copy_pass, &source, &destination, 1, 1, 1, true);
        SDL_CopyGPUTextureToTexture(copy_pass, &source, &destination, 1, 1, 1, true);
        SDL_EndGPUCopyPass(copy_pass);

        // A blit binds a pipeline, a texture and a sampler, so it covers the bind group cache.
        SDL_GPUBlitInfo blit = {
            .source = { .texture = state->frame_source, .w = 1, .h = 1 },
            .destination = { .texture = swapchain_texture, .w = 16, .h = 16 },
            .load_op = SDL_GPU_LOADOP_LOAD,
            .filter = SDL_GPU_FILTER_NEAREST,
        };
        SDL_BlitGPUTexture(cmd, &blit);
    }
    bool submitted = SDL_SubmitGPUCommandBuffer(cmd);
    counting_allocations = false;
    state->presented_frame_allocations += allocation_count;
    if (!submitted) {
        SDL_Log("SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->frames_submitted++;
    SDL_Log("Frame %d submitted", state->frames_submitted);
    if (state->frames_submitted < FRAMES_TO_SUBMIT) {
        return SDL_APP_CONTINUE;
    }

    SDL_WaitForGPUIdle(state->device);
    int futures_after_frames = CountTrackedFutures();
    SDL_Log("Tracked futures: %d after init, %d after %d frames", state->futures_after_init, futures_after_frames, FRAMES_TO_SUBMIT);
    SDL_Log("SDL allocations in %d presented frames after %d warm-up frames: %d", COUNTED_FRAMES, WARM_UP_FRAMES, state->presented_frame_allocations);
    return futures_after_frames <= state->futures_after_init && state->presented_frame_allocations == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    return event->type == SDL_EVENT_QUIT ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    AppState *state = appstate;
    if (state) {
        if (state->device) {
            SDL_WaitForGPUIdle(state->device);
            SDL_ReleaseGPUTexture(state->device, state->frame_source);
            if (state->window) {
                SDL_ReleaseWindowFromGPUDevice(state->device, state->window);
            }
            SDL_DestroyGPUDevice(state->device);
        }
        SDL_DestroyWindow(state->window);
        SDL_free(state);
    }
    SDL_Log("XDL_wgpu test %s", result == SDL_APP_SUCCESS ? "PASSED" : "FAILED");
}
