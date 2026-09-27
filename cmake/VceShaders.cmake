# VceShaders.cmake — reusable Slang → SPIR-V compilation for vulkan_canvas_engine.
#
# A consumer (the library itself, or an app embedding it) gets the canvas/font
# engine's shaders compiled without re-deriving the slangc invocation.
#
#   vce_compile_slang(<target_name> <out_dir> <shader_src_dir> <name> [<name> ...])
#
# compiles each <name>.slang in <shader_src_dir> to <out_dir>/<name>.spv with
# slangc, and groups the results under an ALL custom target named <target_name>
# (add_dependencies(<your_lib> <target_name>) to force them to build first).
#
# The slangc binary is resolved from the VCE_SLANGC cache variable. It defaults
# to $ENV{VULKAN_SDK}, falling back to the Windows Vulkan SDK install path this
# project is developed against; override with -DVCE_SLANGC=/path/to/slangc.

if(DEFINED ENV{VULKAN_SDK})
    if(CMAKE_HOST_WIN32)
        set(_vce_slangc_default "$ENV{VULKAN_SDK}/Bin/slangc.exe")
    else()
        set(_vce_slangc_default "$ENV{VULKAN_SDK}/bin/slangc")
    endif()
else()
    set(_vce_slangc_default "C:/VulkanSDK/1.4.341.1/Bin/slangc.exe")
endif()
set(VCE_SLANGC "${_vce_slangc_default}"
    CACHE FILEPATH "Path to the Slang compiler (slangc) from the Vulkan SDK")

# vk_canvas's own shaders_src, resolved HERE rather than inside the function
# below. Inside a function CMAKE_CURRENT_LIST_DIR is the CALLER's directory, not
# this file's — which silently produced <consumer>/shaders_src and an include
# that could not be found.
get_filename_component(VCE_SHARED_SHADER_INCLUDE
                       "${CMAKE_CURRENT_LIST_DIR}/../shaders_src" ABSOLUTE)

function(vce_compile_slang TARGET_NAME OUT_DIR SHADER_SRC_DIR)
    file(MAKE_DIRECTORY ${OUT_DIR})
    # That directory is always on the include path, whichever shader directory
    # is being compiled: output_encode.slang lives there and is included by
    # composite_frag.slang, which lives in the FONT ENGINE's shaders_src — a
    # different directory, and slangc resolves #include relative to the
    # including file only.
    set(_vce_shared_include ${VCE_SHARED_SHADER_INCLUDE})
    # Depend on every .slang in the directory, not just the entry point being
    # compiled: shaders that #include a shared source (e.g. an fp16 variant that
    # wraps the fp32 one) would otherwise go stale whenever the included file
    # changed, silently shipping a mismatched .spv.
    file(GLOB _slang_sources ${SHADER_SRC_DIR}/*.slang)
    file(GLOB _vce_shared_sources ${_vce_shared_include}/*.slang)
    list(APPEND _slang_sources ${_vce_shared_sources})
    set(_spv_outputs "")
    foreach(SHADER ${ARGN})
        add_custom_command(
            OUTPUT  ${OUT_DIR}/${SHADER}.spv
            COMMAND ${VCE_SLANGC} ${SHADER_SRC_DIR}/${SHADER}.slang
                              -o ${OUT_DIR}/${SHADER}.spv
                              -target spirv
                              -I ${SHADER_SRC_DIR}
                              -I ${_vce_shared_include}
            DEPENDS ${SHADER_SRC_DIR}/${SHADER}.slang ${_slang_sources}
            COMMENT "Compiling ${SHADER}.slang"
        )
        list(APPEND _spv_outputs ${OUT_DIR}/${SHADER}.spv)
    endforeach()
    add_custom_target(${TARGET_NAME} ALL DEPENDS ${_spv_outputs})
endfunction()
