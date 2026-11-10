if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/ReverseZ.cpp" source)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between("${source}" "\tstd::mutex g_stateMutex;"
    "\n\tDXGI_FORMAT ToFloatDepthFormat" "reverse_z_state_storage.h")
extract_between("${source}" "\tD3D11_COMPARISON_FUNC FlipComparison("
    "\n\tenum class ZAxis" "reverse_z_state_comparison.h")
extract_between("${source}" "void ReverseZ::RecordFailure("
    "void ReverseZ::ApplyReverseProjection(" "reverse_z_state_creation.h")
extract_between("${source}" "\tID3D11DepthStencilState* ResolveRequestedDepthState()"
    "\n\tID3D11RenderTargetView* g_lastClearedCubeFace" "reverse_z_state_hooks.h")
extract_between("${source}" "\tstruct ID3D11DeviceContext_ClearState"
    "\n}\n\nvoid ReverseZ::InstallRuntimeHooks" "reverse_z_state_clear.h")
extract_between("${source}" "\tstruct ID3D11DeviceContext_OMGetDepthStencilState"
    "\n\tthread_local bool t_inOccluderDownscale" "reverse_z_state_getters.h")
extract_between("${source}" "\tusing RENDER_TARGET_DEPTHSTENCIL ="
    "\n\t// Flat layout" "reverse_z_state_targets.h")
extract_between("${source}" "\tstd::shared_mutex g_viewMutex;"
    "\n\tstd::mutex g_stateMutex;" "reverse_z_state_views.h")
extract_between("${source}" "\tDXGI_FORMAT ToFloatDepthFormat"
    "\n\tD3D11_COMPARISON_FUNC FlipComparison" "reverse_z_state_resources.h")
extract_between("${source}" "bool ReverseZ::PrepareDepthTargets()"
    "\nID3D11DepthStencilView* ReverseZ::ResolveCubemapFaceDepthView" "reverse_z_state_preparation.h")
extract_between("${source}" "bool ReverseZ::HasPendingBootChange() const"
    "\nvoid ReverseZ::DrawSettings" "reverse_z_state_diagnostics.h")
