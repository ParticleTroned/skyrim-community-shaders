include_guard(GLOBAL)

function(append_copy_if_different _cmds _srcs _src_base _dst_base)
    foreach(_src IN LISTS ${_srcs})
        file(RELATIVE_PATH _rel "${_src_base}" "${_src}")
        set(_dst "${_dst_base}/${_rel}")
        get_filename_component(_dst_dir "${_dst}" DIRECTORY)
        list(
            APPEND ${_cmds}
            COMMAND
            ${CMAKE_COMMAND}
            -E
            make_directory
            "${_dst_dir}"
            COMMAND
            ${CMAKE_COMMAND}
            -E
            copy_if_different
            "${_src}"
            "${_dst}"
        )
    endforeach()
    set(${_cmds} ${${_cmds}} PARENT_SCOPE)
endfunction()

# Copy shader-package content individually so unchanged files keep their
# timestamps. Track every staged asset (not only HLSL/HLSLI): feature
# shader trees also contain runtime configuration, textures, libraries,
# symbols, and notices that must retrigger staging when changed.
file(
    GLOB_RECURSE _package_shaders
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${CMAKE_SOURCE_DIR}/package/Shaders/*"
)
# Exclude test files from production packages
list(FILTER _package_shaders EXCLUDE REGEX "/Tests/")
set(_all_shader_sources ${_package_shaders})
set(_shader_copy_cmds)

set(_shader_feature_paths ${FEATURE_PATHS})
if(SHADER_VALIDATION_INCLUDE_NON_SHIPPED_FEATURES)
    foreach(_fpath IN LISTS _ALL_FEATURE_PATHS)
        get_filename_component(_feature_dir_name "${_fpath}" NAME)
        if(_feature_dir_name IN_LIST AIO_NON_SHIPPED_FEATURE_PACKAGES)
            list(APPEND _shader_feature_paths "${_fpath}")
            message(
                STATUS
                "Staging non-shipped feature shaders for validation: ${_feature_dir_name}"
            )
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _shader_feature_paths)
endif()

set(_feature_shader_paths
    "${FFX_RUNTIME_SHADER_ROOT}"
    "${STREAMLINE_RUNTIME_SHADER_ROOT}"
)
foreach(_fpath IN LISTS _shader_feature_paths)
    if(EXISTS "${_fpath}/Shaders")
        list(APPEND _feature_shader_paths "${_fpath}/Shaders")
    endif()
endforeach()
# Use the same file transport for feature shader paths so generators do
# not reinterpret space-containing entries as malformed command arguments.
set(_feature_shader_paths_list_file
    "${CMAKE_CURRENT_BINARY_DIR}/feature_shader_paths.txt"
)
string(
    REPLACE ";"
    "\n"
    _feature_shader_paths_file_content
    "${_feature_shader_paths}"
)
file(
    WRITE "${_feature_shader_paths_list_file}"
    "${_feature_shader_paths_file_content}"
)

list(
    APPEND _shader_copy_cmds
    COMMAND
    ${CMAKE_COMMAND}
    -DMODE=SHADERS
    -DAIO_DIR="${AIO_DIR}"
    -DSOURCE_DIR="${CMAKE_SOURCE_DIR}"
    -DFEATURE_SHADER_PATHS_FILE="${_feature_shader_paths_list_file}"
    -DSKIP_RUNTIME_DOWNLOADS=${SKIP_RUNTIME_DOWNLOADS}
    -P
    "${CMAKE_SOURCE_DIR}/cmake/CleanupStaleEntries.cmake"
)

append_copy_if_different(_shader_copy_cmds _package_shaders "${CMAKE_SOURCE_DIR}/package/Shaders" "${AIO_DIR}/Shaders")
append_copy_if_different(_shader_copy_cmds FFX_RUNTIME_FILES "${FFX_RUNTIME_SHADER_ROOT}" "${AIO_DIR}/Shaders")
append_copy_if_different(_shader_copy_cmds STREAMLINE_RUNTIME_FILES "${STREAMLINE_RUNTIME_SHADER_ROOT}" "${AIO_DIR}/Shaders")

# feature shader folders
foreach(_fpath IN LISTS _shader_feature_paths)
    if(EXISTS "${_fpath}/Shaders")
        file(
            GLOB_RECURSE _feat_shaders
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES FALSE
            "${_fpath}/Shaders/*"
        )
        list(FILTER _feat_shaders EXCLUDE REGEX "/Tests/")
        list(APPEND _all_shader_sources ${_feat_shaders})
        append_copy_if_different(_shader_copy_cmds _feat_shaders "${_fpath}/Shaders" "${AIO_DIR}/Shaders")
    endif()
endforeach()
list(REMOVE_DUPLICATES _all_shader_sources)

add_custom_command(
    OUTPUT copy_shaders.stamp
    COMMAND
        ${CMAKE_COMMAND} -E make_directory "${AIO_DIR}/Shaders"
        ${_shader_copy_cmds}
    COMMAND ${CMAKE_COMMAND} -E touch copy_shaders.stamp
    DEPENDS
        ${_all_shader_sources}
        ${FFX_RUNTIME_FILES}
        ${STREAMLINE_RUNTIME_FILES}
        ${_feature_shader_paths_list_file}
        ${CSX_RUNTIME_INSTALL_GUARD}
        ${CMAKE_SOURCE_DIR}/cmake/PreservedRuntimePaths.cmake
        ${CMAKE_SOURCE_DIR}/cmake/CleanupStaleEntries.cmake
    COMMENT "Copying changed shaders into AIO/Shaders"
)

# Standalone target for preparing shaders for CI validation
# This allows shader validation to run without waiting for the full build
add_custom_target(
    prepare_shaders
    DEPENDS copy_shaders.stamp
    COMMENT "Preparing shaders for validation"
)

# Incremental local validation: compile only the shaders affected by the
# current working-tree diff (vs HEAD). Mirrors the CI incremental path but
# derives the change set from git instead of a precomputed list. Override
# the config with -DVALIDATE_CHANGED_CONFIG=... (defaults to Flatrim).
set(VALIDATE_CHANGED_CONFIG
    "${CMAKE_SOURCE_DIR}/.github/configs/shader-validation.yaml"
    CACHE FILEPATH
    "hlslkit config used by the validate_changed target"
)
add_custom_target(
    validate_changed
    COMMAND
        ${CMAKE_COMMAND} -E env python
        "${CMAKE_SOURCE_DIR}/tools/validate_changed_shaders.py" --repo-root
        "${CMAKE_SOURCE_DIR}" -- hlslkit-compile --shader-dir
        "${AIO_DIR}/Shaders" --output-dir "${CMAKE_BINARY_DIR}/ShaderCache"
        --config "${VALIDATE_CHANGED_CONFIG}" --max-warnings 0
        --suppress-warnings X1519
    DEPENDS prepare_shaders
    USES_TERMINAL
    COMMENT "Validating only shaders affected by working-tree changes"
)
