if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/Renderer.cpp" source)
extract_between("${source}" "\t\tbool IsDeviceLossResult("
    "\t\tusing Util::SameIdentity;" "neural_device_loss_under_test.h")
extract_between("${source}" "\t\tclass D3D11CapabilityCache"
    "\t\tbool SupportsD3D12Format(" "neural_d3d11_capabilities_under_test.h")
extract_between("${source}" "\tHRESULT Renderer::State::GetDeviceRemovalReasonLocked("
    "\tvoid Renderer::State::LogFailureDiagnosticsLocked(" "neural_device_loss_invalidation_under_test.h")
