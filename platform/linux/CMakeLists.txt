# platform/linux — raw-Wayland desktop backend, built as the static library
# `vk_canvas_wayland`. Unlike the Win32 backend (a single .cc consumers
# compile directly), this is a library because the xdg-shell protocol glue is
# GENERATED at build time by wayland-scanner from the system wayland-protocols
# XML — still the raw protocol, just its C marshalling stubs.
#
# Consumers: add_subdirectory this AFTER vk_canvas core (the lib links
# vk_canvas_core) and link `vk_canvas_wayland`. Dependencies are the Wayland
# equivalents of user32.dll — no toolkits:
#   wayland-client   wire-protocol marshalling
#   wayland-cursor   cursor-theme loader (part of the wayland package)
#   xkbcommon        keymap interpretation (what TranslateMessage did on Win32)

find_package(PkgConfig REQUIRED)
pkg_check_modules(VCE_WAYLAND REQUIRED IMPORTED_TARGET
    wayland-client wayland-cursor xkbcommon)

find_program(VCE_WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(VCE_WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
set(_gen_dir ${CMAKE_CURRENT_BINARY_DIR}/protocols)

# Every protocol we bind is generated the same way: a client header + the
# marshalling stubs, both from the system XML. Adding one is a single call.
#   xdg-shell     — the window itself (stable)
#   idle-inhibit  — "don't blank the screen while this surface is up", for
#                   fullscreen art (unstable v1; the only version that exists)
function(vce_wayland_protocol name xml)
    add_custom_command(
        OUTPUT ${_gen_dir}/${name}-client-protocol.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_gen_dir}
        COMMAND ${VCE_WAYLAND_SCANNER} client-header ${xml}
                ${_gen_dir}/${name}-client-protocol.h
        DEPENDS ${xml}
        COMMENT "wayland-scanner: ${name} client header")
    add_custom_command(
        OUTPUT ${_gen_dir}/${name}-protocol.c
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_gen_dir}
        COMMAND ${VCE_WAYLAND_SCANNER} private-code ${xml}
                ${_gen_dir}/${name}-protocol.c
        DEPENDS ${xml}
        COMMENT "wayland-scanner: ${name} marshalling stubs")
    # The generated .c includes the generated header.
    set_source_files_properties(${_gen_dir}/${name}-protocol.c PROPERTIES
        OBJECT_DEPENDS ${_gen_dir}/${name}-client-protocol.h)
endfunction()

vce_wayland_protocol(xdg-shell
    ${VCE_WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml)
vce_wayland_protocol(idle-inhibit-unstable-v1
    ${VCE_WAYLAND_PROTOCOLS}/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml)

add_library(vk_canvas_wayland STATIC
    wayland_platform.cc
    wayland_display.cc
    wayland_window.cc
    ${_gen_dir}/xdg-shell-protocol.c
    ${_gen_dir}/idle-inhibit-unstable-v1-protocol.c
)
target_sources(vk_canvas_wayland PRIVATE
    ${_gen_dir}/xdg-shell-client-protocol.h
    ${_gen_dir}/idle-inhibit-unstable-v1-client-protocol.h)

target_include_directories(vk_canvas_wayland
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${_gen_dir})

# PUBLIC: the headers consumers include (<wayland-client.h> via
# wayland_display.hh) and the link libs both propagate.
target_link_libraries(vk_canvas_wayland
    PUBLIC vk_canvas_core PkgConfig::VCE_WAYLAND)
