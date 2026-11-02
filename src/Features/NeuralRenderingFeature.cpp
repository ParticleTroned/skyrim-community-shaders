#include "NeuralRenderingFeature.h"
#include "Menu.h"
#include "Menu/SettingsPage.h"
#include "Upscaling.h"
#include "Upscaling/NeuralRendering/ExposureCapture.h"
#include "Upscaling/NeuralRendering/MonoDevBenchBridge.h"
#include "Upscaling/NeuralRendering/MonoRenderer.h"
#include "Upscaling/NeuralRendering/MonoSettingsSerialization.h"
#include "Upscaling/NeuralRendering/Runtime.h"
#include "Utils/UI.h"
#include <algorithm>
#include <utility>

void NeuralRenderingFeature::SetupResources()
{
	globals::features::upscaling.SetNeuralRenderingFeatureAvailable(loaded);
	NeuralRendering::MonoDevBenchBridge::Install();
}
void NeuralRenderingFeature::LoadSettings(json& configuration)
{
	auto settings = configuration.get<NeuralRendering::MonoSettings>();
	NeuralRendering::Sanitize(settings);
	globals::features::upscaling.neuralSettings = settings;
	globals::features::upscaling.SetNeuralRenderingFeatureAvailable(loaded);
}
void NeuralRenderingFeature::SaveSettings(json& configuration) { configuration = globals::features::upscaling.neuralSettings; }
void NeuralRenderingFeature::RestoreDefaultSettings()
{
	globals::features::upscaling.neuralSettings = {};
	globals::features::upscaling.SetNeuralRenderingFeatureAvailable(loaded);
}
void NeuralRenderingFeature::ClearShaderCache() { NeuralRendering::MonoRenderer::Instance().ResetShaderCache(); }

namespace
{
	using NeuralRendering::MonoSettings;
	void DrawUIntSlider(const char* label, std::uint32_t& value, int minimum, int maximum, const char* format = "%d")
	{
		int edit = static_cast<int>(value);
		if (Util::Widgets::SliderInt(label, &edit, minimum, maximum, format))
			value = static_cast<std::uint32_t>(edit);
	}

	void DrawMode(MonoSettings& settings, Upscaling& upscaling)
	{
		const char* networks[]{ T("feature.neural_rendering.network_default", "Provider default"), T("feature.neural_rendering.network_legacy", "Legacy network"), T("feature.neural_rendering.network_newer", "Newer network") };
		int network = static_cast<int>(settings.network);
		{
			const auto guard = Util::DisableGuard(!NeuralRendering::Runtime::Instance().SupportsNetworkSelection());
			if (Util::Widgets::Combo(T("feature.neural_rendering.network", "Network"), &network, networks, 3)) {
				settings.network = static_cast<std::uint32_t>(network);
				const auto limits = NeuralRendering::Runtime::Instance().GetTuningLimits(settings.network);
				settings.intensity = std::min(settings.intensity, limits.intensity);
				settings.localTone = std::min(settings.localTone, limits.localToneStrength);
				settings.localStructure = std::min(settings.localStructure, limits.localStructureStrength);
				settings.skinStructure = std::min(settings.skinStructure, limits.skinStructureStrength);
			}
		}

		const MenuUI::Choice placements[]{
			{ "final", T("feature.neural_rendering.final_scene", "Final scene before UI"), T("feature.neural_rendering.final_scene_summary", "After upscaling at output resolution"), T("feature.neural_rendering.final_scene_help", "Enhance the completed scene before the interface and display encoding.") },
			{ "scaled", T("feature.neural_rendering.before_upscaling", "Scene before upscaling"), T("feature.neural_rendering.before_upscaling_summary", "Before DLSS or FSR at render resolution"), T("feature.neural_rendering.before_upscaling_help", "Enhance the smaller scene first, then let the selected temporal upscaler reconstruct the output.") }
		};
		const int placement = MenuUI::ChoiceCards("Placement", settings.mode == 2 ? 1 : 0, placements);
		if (placement >= 0)
			settings.mode = placement == 1 ? 2u : 0u;
		if (settings.mode == 2 && upscaling.GetUpscaleMethod() != Upscaling::UpscaleMethod::kDLSS && upscaling.GetUpscaleMethod() != Upscaling::UpscaleMethod::kFSR)
			ImGui::TextWrapped("%s", T("feature.neural_rendering.upscaler_required", "The scene-before-upscaling placement requires DLSS or FSR. Its history resets on every inference; the selected upscaler owns temporal reconstruction."));
		DrawUIntSlider(T("feature.neural_rendering.model_resolution", "Inference resolution (%)"), settings.modelResolutionPercent, 30, 100);
		DrawUIntSlider(T("feature.neural_rendering.compression", "Rectangular peripheral compression (%)"), settings.peripheralCompressionPercent, 0, 100);
		if (settings.peripheralCompressionPercent)
			ImGui::TextWrapped("%s", T("feature.neural_rendering.compression_help", "Experimental: compresses the full rectangular inference image toward its center, then reconstructs every source pixel. Actor selection, restricted regions or excessive filtering footprints retain uniform inference. Validate the image quality for your scene."));
		Util::Widgets::Checkbox(T("feature.neural_rendering.pressure", "Reduce inference resolution during GPU memory pressure"), &settings.pressureResolution);
		Util::Widgets::Checkbox(T("feature.neural_rendering.prewarm", "Prepare the exact model configuration at a main or loading menu"), &settings.prewarm);
		if (settings.prewarm)
			ImGui::TextWrapped("%s", T("feature.neural_rendering.prewarm_help", "Prepares only a known full-scene configuration at an idle renderer boundary, with a 30-second unused lease. Disable adaptive inference resolution and actor/category selection for exact prewarming. Changed geometry retires the unused configuration before inference."));
		Util::Widgets::Checkbox(T("feature.neural_rendering.combat", "Pause neural rendering during combat"), &settings.pauseInCombat);

		MenuUI::SectionHeading(T("feature.neural_rendering.screen_region", "Screen-space region"));
		DrawUIntSlider(T("feature.neural_rendering.region", "Region size (%)"), settings.regionPercent, 25, 100);
		DrawUIntSlider(T("feature.neural_rendering.feather", "Edge feather (pixels)"), settings.featherPixels, 0, 256);
		Util::Widgets::SliderFloat(T("feature.neural_rendering.horizontal", "Horizontal shape"), &settings.horizontalScale, 0.5f, 2.0f);
		Util::Widgets::SliderFloat2(T("feature.neural_rendering.offset", "Region offset"), settings.regionOffset.data(), -0.5f, 0.5f);
	}

