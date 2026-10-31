# Shared compile/link policy for the vk_canvas module targets.
#
# "Pay for what you use" is a link-time property. The module archive is already
# split per subsystem (one .cc = one object); function/data sections and
# --gc-sections additionally drop unused functions *within* an object, which
# matters for Canvas, the overlay rasterizer and the per-widget toolkit TUs.
#
# The option exists so tools/size_report.sh can build the same probes with and
# without section GC and show the difference instead of asserting it. It is on
# by default; Debug builds stay section-free because their compile time and
# debug fidelity matter more than a few KB.

option(VCE_CANVAS_FUNCTION_SECTIONS
    "Compile non-Debug modules with function/data sections and link with section GC" ON)

function(vce_canvas_module_settings target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
        if(VCE_CANVAS_FUNCTION_SECTIONS)
            # /OPT:REF is a linker default in Release, but stating it keeps the
            # policy visible next to the /Gy that makes it effective.
            target_compile_options(${target} PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/Gy>")
            target_link_options(${target} INTERFACE "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>")
        endif()
    else()
        target_compile_options(${target} PRIVATE -O2)
        if(VCE_CANVAS_FUNCTION_SECTIONS)
            target_compile_options(${target} PRIVATE
                "$<$<NOT:$<CONFIG:Debug>>:-ffunction-sections>"
                "$<$<NOT:$<CONFIG:Debug>>:-fdata-sections>")
            if(APPLE)
                target_link_options(${target} INTERFACE
                    "$<$<NOT:$<CONFIG:Debug>>:-Wl,-dead_strip>")
            else()
                target_link_options(${target} INTERFACE
                    "$<$<NOT:$<CONFIG:Debug>>:-Wl,--gc-sections>")
            endif()
        endif()
    endif()
endfunction()
