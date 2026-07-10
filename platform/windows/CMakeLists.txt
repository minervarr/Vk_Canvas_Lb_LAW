cmake_minimum_required(VERSION 3.22.1)
project(vk_canvas_windows CXX C)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_subdirectory(${REPO_ROOT}/core ${CMAKE_BINARY_DIR}/core)

# Compile shaders next to the executable: FileAssetReader reads <exe>/assets/.
# composite/tiling/coverage are canonically owned by the font engine (same
# bytes vk_canvas's own copies used to hand-duplicate — see its shaders_src
# comment); overlay/image are genuinely vk_canvas's own.
include(${REPO_ROOT}/cmake/VceShaders.cmake)
vce_compile_slang(compile_shared_shaders
    ${CMAKE_BINARY_DIR}/assets/shaders
    ${REPO_ROOT}/first_party/vulkan_font_engine/shaders_src
    composite_vert
    composite_frag
    tiling
    coverage
)
vce_compile_slang(compile_shaders
    ${CMAKE_BINARY_DIR}/assets/shaders
    ${REPO_ROOT}/shaders_src
    overlay_vert
    overlay_frag
    image_vert
    image_frag
    shape_vert
    shape_frag
)

add_executable(vk_canvas
    main.cc
    win32_platform.cc
)

add_dependencies(vk_canvas compile_shared_shaders compile_shaders)

# FileAssetReader reads <exe dir>/assets/, and the shaders above compile into
# ${CMAKE_BINARY_DIR}/assets — so the exe must sit at the build root.
set_target_properties(vk_canvas PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

target_include_directories(vk_canvas PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(vk_canvas PRIVATE /utf-8)
endif()

target_link_libraries(vk_canvas
    vk_canvas_core
    user32
)
