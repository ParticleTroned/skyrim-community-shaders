#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace Util::FeatureProfiling
{
	/** Feature-owned scopes and shared engine stages have separate attribution. */
	struct View
	{
		std::string_view feature;
		std::string_view ownedRoot;
		std::span<const std::string_view> sharedPrefixes{};
		bool ownedGpu = true;
		const char* coverage = "Timings cover the instrumented passes on this page.";
		std::span<const std::string_view> ownedPrefixes{};  // Additional owned passes outside ownedRoot.
		const char* ownedCoverage = nullptr;

		/** CPU-only update scopes never become feature-owned GPU timings. */
		bool HasOwnedTimings(bool cpuMode) const { return (!ownedRoot.empty() || !ownedPrefixes.empty()) && (cpuMode || ownedGpu); }

		/** Coverage of owned scopes when shared engine stages are excluded. */
		const char* OwnedCoverage() const
		{
			if (!HasOwnedTimings(true))
				return "No separately instrumented scopes. Use an on/off comparison to isolate this feature.";
			if (!ownedGpu)
				return "CPU update scopes only. Shared GPU stages are excluded.";
			return ownedCoverage ? ownedCoverage : coverage;
		}
	};

	inline constexpr auto materialPasses = std::to_array<std::string_view>({ "SharedScene::World", "DeferredComposite" });
	inline constexpr auto waterPasses = std::to_array<std::string_view>({ "SharedScene::World", "Water::RenderWaterEffects" });
	inline constexpr auto shadowPasses = std::to_array<std::string_view>({ "SharedScene::DirectionalShadows" });
	inline constexpr auto vrPasses = std::to_array<std::string_view>({ "ScreenSpaceShadows", "ScreenSpaceGI", "DynamicCubemaps", "DeferredComposite",
		"StereoBlend", "Upscaling::FoveatedMaskVisualization" });
	inline constexpr auto neuralRenderingPasses = std::to_array<std::string_view>({ "Upscaling::DLSSNeuralRendering", "Upscaling::DLSSNeuralRenderingStereo", "Upscaling::DLSSNeuralRenderingSequentialStereo",
		"Upscaling::DLSSNRDepthGuide", "Upscaling::NRColorPrepare", "Upscaling::NRColorReconstruct", "Upscaling::NRColorExposureCapture",
		"Upscaling::DLSS5EarlyCharacterMaskBounds", "Upscaling::DLSS5CharacterMask", "Upscaling::DLSS5CharacterCategoryCapture",
		"Upscaling::DLSS5CharacterRoiSetup", "Upscaling::DLSS5CharacterComposite", "Upscaling::NeuralFinalLdrPreUi" });
	inline constexpr const char* sharedCoverage =
		"Shared pass self times include vanilla rendering and other effects. They are not a feature-cost subtotal. Use on/off comparison for this feature's added cost.";
	inline constexpr const char* materialCoverage =
		"Runs in shared shaders. World/deferred timings show rendering context, not this feature's isolated cost.";

	inline constexpr auto views = std::to_array<View>({ { "DynamicCubemaps", "DynamicCubemaps" },
		{ "GrassCollision", "GrassCollision" },
		{ "GrassOptimizations", "GrassOptimizations" },
		{ "ImageBasedLighting", "IBL" },
		{ "LightLimitFix", "LightLimitFix" },
		{ "NeuralRendering", "NeuralRendering", {}, true, "Instrumented NR evaluation, colour processing and actor preparation. Subtotal excludes ordinary upscaling.", neuralRenderingPasses, "Instrumented D3D11 neural-rendering passes and CPU preparation only. Evaluation on other GPU queues is excluded." },
		{ "ScreenSpaceGI", "ScreenSpaceGI" },
		{ "ScreenSpaceShadows", "ScreenSpaceShadows" },
		{ "Skylighting", "Skylighting" },
		{ "SubsurfaceScattering", "SubsurfaceScattering" },
		{ "TerrainBlending", "TerrainBlending" },
		{ "TerrainShadows", "TerrainShadows" },
		{ "AdaptiveBrightness", "UnderwaterDepthOfField", {}, true, "DOF correction only. Times underwater fog blur correction; other Adaptive Balance adjustments have no separate timers." },
		{ "Upscaling", "Upscaling" },
		{ "VolumetricLighting", "VolumetricLighting" },
		{ "VolumetricShadows", "VolumetricShadows" },
		{ "VR", "VR", vrPasses, true, "Partial VR timings: stereo processing and shared passes affected by stereo/foveation. Not total VR cost or isolated foveation cost.", {}, "Partial stereo processing only. Shared stages and the isolated cost of foveation are excluded." },
		{ "CloudShadows", "CloudShadows", {}, true, "Times cubemap clears/copies only. Shared cloud rendering and shadow application are excluded from the subtotal." },
		{ "InteriorSun", "InteriorSun", shadowPasses, false, "CPU: shadow-caster selection/job preparation. GPU: shared directional-shadow rendering." },
		{ "Wetterness", "Wetterness", materialPasses, false, "CPU: weather, wetness and puddle updates. GPU: shared rendering stages that include wetness shaders." },
		{ "TruePBR", "", materialPasses, false, materialCoverage },
		{ "ExtendedMaterials", "", materialPasses, false, materialCoverage },
		{ "TerrainVariation", "", materialPasses, false, materialCoverage },
		{ "ExtendedTranslucency", "", materialPasses, false, materialCoverage },
		{ "FoliageLighting", "", materialPasses, false, materialCoverage },
		{ "GrassLighting", "", materialPasses, false, materialCoverage },
		{ "HairSpecular", "", materialPasses, false, materialCoverage },
		{ "WaterEffects", "", waterPasses, false, "Water effects run inside shared shaders. The world and water-effects stages include vanilla rendering and other effects." } });

	/** These utility features do not expose profiling controls, even with retained samples. */
	inline bool IsExcluded(std::string_view feature)
	{
		return feature == "Screenshot" || feature == "TextureStreaming";
	}

	/** Returns stable coverage even before a scene has produced timing samples. */
	inline const View* Find(std::string_view feature)
	{
		for (const auto& view : views)
			if (view.feature == feature)
				return &view;
		return nullptr;
	}

	/** Matches a timer or namespace without accepting a similarly named feature. */
	inline bool Matches(std::string_view timer, std::string_view prefix)
	{
		return !prefix.empty() && (timer == prefix ||
									  (timer.starts_with(prefix) && timer.substr(prefix.size()).starts_with("::")));
	}
	/** Specific aliases take precedence over a broader enclosing namespace. */
	inline std::size_t OwnedPrefixLength(const View& view, std::string_view timer)
	{
		std::size_t length = Matches(timer, view.ownedRoot) ? view.ownedRoot.size() : 0;
		for (const auto prefix : view.ownedPrefixes) {
			if (Matches(timer, prefix))
				length = std::max(length, prefix.size());
		}
		return length;
	}

	/** Match owned passes without attributing unrelated work from their enclosing feature. */
	inline bool MatchesOwned(const View& view, std::string_view timer)
	{
		return OwnedPrefixLength(view, timer) != 0;
	}

	/** Attribute each pass once; ambiguous equal ownership remains unattributed. */
	inline bool MatchesExclusiveOwned(const View& view, std::string_view timer)
	{
		const auto length = OwnedPrefixLength(view, timer);
		return length && std::ranges::none_of(views, [&](const auto& other) {
			return other.feature != view.feature && OwnedPrefixLength(other, timer) >= length;
		});
	}

}
