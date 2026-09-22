// Creates the WGPUSurface for an SDL window on Emscripten from the window's canvas selector.
#include "SDL_internal.h"

#include <webgpu/webgpu.h>

// Replaces the SDL_wgpu fork's SDL_WGPU_CreateSurface video-backend hook for the Emscripten target.
WGPUSurface XDL_WGPU_CreateSurface(SDL_Window *window, WGPUInstance instance)
{
    const char *canvas = SDL_GetStringProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, NULL);
    if (!canvas) {
        SDL_SetError("Window has no Emscripten canvas selector");
        return NULL;
    }

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    source.selector.data = canvas;
    source.selector.length = WGPU_STRLEN;

    WGPUSurfaceDescriptor desc = WGPU_SURFACE_DESCRIPTOR_INIT;
    desc.nextInChain = &source.chain;

    return wgpuInstanceCreateSurface(instance, &desc);
}
