if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling/Streamline.cpp" _streamline)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/Streamline.h" _header)
foreach(_forbidden IN ITEMS "slReflexSetOptions" "slReflexSleep" "slReflexGetState")
    string(FIND "${_streamline}${_header}" "${_forbidden}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR "Parallel SDK Reflex state path remains: ${_forbidden}")
    endif()
endforeach()

foreach(_required IN ITEMS
    "reflexBackend.Update(options, currentFrame, GetTickCount64())"
    "reflexBackend.Disable(GetTickCount64())"
    "return outcome.disabled && !frameGenerationQuarantinedByReflex.load"
    "slGetNativeInterface(a_device"
    "reflexBackend.BindDevice(nativeDevice.Get(), adapterSupportsReflex)"
    "reflexBackend.Shutdown()"
    "requestFeatureLoad(sl::kFeatureReflex"
    "requestFeatureLoad(sl::kFeaturePCL"
    "slPCLSetOptions(sl::PCLOptions{})"
    "slPCLSetMarker(marker, *frameToken)"
    "ReflexPolicy::ResolveCSXMarkerOptimization"
)
    string(FIND "${_streamline}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Missing native Reflex integration contract: ${_required}")
    endif()
endforeach()

string(FIND "${_streamline}" "void Streamline::MarkAdapterUnavailable(" _unavailable_start)
string(FIND "${_streamline}" "bool Streamline::CheckFeatures(" _unavailable_end)
if(_unavailable_start EQUAL -1 OR _unavailable_end LESS_EQUAL _unavailable_start)
    message(FATAL_ERROR "Cannot locate the adapter-unavailable boundary")
endif()
math(EXPR _unavailable_length "${_unavailable_end} - ${_unavailable_start}")
string(SUBSTRING "${_streamline}" ${_unavailable_start} ${_unavailable_length} _unavailable)
foreach(_required IN ITEMS
    "reflexBackend.BindDevice(nullptr, false)"
    "boundDeviceIdentity = nullptr"
)
    string(FIND "${_unavailable}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Adapter loss must invalidate old native Reflex state: ${_required}")
    endif()
endforeach()

string(FIND "${_streamline}" "void Streamline::UpdateReflex()" _update_start)
string(SUBSTRING "${_streamline}" ${_update_start} -1 _update)
string(FIND "${_update}" "AcquireFrameToken" _token)
if(NOT _token EQUAL -1)
    message(FATAL_ERROR "Native sleep must not depend on a Streamline frame token")
endif()
