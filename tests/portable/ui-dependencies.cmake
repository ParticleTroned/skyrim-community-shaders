# Native builds reuse the project's real UI libraries. Standalone portable
# builds may provide packages or explicitly fetch the same pinned versions.
if(NOT TARGET imgui::imgui)
    find_package(imgui CONFIG QUIET)
endif()
if(NOT TARGET fmt::fmt)
    find_package(fmt CONFIG QUIET)
endif()
option(CSX_PORTABLE_FETCH_UI_DEPS "Fetch pinned ImGui and fmt for production preset UI tests" OFF)
if(CSX_PORTABLE_FETCH_UI_DEPS)
    include(FetchContent)
    if(NOT TARGET imgui::imgui)
        FetchContent_Declare(csx_ui_imgui
            URL https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.92.6-docking
            URL_HASH SHA256=5e84cdaa6a6041586a0d11a3071b749734a0439d66fdbdad37ae5b27e37d396c
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
        FetchContent_MakeAvailable(csx_ui_imgui)
        add_library(csx_portable_imgui STATIC
            "${csx_ui_imgui_SOURCE_DIR}/imgui.cpp"
            "${csx_ui_imgui_SOURCE_DIR}/imgui_draw.cpp"
            "${csx_ui_imgui_SOURCE_DIR}/imgui_tables.cpp"
            "${csx_ui_imgui_SOURCE_DIR}/imgui_widgets.cpp"
            "${csx_ui_imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp")
        target_include_directories(csx_portable_imgui PUBLIC
            "${csx_ui_imgui_SOURCE_DIR}" "${csx_ui_imgui_SOURCE_DIR}/misc/cpp")
        add_library(imgui::imgui ALIAS csx_portable_imgui)
    endif()
    if(NOT TARGET fmt::fmt)
        FetchContent_Declare(csx_ui_fmt
            URL https://codeload.github.com/fmtlib/fmt/tar.gz/refs/tags/12.1.0
            URL_HASH SHA256=ea7de4299689e12b6dddd392f9896f08fb0777ac7168897a244a6d6085043fea
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE
            EXCLUDE_FROM_ALL)
        FetchContent_MakeAvailable(csx_ui_fmt)
    endif()
endif()
if(NOT TARGET imgui::imgui OR NOT TARGET fmt::fmt)
    message(FATAL_ERROR
        "Preset UI regression tests require genuine ImGui 1.92.6 docking and fmt. "
        "Provide their package directories or set CSX_PORTABLE_FETCH_UI_DEPS=ON. "
        "Offline sources may use FETCHCONTENT_SOURCE_DIR_CSX_UI_IMGUI and "
        "FETCHCONTENT_SOURCE_DIR_CSX_UI_FMT.")
endif()
