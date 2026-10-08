include_guard(GLOBAL)

# These paths are relative to Shaders; skip mode leaves deployed runtimes alone.
function(csx_is_preserved_runtime_path _relative_path _out_var)
    string(REPLACE "\\" "/" _path "${_relative_path}")
    string(TOLOWER "${_path}" _path)
    set(_preserved FALSE)
    if(
        SKIP_RUNTIME_DOWNLOADS
        AND _path MATCHES "^upscaling/(fidelityfx|streamline)/"
    )
        set(_preserved TRUE)
    endif()
    set(${_out_var} ${_preserved} PARENT_SCOPE)
endfunction()
