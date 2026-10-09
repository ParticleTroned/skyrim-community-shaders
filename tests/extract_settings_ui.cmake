if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
foreach(kind IN ITEMS page widget)
    if(kind STREQUAL "page")
        set(path "src/Menu/SettingsPage.cpp")
        set(marker "namespace MenuUI")
    else()
        set(path "src/Utils/Slider.cpp")
        set(marker "namespace Util::Widgets")
    endif()
    file(READ "${PROJECT_ROOT}/${path}" source)
    string(FIND "${source}" "${marker}" start)
    if(start LESS 0)
        message(FATAL_ERROR "Settings UI namespace is missing")
    endif()
    string(SUBSTRING "${source}" ${start} -1 body)
    file(WRITE "${OUTPUT_DIRECTORY}/settings_${kind}_under_test.h" "${body}")
endforeach()
file(READ "${PROJECT_ROOT}/src/Utils/UI.h" source)
string(FIND "${source}" "\tnamespace Widgets" start)
string(FIND "${source}" "\n\tvoid UpdateImGuiInput" end)
math(EXPR length "${end} - ${start}")
string(SUBSTRING "${source}" ${start} ${length} declarations)
string(FIND "${source}" "		inline ImVec4 SecondaryText()" color_start)
string(SUBSTRING "${source}" ${color_start} -1 color_rest)
string(FIND "${color_rest}" "
		}" color_end)
math(EXPR color_length "${color_end} + 4")
string(SUBSTRING "${color_rest}" 0 ${color_length} secondary_text)
file(WRITE "${OUTPUT_DIRECTORY}/settings_widget_declarations.h"
    "#pragma once\nnamespace Util { ${declarations} }")

file(READ "${OUTPUT_DIRECTORY}/settings_page_under_test.h" page_body)
file(WRITE "${OUTPUT_DIRECTORY}/settings_page_under_test.h"
    "#include \"settings_widget_declarations.h\"\nnamespace Util::Color { ${secondary_text} }\n${page_body}")

file(READ "${PROJECT_ROOT}/src/Features/VolumetricLighting.cpp" source)
set(prefixes "")
foreach(method IN ITEMS DrawSettings)
    string(FIND "${source}" "void VolumetricLighting::${method}()" start)
    string(SUBSTRING "${source}" ${start} -1 rest)
    string(FIND "${rest}" "auto drawVRRestartHint" end)
    if(start LESS 0 OR end LESS 0)
        message(FATAL_ERROR "Volumetric lighting page preamble is missing")
    endif()
    string(SUBSTRING "${rest}" 0 ${end} prefix)
    string(APPEND prefixes "${prefix}}\n")
endforeach()
file(WRITE "${OUTPUT_DIRECTORY}/settings_lighting_pages_under_test.h" "${prefixes}")

include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" source)
extract_between("${source}"
    "\tvoid DrawNeuralIntegerSetting("
    "\tvoid DrawNeuralRenderingSharedImageSettings("
    "settings_model_resolution_under_test.h")

# Exercise the production menu settings declaration and serializer with legacy inputs.
file(READ "${PROJECT_ROOT}/src/Menu.h" source)
string(FIND "${source}" "	struct Settings\n	{" start)
string(SUBSTRING "${source}" ${start} -1 rest)
string(FIND "${rest}" "\n	};" end)
math(EXPR length "${end} + 4")
string(SUBSTRING "${rest}" 0 ${length} declaration)
file(READ "${PROJECT_ROOT}/src/Menu.cpp" source)
string(FIND "${source}" "NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(\n	Menu::Settings," start)
string(SUBSTRING "${source}" ${start} -1 rest)
string(FIND "${rest}" ")" end)
math(EXPR length "${end} + 1")
string(SUBSTRING "${rest}" 0 ${length} serializer)
file(WRITE "${OUTPUT_DIRECTORY}/menu_settings_under_test.h"
    "struct Menu { using ThemeSettings = nlohmann::json; ${declaration} };\n${serializer}\n")

# Exercise shared external actions and the Stabilizer's draft guard with real ImGui.
file(READ "${PROJECT_ROOT}/src/Menu/FeatureListRenderer.cpp" source)
extract_between("${source}"
    "\tconstexpr float footerTextScale"
    "\tstruct FeatureBannerTexture"
    "settings_footer_under_test.h")
file(READ "${PROJECT_ROOT}/src/Menu/ExternalSettingsPage.h" source)
extract_between("${source}"
    "\tstruct SettingsAction"
    "\t/** Menu-only"
    "settings_external_actions_under_test.h")
file(READ "${PROJECT_ROOT}/src/Menu/StabilizerPage.cpp" source)
extract_between("${source}"
    "\tstd::string editorGroup"
    "\n}\n\nnamespace MenuUI"
    "settings_stabilizer_navigation_under_test.h")

# Exercise complete material-page draw paths with only game services substituted.
file(READ "${PROJECT_ROOT}/src/Utils/UI.cpp" source)
extract_between("${source}" "	bool UIntCheckbox(" "
	namespace" "settings_uint_checkbox_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/FoliageLighting.cpp" source)
extract_between("${source}" "void FoliageLighting::DrawFoliageScatteringSetting()" "void FoliageLighting::DrawPerformanceSettings" "settings_foliage_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/FoliageLighting.h" source)
extract_between("${source}" "	struct alignas(16) Settings" "	STATIC_ASSERT_ALIGNAS_16" "settings_foliage_fields_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/SubsurfaceScattering.cpp" source)
extract_between("${source}" "	void DrawHumanSkinControls(" "
	void ApplyClampedHumanSkinControls(" "settings_skin_controls_under_test.h")
extract_between("${source}" "void SubsurfaceScattering::DrawSettings()" "void SubsurfaceScattering::DrawPerformanceSettings" "settings_skin_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/SubsurfaceScattering.h" source)
extract_between("${source}" "	struct DiffusionProfile" "
	float CharacterLightingStrengthOriginal" "settings_skin_fields_under_test.h")

file(READ "${PROJECT_ROOT}/src/Utils/WinApi.cpp" source)
extract_between("${source}" "	bool OpenInShell(" "
	std::optional<REL::Version>" "settings_shell_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/ScreenshotFeature.cpp" source)
extract_between("${source}" "	MenuUI::ActionFeedback CaptureRequestFeedback(" "
}

void ScreenshotFeature::DrawSettings()" "settings_capture_feedback_under_test.h")

file(READ "${PROJECT_ROOT}/src/Menu/FeatureListRenderer.cpp" source)
string(FIND "${source}" "void FeatureListRenderer::DrawMenuVisitor::RenderReactiveConstraintWarningDialog()" warning_start)
if(warning_start LESS 0)
    message(FATAL_ERROR "Constraint warning dialog is missing")
endif()
string(SUBSTRING "${source}" ${warning_start} -1 warning_body)
string(REPLACE "ImGui::Selectable(" "TrackedSelectable(" warning_body "${warning_body}")
file(WRITE "${OUTPUT_DIRECTORY}/settings_constraint_warning_under_test.h" "${warning_body}")
