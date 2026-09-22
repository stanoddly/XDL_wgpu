// Smoke test for XDL_wgpu: uses only public SDL3 headers and must be linked with libXDL_wgpu.a before libSDL3.a.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <XDL_wgpu.h>

#define TEST_WIDTH 64
#define TEST_HEIGHT 4
#define NARROW_WIDTH 5
#define NARROW_HEIGHT 3
#define FRAMES_TO_SUBMIT 3

typedef struct AppState
{
    SDL_Window *window;
    SDL_GPUDevice *device;
    int frames_submitted;
} AppState;

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

static bool PixelsAreUniform(const Uint8 *pixels, Uint32 count, Uint32 bytes_per_pixel)
{
    for (Uint32 i = 1; i < count; i++) {
        if (SDL_memcmp(pixels, pixels + i * bytes_per_pixel, bytes_per_pixel) != 0) {
            return false;
        }
    }
    return true;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
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

    state->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_WGSL, true, NULL);
    if (!state->device) {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("Device shader formats: 0x%x", (unsigned)SDL_GetGPUShaderFormats(state->device));
    SDL_Log("Device driver: %s", SDL_GetGPUDeviceDriver(state->device));

    state->window = SDL_CreateWindow("XDL_wgpu test", 256, 256, 0);
    if (!state->window || !SDL_ClaimWindowForGPUDevice(state->device, state->window)) {
        SDL_Log("Window setup failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

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

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    AppState *state = appstate;

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
    }
    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        SDL_Log("SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->frames_submitted++;
    SDL_Log("Frame %d submitted", state->frames_submitted);
    return state->frames_submitted < FRAMES_TO_SUBMIT ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
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
