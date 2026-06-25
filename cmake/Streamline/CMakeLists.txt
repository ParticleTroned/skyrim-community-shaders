cmake_minimum_required(VERSION 3.21)

add_library(Streamline INTERFACE)

target_include_directories(
  Streamline INTERFACE ${CMAKE_SOURCE_DIR}/extern/Streamline-DX12/include)

if(MSVC)
  target_compile_options(Streamline INTERFACE /wd5103)
endif()

# Fetch the Streamline plugin DLLs (sl.interposer.dll + sl.*.dll) at configure time. These
# are NOT in the submodule — since SL 2.7.32 they ship only in the release zip — and only the
# headers above come from extern/Streamline-DX12. The zip is large (~213 MB), so the fetch is
# lazy + cached: we download+extract only when STREAMLINE_FETCH is ON AND the extracted
# interposer is not already present. On success STREAMLINE_DLL_DIR is repointed at the folder
# under the extracted tree that holds sl.interposer.dll; the install() COMPONENT + PREPARE_AIO
# staging in the top-level CMakeLists.txt then glob ${STREAMLINE_DLL_DIR}/sl.*.dll from there.
# With STREAMLINE_FETCH=OFF (or no network) STREAMLINE_DLL_DIR stays empty/unset and Streamline
# staging is simply skipped, degrading cleanly to FSR — same as a clean checkout today.
option(STREAMLINE_FETCH "Fetch the Streamline plugin DLLs (~213 MB release zip) at configure time" ON)

set(STREAMLINE_FETCH_URL
    "https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.10.3/streamline-sdk-v2.10.3.zip"
)
set(_sl_deps_dir "${CMAKE_BINARY_DIR}/_deps/streamline")
set(_sl_zip "${_sl_deps_dir}/streamline-sdk-v2.10.3.zip")

# Lazy guard: only fetch when enabled and the interposer is not already extracted.
file(GLOB_RECURSE _sl_interposer "${_sl_deps_dir}/*sl.interposer.dll")
if(STREAMLINE_FETCH AND NOT _sl_interposer)
    message(STATUS "Fetching Streamline plugin DLLs from ${STREAMLINE_FETCH_URL} (~213 MB)")
    file(MAKE_DIRECTORY "${_sl_deps_dir}")
    # Immutable release tag → TLS-verified, but no EXPECTED_HASH (the zip is not pinned here).
    file(
        DOWNLOAD
        "${STREAMLINE_FETCH_URL}"
        "${_sl_zip}"
        TLS_VERIFY ON
        STATUS _sl_dl_status
    )
    list(GET _sl_dl_status 0 _sl_dl_code)
    if(NOT _sl_dl_code EQUAL 0)
        list(GET _sl_dl_status 1 _sl_dl_msg)
        file(REMOVE "${_sl_zip}")
        message(
            FATAL_ERROR
            "Failed to download Streamline SDK zip from '${STREAMLINE_FETCH_URL}' "
            "(status ${_sl_dl_code}: ${_sl_dl_msg}). Set -DSTREAMLINE_FETCH=OFF to skip."
        )
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_sl_zip}" DESTINATION "${_sl_deps_dir}")
    # Re-resolve the interposer after extraction.
    file(GLOB_RECURSE _sl_interposer "${_sl_deps_dir}/*sl.interposer.dll")
endif()

# Point STREAMLINE_DLL_DIR at the directory holding sl.interposer.dll (robust to a top-level
# folder inside the zip — typically <_sl_deps_dir>/.../bin/x64). FORCE so a stale cached value
# (e.g. the old extern/Streamline-DX12/bin/x64 default) is overridden once the DLLs are fetched.
if(_sl_interposer)
    list(GET _sl_interposer 0 _sl_interposer_first)
    get_filename_component(_sl_dll_dir "${_sl_interposer_first}" DIRECTORY)
    set(STREAMLINE_DLL_DIR "${_sl_dll_dir}" CACHE PATH "Directory holding the Streamline plugin DLLs to stage" FORCE)
    message(STATUS "Streamline plugin DLLs available at ${STREAMLINE_DLL_DIR}")
endif()
