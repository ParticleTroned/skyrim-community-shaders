if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_FILE are required")
endif()
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _source)
get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
extract_between("${_source}" "\tbool IsVRRenderScaleNativeRecoveryProfile(" "\tbool IsVRRenderScaleRelatchActivationTarget(" "vr_recovery_preserve.h")
extract_between("${_source}" "std::optional<Upscaling::VRRenderScaleDesiredProfile> Upscaling::TakePendingVRRenderScaleRequest()" "bool Upscaling::IsLatestVRRenderScaleRequest(" "vr_recovery_take.h")
extract_between("${_source}" "bool Upscaling::HasPendingVRUpscalingTransition() const" "bool Upscaling::HasPendingVRRenderScaleTransition() const" "vr_recovery_pending.h")
string(FIND "${_source}" "void Upscaling::RequestPerfModeRenderTargetRecreate(" _request_start)
if(_request_start EQUAL -1)
    message(FATAL_ERROR "Cannot find recovery snapshot producer")
endif()
string(SUBSTRING "${_source}" ${_request_start} -1 _request)
extract_between("${_request}" "\tconst auto controllerSnapshot = GetVRRenderScaleTransitionSnapshot();" "\tconst bool directMenuRequestRelatch =" "vr_recovery_select.h")
extract_between("${_request}" "\tif (a_providerNeutralNativeRecovery) {\n\t\tpendingVRRenderScaleRecoverySnapshot = {};" "\tconst uint32_t currentFrame = globals::state ?" "vr_recovery_capture.h")
file(WRITE "${OUTPUT_FILE}" [=[
#include "vr_recovery_preserve.h"
#include "vr_recovery_take.h"
#include "vr_recovery_pending.h"
void Upscaling::CaptureRecoverySnapshot(
    VRUpscalingTransitionOrigin effectiveOrigin,
    const PerfModeState::BootSnapshot* a_recoverySnapshot,
    bool a_providerNeutralNativeRecovery,
    uint64_t relatchEpoch)
{
#include "vr_recovery_select.h"
#include "vr_recovery_capture.h"
}
]=])
