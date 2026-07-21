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
set(_xdg_shell_xml ${VCE_WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml)
set(_gen_dir ${CMAKE_CURRENT_BINARY_DIR}/protocols)

add_custom_command(
    OUTPUT ${_gen_dir}/xdg-shell-client-protocol.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_gen_dir}
    COMMAND ${VCE_WAYLAND_SCANNER} client-header ${_xdg_shell_xml}
            ${_gen_dir}/xdg-shell-client-protocol.h
    DEPENDS ${_xdg_shell_xml}
    COMMENT "wayland-scanner: xdg-shell client header")

add_custom_command(
    OUTPUT ${_gen_dir}/xdg-shell-protocol.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_gen_dir}
    COMMAND ${VCE_WAYLAND_SCANNER} private-code ${_xdg_shell_xml}
            ${_gen_dir}/xdg-shell-protocol.c
    DEPENDS ${_xdg_shell_xml}
    COMMENT "wayland-scanner: xdg-shell marshalling stubs")

add_library(vk_canvas_wayland STATIC
    wayland_platform.cc
    wayland_display.cc
    wayland_window.cc
    ${_gen_dir}/xdg-shell-protocol.c
)
# The generated .c includes the generated header.
set_source_files_properties(${_gen_dir}/xdg-shell-protocol.c PROPERTIES
    OBJECT_DEPENDS ${_gen_dir}/xdg-shell-client-protocol.h)
target_sources(vk_canvas_wayland PRIVATE
    ${_gen_dir}/xdg-shell-client-protocol.h)

target_include_directories(vk_canvas_wayland
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${_gen_dir})

# PUBLIC: the headers consumers include (<wayland-client.h> via
# wayland_display.hh) and the link libs both propagate.
target_link_libraries(vk_canvas_wayland
    PUBLIC vk_canvas_core PkgConfig::VCE_WAYLAND)
