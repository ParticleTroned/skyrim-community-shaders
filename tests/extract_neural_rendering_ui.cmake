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
    string(REPLACE "Util::Widgets::" "ImGui::" result "${result}")
    set(${output} "${result}" PARENT_SCOPE)
endfunction()

extract("src/Features/NeuralRenderingFeature.cpp"
    "std::string_view NeuralRenderingFeature::GetSettingsFooterText() const"
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
    "bool Upscaling::ToggleNeuralRendering("
    "void Upscaling::DrawNeuralRenderingSettings(" master_control)
extract("src/Features/Upscaling.cpp"
    "\t\tNeuralRendering::NormalizeRenderingCoverage(settings, globals::game::isVR);\n\t\tconst bool dlssSelected = a_upscaleMethod == UpscaleMethod::kDLSS;"
    "\t\t{\n\t\t\tif (page.Is(\"diagnostics\")" selection_controls)
extract("src/Features/Upscaling.cpp"
    "bool Upscaling::IsNeuralRenderingEnabled("
    "void Upscaling::DrawPeripheryTAAControl(" availability_policy)
extract("src/Features/Upscaling.cpp"
    "bool Upscaling::ApplyNeuralRenderingPreset("
    "bool Upscaling::HasSameNeuralRenderingSettingsKey(" image_preset)
extract("src/Features/Upscaling.cpp"
    "\t\t\tDrawNeuralRenderingSharedImageSettings(settings);"
    "\t\t\tif (page.Is(\"actors\")) {" appearance_controls)
extract("src/Features/Upscaling.cpp"
    "bool Upscaling::IsNeuralRenderingHardwareSupported() const noexcept"
    "NeuralRendering::RenderingMode Upscaling::GetNeuralRenderingMode()" execution_gate)
extract("src/Features/Upscaling.cpp"
    "bool Upscaling::IsFoveatedVendorDispatchEnabled("
    "bool Upscaling::IsFSRRuntimePathActive(" dispatch_gate)
extract("src/Features/Upscaling.cpp"
    "bool Upscaling::IsActiveUpscalingFoveatedProfileAvailable()"
    "bool Upscaling::IsNeuralRenderingFovConfigurationAvailable()" profile_gate)
# The policy harness visits each panel together; native navigation has a separate ImGui test.
string(FIND "${selection_controls}" "\t\tconst bool usesFov =" page_start)
string(FIND "${selection_controls}" "\t\tif (page.Is(\"mode\")) {" page_end)
if(page_start LESS 0 OR page_end LESS_EQUAL page_start)
    message(FATAL_ERROR "NR tab declaration boundaries are missing")
endif()
string(SUBSTRING "${selection_controls}" 0 ${page_start} before_page)
string(SUBSTRING "${selection_controls}" ${page_end} -1 after_page)
set(selection_controls "${before_page}${after_page}")
foreach(panel IN ITEMS selection_controls appearance_controls)
    string(REGEX REPLACE "page\\.Is\\(\"[^\"]+\"\\)" "true" ${panel} "${${panel}}")
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_DIRECTORY}/neural_rendering_ui_under_test.h"
    "${fov_profile}\n${fov_request}\n${fov_readiness}\n${developer_mode}\n${draw_settings}\n${availability_policy}\n${execution_gate}\n${dispatch_gate}\n${profile_gate}\n${image_preset}\n${master_control}\nvoid Upscaling::DrawSelectionControls() {\nconst auto a_upscaleMethod = GetUpscaleMethod();\nconst bool showDiagnostics = globals::state && globals::state->IsDeveloperMode();\nconst std::function<void(bool)> a_drawColourSettings;\nDrawNeuralRenderingEnableControl();\n${selection_controls}\n(void)missingRenderScale;\n(void)reducedResolution;\n${appearance_controls}\n}\n}\n")

extract("src/Features/VR/Input.cpp"
    "bool VR::IsControllerComboPressed("
    "void VR::UpdateOverlayMenuStateFromInput()" controller_toggle)
extract("src/Features/VR/Input.cpp"
    "void VR::ProcessVREvents("
    "void VR::ProcessVRButtonEvent(" controller_events)
file(WRITE "${OUTPUT_DIRECTORY}/neural_controller_toggle_under_test.h" "${controller_toggle}\n${controller_events}\n")

extract("src/Menu/StabilizerPage.cpp"
    "\t\tconst auto& upscaling = globals::features::upscaling;"
    "\t\tImGui::Spacing();\n\t\tImGui::SeparatorText(\"Features\");" stabilizer_warning)
file(APPEND "${OUTPUT_DIRECTORY}/neural_rendering_ui_under_test.h"
    "\nvoid DrawStabilizerNRWarnings(const Upscaling::VRFpsStabilizerConfig& config, bool showNotConfigured) {\n${stabilizer_warning}\n}\n")

extract("src/Menu/StabilizerPage.cpp"
    "\tconstexpr std::array<const char*, 4> kVRFpsStabilizerMethodNames"
    "\tstruct VRFpsStabilizerUIState" stabilizer_names)
extract("src/Menu/StabilizerPage.cpp"
    "\tbool DrawVRFpsStabilizerUpscaleMethod("
    "\tbool DrawVRFpsStabilizerFeatureToggle(" stabilizer_selectors)
extract("src/Features/Upscaling.cpp"
    "\tbool DrawUpscalingMethodSelection("
    "void Upscaling::DrawSettingsHeaderControls()" method_selection)
file(APPEND "${OUTPUT_DIRECTORY}/neural_rendering_ui_under_test.h"
    "\nnamespace {\n${stabilizer_names}\n${stabilizer_selectors}\n${method_selection}\n")
