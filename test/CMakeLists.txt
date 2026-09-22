add_executable(XDL_wgpu_test test.c)
set_target_properties(XDL_wgpu_test PROPERTIES OUTPUT_NAME test SUFFIX .html)
# Link order is the whole mechanism: XDL_wgpu before SDL3 so its SDL_GPU* definitions win and libSDL3.a(SDL_gpu.c.o) is never extracted.
target_link_libraries(XDL_wgpu_test PRIVATE XDL_wgpu SDL3::SDL3-static)
target_link_options(XDL_wgpu_test PRIVATE
    -sASYNCIFY -sALLOW_MEMORY_GROWTH --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html
    -Wl,--trace-symbol=SDL_CreateGPUDevice -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/test.map)
set_property(TARGET XDL_wgpu_test APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
