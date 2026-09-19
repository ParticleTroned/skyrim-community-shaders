if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

function(extract path start_marker end_marker output)
    file(READ "${PROJECT_ROOT}/${path}" source)
    string(FIND "${source}" "${start_marker}" start)
    string(FIND "${source}" "${end_marker}" end)
    if(start EQUAL -1 OR end EQUAL -1 OR end LESS_EQUAL start)
        message(FATAL_ERROR "Neural Rendering UI extraction boundaries are missing")
    endif()
    math(EXPR length "${end} - ${start}")
    string(SUBSTRING "${source}" ${start} ${length} result)
    set(${output} "${result}" PARENT_SCOPE)
endfunction()

extract("src/Features/NeuralRenderingFeature.cpp"
    "void NeuralRenderingFeature::DrawSettings()"
    "void NeuralRenderingFeature::DataLoaded()" draw_settings)
extract("src/State.cpp"
    "bool State::IsDeveloperMode()"
    "void State::ModifyRenderTarget(" developer_mode)
extract("src/Features/Upscaling.cpp"
    "\tbool SupportsFoveatedVendorDispatch("
    "\tbool ShouldUseReducedResolutionForUpscaling(" fov_request)
extract("src/Features/Upscaling.cpp"
    "\tstruct FoveatedMaskProfileParams"
    "\tfloat FoveatedMaskDistanceUV(" fov_profile)
extract("src/Features/Upscaling.cpp"
    "bool Upscaling::IsNeuralRenderingFovConfigurationAvailable()"
    "const char* Upscaling::GetFoveatedUpscalingModeName(" fov_readiness)
extract("src/Features/Upscaling.cpp"
    "\t\tconst bool fovAvailable = IsNeuralRenderingFovConfigurationAvailable(a_upscaleMethod);"
    "\t\tconst bool routeAvailable =" selection_controls)
extract("src/Features/Upscaling.cpp"
    "bool Upscaling::IsNeuralRenderingRequested() const noexcept"
    "NeuralRendering::RenderingMode Upscaling::GetNeuralRenderingMode()" execution_gate)
extract("src/Features/Upscaling.cpp"
    "bool Upscaling::IsFoveatedVendorDispatchEnabled("
    "bool Upscaling::IsFSRRuntimePathActive(" dispatch_gate)
extract("src/Features/Upscaling.cpp"
    "bool Upscaling::IsActiveUpscalingFoveatedProfileAvailable()"
    "bool Upscaling::IsNeuralRenderingFovConfigurationAvailable()" profile_gate)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_DIRECTORY}/neural_rendering_ui_under_test.h"
    "${fov_profile}\n${fov_request}\n${fov_readiness}\n${developer_mode}\n${draw_settings}\n${execution_gate}\n${dispatch_gate}\n${profile_gate}\nvoid Upscaling::DrawSelectionControls(bool a_essentialsOnly) {\nconst auto a_upscaleMethod = GetUpscaleMethod();\n${selection_controls}\n}\n")