	void DrawBlending(MonoSettings& settings)
	{
		const MenuUI::Choice reconstruction[]{
			{ "basic", T("feature.neural_rendering.reconstruction_basic", "Basic reconstruction"), T("feature.neural_rendering.reconstruction_basic_summary", "Reconstruct the neural residual"), T("feature.neural_rendering.reconstruction_basic_help", "Blend the reconstructed neural change into the original source image.") },
			{ "guided", T("feature.neural_rendering.reconstruction_guided", "Guided reconstruction"), T("feature.neural_rendering.reconstruction_guided_summary", "Preserve source detail"), T("feature.neural_rendering.reconstruction_guided_help", "Use source guidance when reconstructing the neural change at full scene resolution.") }
		};
		const int choice = MenuUI::ChoiceCards("Reconstruction", settings.guidedReconstruction ? 1 : 0, reconstruction);
		if (choice >= 0)
			settings.guidedReconstruction = choice == 1;
		MenuUI::DetailNote(T("feature.neural_rendering.blending_help", "Reconstruction preserves the original scene outside the selected region. Material strengths are applied by the SE compositor; select them in Selection."));
	}

	void DrawLook(MonoSettings& settings)
	{
		const char* styles[]{ T("feature.neural_rendering.standard", "Standard"), T("feature.neural_rendering.natural", "Natural"), T("feature.neural_rendering.cinematic", "Cinematic") };
		int style = static_cast<int>(settings.style);
		if (Util::Widgets::Combo(T("feature.neural_rendering.style", "Style"), &style, styles, 3))
			settings.style = static_cast<std::uint32_t>(style);
		const auto limits = NeuralRendering::Runtime::Instance().GetTuningLimits(settings.network);
		Util::Widgets::SliderFloat(T("feature.neural_rendering.intensity", "Intensity"), &settings.intensity, 0.0f, limits.intensity);
		Util::Widgets::SliderFloat(T("feature.neural_rendering.tone", "Local tone"), &settings.localTone, 0.0f, limits.localToneStrength);
		Util::Widgets::SliderFloat(T("feature.neural_rendering.structure", "Local structure"), &settings.localStructure, 0.0f, limits.localStructureStrength);
		Util::Widgets::SliderFloat(T("feature.neural_rendering.skin_structure", "Skin structure"), &settings.skinStructure, 0.0f, limits.skinStructureStrength);
	}

