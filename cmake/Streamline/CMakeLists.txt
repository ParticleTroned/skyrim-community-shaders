cmake_minimum_required(VERSION 3.21)

add_library(Streamline INTERFACE)

target_include_directories(
  Streamline INTERFACE ${CMAKE_SOURCE_DIR}/extern/Streamline/include)

if(MSVC)
  target_compile_options(Streamline INTERFACE /wd5103)
endif()

# The Streamline runtime DLLs are NOT fetched here. The signed production set
# (sl.common/dlss/dlss_g/pcl/reflex + nvngx_* NGX models + NvLowLatencyVk.dll)
# and the FSR/XeSS runtimes (amd_fidelityfx_vk.dll, libxess.dll) are committed
# under package/SKSE/Plugins/CommunityShaders/Streamline/ and ship with the
# package tree; the CS-built fork plugins (sl.interposer, sl.fsr, sl.xess) are
# staged from extern/Streamline/_artifacts by the top-level CMakeLists.txt.
