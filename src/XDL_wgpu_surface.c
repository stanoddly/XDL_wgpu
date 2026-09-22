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
