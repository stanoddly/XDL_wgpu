cmake_minimum_required(VERSION 3.24)
project(XDL_wgpu VERSION 0.1.0 LANGUAGES C)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "XDL_wgpu targets wasm32-emscripten only; configure with emcmake")
endif()

set(XDL_SDL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL" CACHE PATH "SDL source tree (release-3.4.x) to build against")
set(XDL_EMSCRIPTEN_FLAGS "" CACHE STRING "Extra emcc flags for compiling the library (for example -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0); the test keeps its own asyncify flags")
option(XDL_BUILD_TEST "Build test/test.html" ON)

if(NOT EXISTS "${XDL_SDL_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "No SDL source at ${XDL_SDL_SOURCE_DIR}; run 'git submodule update --init' or set XDL_SDL_SOURCE_DIR")
endif()

# Stock SDL: no GPU backend, no dynamic API on Emscripten, so its SDL_gpu.c.o only ever links when nothing else defines SDL_GPU*.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${XDL_SDL_SOURCE_DIR}" external/SDL EXCLUDE_FROM_ALL)

separate_arguments(xdl_emscripten_flags NATIVE_COMMAND "${XDL_EMSCRIPTEN_FLAGS}")
set(xdl_port_flags --use-port=emdawnwebgpu)

add_library(XDL_wgpu STATIC src/SDL_gpu.c src/SDL_gpu_webgpu.c src/SDL_wgpu_surface.c)
set_target_properties(XDL_wgpu PROPERTIES OUTPUT_NAME XDL_wgpu PREFIX lib)

# Compile exactly like an SDL-internal source file: same generated SDL_build_config.h, same private include path and defines.
get_target_property(sdl_include_dirs SDL3-static INCLUDE_DIRECTORIES)
get_target_property(sdl_compile_defs SDL3-static COMPILE_DEFINITIONS)
get_target_property(sdl_compile_opts SDL3-static COMPILE_OPTIONS)
target_include_directories(XDL_wgpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${sdl_include_dirs} "${XDL_SDL_SOURCE_DIR}/src/gpu")
target_compile_definitions(XDL_wgpu PRIVATE ${sdl_compile_defs} SDL_GPU_WEBGPU=1)
target_compile_options(XDL_wgpu PRIVATE ${sdl_compile_opts} ${xdl_port_flags} ${xdl_emscripten_flags})
target_link_libraries(XDL_wgpu PUBLIC SDL3::Headers)
target_link_options(XDL_wgpu INTERFACE ${xdl_port_flags})

if(XDL_BUILD_TEST)
    add_subdirectory(test)
endif()
