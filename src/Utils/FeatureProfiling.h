#pragma once

#include <array>
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

		/** CPU-only update scopes never become feature-owned GPU timings. */
		bool HasOwnedTimings(bool cpuMode) const { return !ownedRoot.empty() && (cpuMode || ownedGpu); }
	};

	inline constexpr auto materialPasses = std::to_array<std::string_view>({ "SharedScene::World", "DeferredComposite" });
	inline constexpr auto waterPasses = std::to_array<std::string_view>({ "SharedScene::World", "Water::RenderWaterEffects" });
	inline constexpr auto shadowPasses = std::to_array<std::string_view>({ "SharedScene::DirectionalShadows" });
	inline constexpr auto vrPasses = std::to_array<std::string_view>({ "ScreenSpaceShadows", "ScreenSpaceGI", "DynamicCubemaps", "DeferredComposite",
		"StereoBlend", "Upscaling::FoveatedMaskVisualization" });
	inline constexpr const char* sharedCoverage =
		"These shared pass self times include vanilla rendering and other effects. They are not summed as a feature cost. Use an on/off comparison to isolate this feature's additional cost.";
	inline constexpr const char* materialCoverage =
		"This feature runs inside shared shaders. The world and deferred stages provide broad rendering context, not a separate feature cost.";

	inline constexpr auto views = std::to_array<View>({ { "DynamicCubemaps", "DynamicCubemaps" },
		{ "GrassCollision", "GrassCollision" },
		{ "GrassOptimizations", "GrassOptimizations" },
		{ "ImageBasedLighting", "IBL" },
		{ "LightLimitFix", "LightLimitFix" },
		{ "NeuralRendering", "NeuralRendering" },
		{ "ScreenSpaceGI", "ScreenSpaceGI" },
		{ "ScreenSpaceShadows", "ScreenSpaceShadows" },
		{ "Skylighting", "Skylighting" },
		{ "SubsurfaceScattering", "SubsurfaceScattering" },
		{ "TerrainBlending", "TerrainBlending" },
		{ "TerrainShadows", "TerrainShadows" },
		{ "CSUtility", "UnderwaterDepthOfField", {}, true, "Measures the underwater fog correction before depth of field. Other utility settings are not separately timed." },
		{ "Upscaling", "Upscaling" },
		{ "VolumetricLighting", "VolumetricLighting" },
		{ "VolumetricShadows", "VolumetricShadows" },
		{ "VR", "VR", vrPasses, true, "Partial VR coverage: stereo processing and shared passes affected by stereo and foveation settings. This is not the total VR cost or the isolated cost of foveation." },
		{ "CloudShadows", "CloudShadows", {}, true, "Measures cubemap clears and copies only. Cloud rendering and shadow application run in shared passes and are not included in this subtotal." },
		{ "InteriorSun", "InteriorSun", shadowPasses, false, "CPU timings cover shadow-caster selection and job preparation. GPU timings show the shared directional-shadow rendering stage." },
		{ "Wetterness", "Wetterness", materialPasses, false, "CPU timings cover weather, wetness and puddle-state updates. GPU timings show broad shared rendering stages containing the wetness shaders." },
		{ "TruePBR", "", materialPasses, false, materialCoverage },
		{ "ExtendedMaterials", "", materialPasses, false, materialCoverage },
		{ "TerrainVariation", "", materialPasses, false, materialCoverage },
		{ "ExtendedTranslucency", "", materialPasses, false, materialCoverage },
		{ "FoliageLighting", "", materialPasses, false, materialCoverage },
		{ "GrassLighting", "", materialPasses, false, materialCoverage },
		{ "HairSpecular", "", materialPasses, false, materialCoverage },
		{ "WaterEffects", "", waterPasses, false, "Water effects run inside shared shaders. The world and water-effects stages include vanilla rendering and other effects." } });

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
}