	void DrawColour(MonoSettings& settings)
	{
		const char* colors[]{ T("feature.neural_rendering.raw", "Provider appearance"), T("feature.neural_rendering.preserve", "Preserve source lighting"), T("feature.neural_rendering.neural_lighting", "Neural lighting") };
		int color = settings.colorMode == 0 ? 0 : settings.colorMode == 3 ? 2 :
		                                                                    1;
		if (Util::Widgets::Combo(T("feature.neural_rendering.color", "Color reconstruction"), &color, colors, 3))
			settings.colorMode = color == 0 ? 0u : color == 2 ? 3u :
			                                                    2u;
		Util::Widgets::SliderFloat(T("feature.neural_rendering.detail", "Detail strength"), &settings.detailStrength, 0.0f, 2.0f);
		Util::Widgets::SliderFloat(T("feature.neural_rendering.lighting_preservation", "Lighting preservation"), &settings.lightingPreservation, 0.0f, 1.0f);
		Util::Widgets::SliderFloat(T("feature.neural_rendering.linear_exposure", "Linear scene exposure calibration"), &settings.exposure, 0.125f, 8.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
		const char* exposures[]{ T("feature.neural_rendering.exposure_manual", "Manual calibration"), T("feature.neural_rendering.exposure_current", "Captured HDR: current source frame"), T("feature.neural_rendering.exposure_previous", "Captured HDR: previous source frame") };
		int exposure = static_cast<int>(settings.exposureSource);
		if (Util::Widgets::Combo(T("feature.neural_rendering.exposure_source", "Linear exposure source"), &exposure, exposures, 3))
			settings.exposureSource = static_cast<std::uint32_t>(exposure);
		if (settings.exposureSource != 0)
			ImGui::TextWrapped("%s", T("feature.neural_rendering.exposure_help", "Captured exposure is accepted only from the exact HDR adaptation pass and source frame. Missing, ambiguous, or invalid evidence keeps the original scene. Before-upscaling placement can use the previous source frame."));
	}

	void DrawSelection(MonoSettings& settings)
	{
		const MenuUI::Choice coverage[]{
			{ "scene", T("feature.neural_rendering.selection_scene", "Entire scene"), T("feature.neural_rendering.selection_scene_summary", "One enhancement across the scene"), T("feature.neural_rendering.selection_scene_help", "Enhance all pixels inside the chosen screen-space region.") },
			{ "strengths", T("feature.neural_rendering.selection_strengths", "Scene and materials"), T("feature.neural_rendering.selection_strengths_summary", "Adjust character material strengths"), T("feature.neural_rendering.selection_strengths_help", "Enhance the scene and use separate strengths for selected character materials.") },
			{ "actors", T("feature.neural_rendering.selection_actors", "Actors only"), T("feature.neural_rendering.selection_actors_summary", "Preserve the surrounding scene"), T("feature.neural_rendering.selection_actors_help", "Apply neural changes only to selected character materials.") }
		};
		const int coverageIndex = settings.charactersOnly ? 2 : settings.sceneCategoryStrengths ? 1 :
		                                                                                          0;
		const int choice = MenuUI::ChoiceCards("Coverage", coverageIndex, coverage);
		if (choice >= 0) {
			settings.charactersOnly = choice == 2;
			settings.sceneCategoryStrengths = choice == 1;
		}
		const auto guard = Util::DisableGuard(!settings.charactersOnly && !settings.sceneCategoryStrengths);
		MenuUI::SectionHeading(T("feature.neural_rendering.materials", "Materials and strengths"));
		const char* categories[]{ T("feature.neural_rendering.faces", "Faces"), T("feature.neural_rendering.skin", "Skin"), T("feature.neural_rendering.hair", "Hair"), T("feature.neural_rendering.armor", "Armor"), T("feature.neural_rendering.weapons", "Weapons") };
		for (std::uint32_t index = 0; index < 5; ++index) {
			ImGui::PushID(static_cast<int>(index));
			const SKSE::stl::scope_exit restoreId([] { ImGui::PopID(); });
			bool selected = (settings.categoryMask & (1u << (index + 1))) != 0;
			if (Util::Widgets::Checkbox(categories[index], &selected))
				settings.categoryMask = selected ? settings.categoryMask | (1u << (index + 1)) : settings.categoryMask & ~(1u << (index + 1));
			Util::Widgets::SliderFloat(T("feature.neural_rendering.category_strength", "Strength"), &settings.categoryStrengths[index], 0.0f, 1.0f);
		}
		const char* groups[]{ T("feature.neural_rendering.humans", "Humans"), T("feature.neural_rendering.humanoids", "Other humanoids"), T("feature.neural_rendering.creatures", "Creatures"), T("feature.neural_rendering.animals", "Animals"), T("feature.neural_rendering.other_actors", "Other actors") };
		for (std::uint32_t index = 0; index < 5; ++index) {
			bool selected = (settings.actorGroupMask & (1u << index)) != 0;
			if (Util::Widgets::Checkbox(groups[index], &selected))
				settings.actorGroupMask = selected ? settings.actorGroupMask | (1u << index) : settings.actorGroupMask & ~(1u << index);
		}
	}

	void DrawActors(MonoSettings& settings)
	{
		if (!settings.charactersOnly && !settings.sceneCategoryStrengths &&
			MenuUI::DetailNote(T("feature.neural_rendering.actor_selection_help", "Choose Actors only or Scene and materials in Selection to use these controls."), T("feature.neural_rendering.open_selection", "Open Selection")))
			MenuUI::SettingsPage::Select("NeuralRendering", "selection");
		const auto actorGuard = Util::DisableGuard(!settings.charactersOnly && !settings.sceneCategoryStrengths);
		Util::Widgets::SliderFloat(T("feature.neural_rendering.distance", "Maximum distance (meters; zero disables limit)"), &settings.maximumDistanceMeters, 0.0f, 30.0f);
		DrawUIntSlider(T("feature.neural_rendering.face_pixels", "Minimum projected face size (pixels)"), settings.minimumFacePixelSize, 1, 512);
		DrawUIntSlider(T("feature.neural_rendering.hold", "Selection hold (frames)"), settings.holdFrames, 0, 30);
		Util::Widgets::Checkbox(T("feature.neural_rendering.adaptive_actors", "Select actors by perceptual detail"), &settings.adaptiveActorSelection);
		Util::Widgets::Checkbox(T("feature.neural_rendering.actor_focus", "Fade whole actors outside screen focus"), &settings.actorFocus);
		Util::Widgets::SliderFloat(T("feature.neural_rendering.focus", "Screen focus size"), &settings.focusScale, 0.25f, 1.0f);

		MenuUI::SectionHeading(T("feature.neural_rendering.actor_processing_area", "Actor processing area"));
		const auto cropGuard = Util::DisableGuard(!settings.charactersOnly);
		const MenuUI::Choice crops[]{
			{ "full", T("feature.neural_rendering.crop_full", "Full image"), T("feature.neural_rendering.crop_full_summary", "Keep the full inference image"), T("feature.neural_rendering.crop_full_help", "Run inference over the source region, then apply only the selected actor pixels.") },
			{ "bounds", T("feature.neural_rendering.crop_bounds", "Actor bounds"), T("feature.neural_rendering.crop_bounds_summary", "Crop around selected actors"), T("feature.neural_rendering.crop_bounds_help", "Reduce the inference image to the combined selected character bounds when valid.") },
			{ "packed", T("feature.neural_rendering.crop_packed", "Packed actors"), T("feature.neural_rendering.crop_packed_summary", "Pack separated character regions"), T("feature.neural_rendering.crop_packed_help", "Pack up to four regions only when ownership, coverage and useful area savings are proven. Otherwise retain the uniform path.") }
		};
		const int cropIndex = !settings.cropToCharacters ? 0 : settings.packedCharacters ? 2 :
		                                                                                   1;
		const int crop = MenuUI::ChoiceCards("ActorRegions", cropIndex, crops);
		if (crop >= 0) {
			settings.cropToCharacters = crop != 0;
			settings.packedCharacters = crop == 2;
		}
	}

	void DrawDiagnostics(MonoSettings& settings)
	{
		const auto snapshot = NeuralRendering::MonoRenderer::Instance().Snapshot();
		ImGui::Text(T("feature.neural_rendering.status", "Status: %s"), snapshot.status.c_str());
		if (!snapshot.detail.empty())
			ImGui::TextWrapped("%s", snapshot.detail.c_str());
		ImGui::Text(T("feature.neural_rendering.dimensions", "Inference: %u x %u (%u%%)"), snapshot.modelWidth, snapshot.modelHeight, snapshot.effectivePercent);
		ImGui::Text(T("feature.neural_rendering.prewarm_status", "Prewarm: %s"), snapshot.prewarmStatus.c_str());
		ImGui::Text(T("feature.neural_rendering.recovery_status", "Queue recovery attempts: %u; completed: %llu"), snapshot.stallAttempts, static_cast<unsigned long long>(snapshot.stallRecoveries));
		ImGui::Text(T("feature.neural_rendering.compression_status", "Compression: %s"), snapshot.compressionStatus.c_str());
		ImGui::Text(T("feature.neural_rendering.packed_status", "Packed regions: %u; coverage fallbacks: %llu"), snapshot.packedRegions, static_cast<unsigned long long>(snapshot.packedCoverageRejected));

		MenuUI::SectionHeading(T("feature.neural_rendering.exposure_diagnostics", "Exposure diagnostics"));
		Util::Widgets::Checkbox(T("feature.neural_rendering.capture_exposure", "Capture HDR exposure evidence"), &settings.captureExposure);
		const auto capture = NeuralRendering::Color::ExposureCapture::Instance().GetStatus();
		ImGui::Text(T("feature.neural_rendering.capture_status", "HDR producers: %u; captured: %llu; rejected: %llu; readbacks dropped: %llu"),
			capture.producersRegistered, static_cast<unsigned long long>(capture.captures), static_cast<unsigned long long>(capture.rejected), static_cast<unsigned long long>(capture.dropped));
		if (!capture.lastReason.empty())
			ImGui::TextWrapped("%s", capture.lastReason.c_str());
		const NeuralRendering::Color::ExposureEvidence* latest = nullptr;
		for (const auto& sample : capture.samples)
			if (sample.readbackComplete && (!latest || sample.stamp.sequence > latest->stamp.sequence))
				latest = &sample;
		if (latest) {
			ImGui::Text(T("feature.neural_rendering.capture_value", "Source frame %u: average %.6f, target %.6f, ratio %.6f, validity %.0f"), latest->stamp.frame, latest->values[0], latest->values[1], latest->values[2], latest->values[3]);
			if (latest->gammaKnown)
				ImGui::Text(T("feature.neural_rendering.capture_gamma", "Captured gamma exponent: %.6f"), latest->frameGammaExponent);
		}
	}
}

Feature::SettingsHeaderStatus NeuralRenderingFeature::GetSettingsHeaderStatus() const
{
	const auto& upscaling = globals::features::upscaling;
	if (!NeuralRendering::Runtime::IsInstalled())
		return { T("feature.neural_rendering.provider_missing_status", "Provider missing"), true };
	if (!upscaling.fidelityFX.IsNvidiaAdapterDetected())
		return { T("feature.neural_rendering.unsupported_status", "Unsupported device"), true };
	if (upscaling.IsFrameGenerationConfigured() || upscaling.IsFrameGenerationDx12PathActive())
		return { T("feature.neural_rendering.fg_status", "Frame generation owns presentation"), true };
	if (!upscaling.neuralSettings.enabled)
		return { T("feature.neural_rendering.off_status", "Disabled"), false };
	return { upscaling.IsNeuralRenderingRequested() ? T("feature.neural_rendering.requested_status", "Requested") :
													  T("feature.neural_rendering.waiting_status", "Waiting for an eligible scene"),
		false };
}

void NeuralRenderingFeature::DrawSettingsEnabledControl()
{
	static MenuUI::ActionFeedback feedback;
	auto& upscaling = globals::features::upscaling;
	const auto* blocker = upscaling.GetNeuralRenderingMutationBlockReason();
	{
		const auto guard = Util::DisableGuard(blocker != nullptr);
		bool enabled = upscaling.neuralSettings.enabled;
		if (Util::Widgets::Checkbox(T("menu.features.enabled", "Enabled"), &enabled)) {
			std::string error;
			if (upscaling.ToggleNeuralRendering(&error))
				feedback = {};
			else
				feedback = { std::move(error), true };
		}
	}
	if (blocker)
		Util::AddTooltip(blocker, ImGuiHoveredFlags_AllowWhenDisabled);
	feedback.Draw();
}

void NeuralRenderingFeature::DrawSettings()
{
	static MenuUI::ActionFeedback feedback;
	auto& upscaling = globals::features::upscaling;
	auto settings = upscaling.neuralSettings;
	if (!NeuralRendering::Runtime::IsInstalled())
		MenuUI::DetailNote(T("feature.neural_rendering.missing", "Install the optional compatible nvngx_dlssnr.dll in Data/Shaders/Upscaling/Streamline, then restart the game."));
	if (!upscaling.fidelityFX.IsNvidiaAdapterDetected())
		MenuUI::DetailNote(T("feature.neural_rendering.hardware", "Neural rendering requires a supported NVIDIA graphics device."));
	if (upscaling.IsFrameGenerationConfigured() || upscaling.IsFrameGenerationDx12PathActive())
		MenuUI::DetailNote(T("feature.neural_rendering.frame_generation_gate", "Neural rendering is suspended while frame generation is configured or its DX12 presentation path is active. Disable frame generation and restart to use neural rendering."));
	const auto* blocker = upscaling.GetNeuralRenderingMutationBlockReason();
	if (blocker)
		MenuUI::DetailNote(blocker);
	MenuUI::SettingsPage page("NeuralRendering", {
													 { "mode", T("feature.neural_rendering.page.mode", "Mode"), T("feature.neural_rendering.page.mode_help", "Choose where neural rendering runs and its inference resolution."), settings.mode == 2 ? T("feature.neural_rendering.before_upscaling", "Scene before upscaling") : T("feature.neural_rendering.final_scene", "Final scene before UI"), true, true, T("feature.neural_rendering.page.foundation", "Set the foundation") },
													 { "blending", T("feature.neural_rendering.page.blending", "Blending"), T("feature.neural_rendering.page.blending_help", "Choose how neural changes are reconstructed into the source."), T("feature.neural_rendering.page.blending_summary", "Source detail and reconstruction") },
													 { "look", T("feature.neural_rendering.page.look", "Look"), T("feature.neural_rendering.page.look_help", "Refine the shared style, intensity and structure."), T("feature.neural_rendering.page.look_summary", "Neural appearance"), true, true, T("feature.neural_rendering.page.picture", "Shape the shared picture") },
													 { "colour", T("feature.neural_rendering.page.colour", "Colour"), T("feature.neural_rendering.page.colour_help", "Refine lighting preservation and exposure."), T("feature.neural_rendering.page.colour_summary", "Colour and lighting") },
													 { "selection", T("feature.neural_rendering.page.selection", "Selection"), T("feature.neural_rendering.page.selection_help", "Choose the scene, actor types and material strengths."), T("feature.neural_rendering.page.selection_summary", "Where the enhancement appears"), true, true, T("feature.neural_rendering.page.coverage", "Refine where it is applied") },
													 { "actors", T("feature.neural_rendering.page.actors", "Actors"), T("feature.neural_rendering.page.actors_help", "Refine distance, focus and character-region processing."), T("feature.neural_rendering.page.actors_summary", "Actor coverage and regions"), true, true, nullptr, nullptr, nullptr, settings.charactersOnly || settings.sceneCategoryStrengths },
													 { "diagnostics", T("feature.neural_rendering.page.diagnostics", "Diagnostics"), T("feature.neural_rendering.page.diagnostics_help", "Inspect actual provider, reconstruction and exposure state."), T("feature.neural_rendering.page.diagnostics_summary", "Runtime evidence"), true, false },
												 },
		T("feature.neural_rendering.page.title", "Your NR setup"), T("feature.neural_rendering.page.guidance", "Choose a mode, then refine the picture."));
	{
		const auto guard = Util::DisableGuard(blocker != nullptr);
		if (page.Is("mode"))
			DrawMode(settings, upscaling);
		if (page.Is("blending"))
			DrawBlending(settings);
		if (page.Is("look"))
			DrawLook(settings);
		if (page.Is("colour"))
			DrawColour(settings);
		if (page.Is("selection"))
			DrawSelection(settings);
		if (page.Is("actors"))
			DrawActors(settings);
		if (page.Is("diagnostics"))
			DrawDiagnostics(settings);
	}
	NeuralRendering::Sanitize(settings);
	if (settings != upscaling.neuralSettings) {
		if (const auto* reason = upscaling.GetNeuralRenderingMutationBlockReason())
			feedback = { reason, true };
		else if (!globals::menu->PrepareSettingsMutation())
			feedback = { T("feature.neural_rendering.toggle_baseline", "Could not preserve the settings baseline. Neural rendering was not changed."), true };
		else {
			upscaling.neuralSettings = settings;
			upscaling.RequestHistoryReset();
			globals::menu->RequestSettingsDirtyCheck();
			feedback = {};
		}
	}
	feedback.Draw();
}
