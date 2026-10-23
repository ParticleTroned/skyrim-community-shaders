if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.cpp" source)
extract_between("${source}" "\tvoid Renderer::State::LogFailureDiagnosticsLocked("
    "\tvoid Renderer::State::SucceedLocked(" "neural_failure_diagnostics_under_test.h")
extract_between("${source}" "\tvoid Renderer::State::QuarantineAfterUnexpectedFailureLocked("
    "\tbool Renderer::State::EnsureBackendLocked(" "neural_failure_quarantine_under_test.h")
extract_between("${source}" "\t\tstruct ResourceKey\n"
    "\t\tstruct HistoryKey\n" "neural_failure_resource_key_under_test.h")
extract_between("${source}" "\tbool Renderer::State::ServiceStallRecoveryLocked("
    "\tbool Renderer::State::ServiceRetainedBackendLocked(" "neural_stall_service_under_test.h")
extract_between("${source}" "\t\tvoid FinishApplyLocked("
    "\t\tmutable std::mutex mutex_;" "neural_reconstruction_finish_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.h" header)
extract_between("${header}" "\tenum class RendererStage"
    "\tstruct RendererCounters" "neural_failure_stage_under_test.h")

# The declaration, definition and three terminal failure calls are the only sites.
string(REGEX MATCHALL "LogFailureDiagnosticsLocked\\(" sites "${source}")
list(LENGTH sites count)
if(NOT count EQUAL 5)
    message(FATAL_ERROR "Failure diagnostics gained an unreviewed call site")
endif()
extract_between("${source}" "\tvoid Renderer::State::LogFailureDiagnosticsLocked("
    "\tbool Renderer::State::FailLocked(" "neural_failure_capture_under_test.h")
file(READ "${OUTPUT_DIRECTORY}/neural_failure_capture_under_test.h" capture)
if(capture MATCHES "(WaitForIdle|Flush|BeginD3D12|EndD3D12|SetOwnerWork|GetDesc)\\(")
    message(FATAL_ERROR "Failure capture must remain read-only and never wait on GPU work")
endif()
