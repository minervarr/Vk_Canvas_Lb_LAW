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
# to the Windows Vulkan SDK install path this project is developed against; a
# host with the SDK elsewhere overrides it with -DVCE_SLANGC=/path/to/slangc.

set(VCE_SLANGC "C:/VulkanSDK/1.4.341.1/Bin/slangc.exe"
    CACHE FILEPATH "Path to the Slang compiler (slangc) from the Vulkan SDK")

function(vce_compile_slang TARGET_NAME OUT_DIR SHADER_SRC_DIR)
    file(MAKE_DIRECTORY ${OUT_DIR})
    set(_spv_outputs "")
    foreach(SHADER ${ARGN})
        add_custom_command(
            OUTPUT  ${OUT_DIR}/${SHADER}.spv
            COMMAND ${VCE_SLANGC} ${SHADER_SRC_DIR}/${SHADER}.slang
                              -o ${OUT_DIR}/${SHADER}.spv
                              -target spirv
            DEPENDS ${SHADER_SRC_DIR}/${SHADER}.slang
            COMMENT "Compiling ${SHADER}.slang"
        )
        list(APPEND _spv_outputs ${OUT_DIR}/${SHADER}.spv)
    endforeach()
    add_custom_target(${TARGET_NAME} ALL DEPENDS ${_spv_outputs})
endfunction()
