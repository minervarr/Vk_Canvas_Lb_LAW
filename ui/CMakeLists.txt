# ui/ — the toolkit layer above the engine.
#
# It draws through Canvas and reads FrameInput; it owns no window, device or
# swapchain, and it must never include Vulkan or the gpu/ headers. `vk_canvas_ui`
# is a sibling target to the engine modules; the compatibility umbrella
# `vk_canvas_core` links it for one transition cycle so existing consumers keep
# their include paths.

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/VceModules.cmake)

add_library(vk_canvas_ui STATIC
    state.cc
    context.cc
    widgets_text_fit.cc
    widgets_text_field.cc
    widgets_toggle.cc
    widgets_radio.cc
    widgets_stepper.cc
    widgets_slider.cc
    widgets_segmented.cc
    widgets_dropdown.cc
    widgets_scroll_list.cc
    widgets_table.cc
    widgets_button.cc
    widgets_group_header.cc
    ctx_label.cc
    ctx_toggle.cc
    ctx_radio.cc
    ctx_slider.cc
    ctx_button.cc
    ctx_text_field.cc
    ctx_scroll_list.cc
    textarea.cc
    plotview.cc
    pager.cc
    pager_draw.cc
)

target_include_directories(vk_canvas_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/theme
)

vce_canvas_module_settings(vk_canvas_ui)

# The demo is a consumer of the toolkit, not part of it. It builds for the
# tests and on explicit request; a normal app target does not compile it.
option(VCE_BUILD_UI_DEMO "Build the vk_canvas_ui_demo settings-screen example" OFF)
if(BUILD_TESTING OR VCE_BUILD_UI_DEMO)
    add_library(vk_canvas_ui_demo STATIC demo/settings_screen.cc)
    target_link_libraries(vk_canvas_ui_demo PUBLIC vk_canvas_ui)
    vce_canvas_module_settings(vk_canvas_ui_demo)
endif()

# draw PUBLIC-exposes Canvas and the base modules. This target's SOURCE and
# headers are Vulkan-free (enforced by tools/check_deps.py); the link line still
# carries libvulkan because the font-engine submodule builds its GPU units into
# one vk_font_core archive and publicly links the loader. Splitting that target
# is outside this repository; Phase 2's rule is about what ui/ may include and
# call, not about a transitive loader in the font engine.
target_link_libraries(vk_canvas_ui PUBLIC
    vk_canvas_draw
    vk_canvas_base
    vk_font_core
)

# CPU-only widget tests. They link the UI target (which links draw+font) but no
# renderer object; the tests exercise geometry and state transitions, not
# pixels. The loader appears on the link line only through vk_font_core.
if(NOT ANDROID AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    function(vce_add_ui_test name)
        add_executable(${name} tests/${name}.cc)
        target_link_libraries(${name} PRIVATE vk_canvas_ui)
        target_compile_features(${name} PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(${name} PRIVATE /W3 /utf-8)
        else()
            target_compile_options(${name} PRIVATE -Wall -Wextra)
        endif()
        if(BUILD_TESTING)
            add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
            set_tests_properties(${name} PROPERTIES LABELS unit)
        endif()
    endfunction()

    vce_add_ui_test(pager_test)
    vce_add_ui_test(widget_geometry_test)
    vce_add_ui_test(ui_state_test)
    vce_add_ui_test(theme_test)
    vce_add_ui_test(context_widgets_test)
    # The demo target is only built by CTest or on explicit request. A
    # consumer's ordinary Debug build still compiles this test block (the
    # condition above is CMAKE_BUILD_TYPE, not BUILD_TESTING), so linking a
    # target that may not exist breaks the whole build. Guard the one test that
    # needs the demo; pager/widgets/state/theme do not.
    if(TARGET vk_canvas_ui_demo)
        vce_add_ui_test(context_demo_test)
        target_link_libraries(context_demo_test PRIVATE vk_canvas_ui_demo)
    endif()
    # Counting operator new: proves the steady-state frame build does not
    # allocate after warm-up. See the test's header comment.
    vce_add_ui_test(frame_alloc_test)
endif()
