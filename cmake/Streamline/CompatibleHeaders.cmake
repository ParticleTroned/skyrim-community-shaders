function(csx_prepare_streamline_headers source_directory output_root output_variable)
    set(_pcl_source "${source_directory}/sl_pcl.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_pcl_source}")
    file(READ "${_pcl_source}" _pcl)
    string(REPLACE "\r\n" "\n" _pcl "${_pcl}")
    set(_original [=[#if __cplusplus == 202302L
using to_underlying = std::to_underlying;
#else
// Return `enum class` member as value of underlying type (i.e. an int).  Basically same as:
// static_cast<std::underlying_type_t<decltype(value)>>(value);
// See c++23s std::to_underlying()
template<class T>
constexpr auto to_underlying(T value)
{
    return std::underlying_type_t<T>(value);
}
#endif]=])
    string(FIND "${_pcl}" "${_original}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Streamline sl_pcl.h changed; review the C++23 compatibility patch before updating the generated headers.")
    endif()
    set(_replacement [=[template<class T>
constexpr auto to_underlying(T value)
{
    return std::underlying_type_t<T>(value);
}]=])
    string(REPLACE "${_original}" "${_replacement}" _pcl "${_pcl}")

    # Quoted sibling includes must resolve within the same corrected header tree.
    file(GLOB_RECURSE _headers CONFIGURE_DEPENDS LIST_DIRECTORIES false
        RELATIVE "${source_directory}" "${source_directory}/*")
    # A changed inventory gets its own directory so removed SDK headers cannot linger.
    string(SHA256 _layout "${_headers}")
    set(_include_directory "${output_root}/${_layout}")
    foreach(_header IN LISTS _headers)
        if(NOT _header STREQUAL "sl_pcl.h")
            configure_file("${source_directory}/${_header}" "${_include_directory}/${_header}" COPYONLY)
        endif()
    endforeach()
    set(_pcl_output "${_include_directory}/sl_pcl.h")
    set(_previous "")
    if(EXISTS "${_pcl_output}")
        file(READ "${_pcl_output}" _previous)
    endif()
    # Unchanged configuration must not force recompilation of SDK consumers.
    if(NOT _previous STREQUAL _pcl)
        file(WRITE "${_pcl_output}" "${_pcl}")
    endif()
    set("${output_variable}" "${_include_directory}" PARENT_SCOPE)
endfunction()
