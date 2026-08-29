# Compares composite_frag.slang's push-constant block against its C++ mirror,
# CompositePush in core/renderer.cc, and fails the build when they disagree.
#
# This exists because they disagreed once and nothing noticed. Two branches
# each appended fields to the same block; when they were reconciled the C++
# side kept one order and the shader took another. The result was a video
# player that rotated every frame the wrong way, never ran its PQ decode, and
# ran the camera's focus-peaking filter across the picture — with no error, no
# warning and no validation-layer message, because a push block is a memcpy
# into a struct nothing inspects. The mismatch is only observable by looking at
# the picture and knowing what it should have been.
#
# Run as:
#   cmake -DSHADER=<composite_frag.slang> -DCPP=<renderer.cc>
#         -P check_composite_pc.cmake

if(NOT EXISTS "${SHADER}")
    message(FATAL_ERROR "check_composite_pc: no shader at ${SHADER}")
endif()
if(NOT EXISTS "${CPP}")
    message(FATAL_ERROR "check_composite_pc: no C++ mirror at ${CPP}")
endif()

# Flatten a declaration into scalars, so the shader's `float2 uvScale` and the
# C++ side's `float uvScaleX; float uvScaleY;` compare equal. That is the one
# legitimate difference in spelling between the two: std430 gives a float2 an
# 8-byte alignment, which a pair of floats at the same offsets already meets.
function(_flatten_decl type name out_var)
    set(_result "")
    if(type STREQUAL "float2")
        list(APPEND _result "float ${name}X" "float ${name}Y")
    elseif(type STREQUAL "float3")
        list(APPEND _result "float ${name}X" "float ${name}Y" "float ${name}Z")
    elseif(type STREQUAL "float4")
        list(APPEND _result "float ${name}X" "float ${name}Y" "float ${name}Z" "float ${name}W")
    else()
        list(APPEND _result "${type} ${name}")
    endif()
    set(${out_var} "${_result}" PARENT_SCOPE)
endfunction()

# Collect scalar fields from the lines between `begin_re` and `end_re`.
function(_collect_fields path begin_re end_re out_var)
    file(STRINGS "${path}" _lines)
    set(_in FALSE)
    set(_fields "")
    foreach(_line IN LISTS _lines)
        if(NOT _in)
            if(_line MATCHES "${begin_re}")
                set(_in TRUE)
            endif()
            continue()
        endif()
        if(_line MATCHES "${end_re}")
            break()
        endif()
        # Trailing comments must not be scanned for declarations.
        string(REGEX REPLACE "//.*$" "" _line "${_line}")
        if(_line MATCHES "^[ \t]*(float4|float3|float2|float|int)[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*;")
            _flatten_decl("${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}" _scalars)
            list(APPEND _fields ${_scalars})
        endif()
    endforeach()
    if(NOT _in)
        message(FATAL_ERROR
            "check_composite_pc: never found the opening marker /${begin_re}/ in ${path}")
    endif()
    set(${out_var} "${_fields}" PARENT_SCOPE)
endfunction()

_collect_fields("${SHADER}" "cbuffer[ \t]+PC[ \t]*{" "^[ \t]*}[ \t]*pc[ \t]*;" _shader_fields)
_collect_fields("${CPP}"    "COMPOSITE_PC_BEGIN"      "COMPOSITE_PC_END"        _cpp_fields)

list(LENGTH _shader_fields _n_shader)
list(LENGTH _cpp_fields    _n_cpp)
if(_n_shader EQUAL 0)
    message(FATAL_ERROR "check_composite_pc: parsed no fields out of ${SHADER}")
endif()

if(NOT _shader_fields STREQUAL _cpp_fields)
    string(REPLACE ";" "\n    " _s "${_shader_fields}")
    string(REPLACE ";" "\n    " _c "${_cpp_fields}")
    message(FATAL_ERROR
        "The composite push block has drifted apart.\n\n"
        "  ${SHADER}\n    ${_s}\n\n"
        "  ${CPP} (CompositePush)\n    ${_c}\n\n"
        "These are written to the same bytes by vkCmdPushConstants and nothing "
        "checks them at runtime, so a mismatch silently feeds each field's value "
        "to whichever field the shader happens to read at that offset. Fix both "
        "in the same commit; append new fields at the end and never reorder.")
endif()

math(EXPR _bytes "${_n_shader} * 4")
message(STATUS "composite push block: ${_n_shader} scalars, ${_bytes} bytes, shader and C++ agree")
