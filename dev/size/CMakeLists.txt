# Feature-isolated link probes for tools/size_report.sh.
#
# Each executable references exactly one layer or feature, so its size is the
# linker's answer to "what does an app pay when it uses only this?". They are
# never executed: main() only dereferences what it is asked to keep, which is
# all the linker needs. The script builds this directory with section GC on and
# off and reports both.

function(vce_add_size_probe name)
    add_executable(${name} ${name}.cc)
    target_compile_features(${name} PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(${name} PRIVATE -O2)
    endif()
endfunction()

# Layout math only: one base-layer object.
vce_add_size_probe(size_layout)
target_link_libraries(size_layout PRIVATE vk_canvas_base)

# Canvas primitive path: draw + base (plus the font engine's transitive link
# line, which is part of what a Canvas consumer pays today).
vce_add_size_probe(size_canvas_rect)
target_link_libraries(size_canvas_rect PRIVATE vk_canvas_draw)

# One widget from the toolkit, and every widget. The delta is the value of the
# per-widget translation units.
vce_add_size_probe(size_ui_geometry)
target_link_libraries(size_ui_geometry PRIVATE vk_canvas_ui)
vce_add_size_probe(size_ui_toggle)
target_link_libraries(size_ui_toggle PRIVATE vk_canvas_ui)
vce_add_size_probe(size_ui_all_widgets)
target_link_libraries(size_ui_all_widgets PRIVATE vk_canvas_ui)

# Renderer entry point: pulls the GPU archive, the font engine and the loader
# (the loader itself is a shared dependency, not part of the binary).
vce_add_size_probe(size_gpu_renderer)
target_link_libraries(size_gpu_renderer PRIVATE vk_canvas_gpu)
