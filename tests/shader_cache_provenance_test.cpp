#include "Utils/CacheInvalidation.h"

#include <cassert>
#include <filesystem>
#include <fstream>

int main()
{
	using namespace Util::CacheInvalidation;
	const std::vector<FeatureState> features;
	const std::map<std::string, CacheIniEntry> entries;

	const auto matching = ClassifyMismatches(
		"CSX 3.19-VR", std::string("CSX 3.19-VR"),
		"abi-current", std::string("abi-current"),
		"compiler-current", std::string("compiler-current"), features, entries);
	assert(matching.empty());

	// Release labels are evidence only. They must not discard bytecode whose
	// source closure and explicit cache contracts still match.
	const auto releaseOnly = ClassifyMismatches(
		"CSX 3.20-VR", std::string("CSX 3.19-VR"),
		"abi-current", std::string("abi-current"),
		"compiler-current", std::string("compiler-current"), features, entries);
	assert(releaseOnly.empty());

	const std::vector<FeatureState> absentDisabledFeature{
		{ "OptionalFeature", "Optional Feature", false, "1.0.0", "OPTIONAL_FEATURE", "" }
	};
	const auto absentDisabled = ClassifyMismatches(
		"CSX 3.20-VR", std::string("CSX 3.19-VR"),
		"abi-current", std::string("abi-current"),
		"compiler-current", std::string("compiler-current"), absentDisabledFeature, {});
	assert(absentDisabled.empty());

	const auto missingAbi = ClassifyMismatches(
		"CSX 3.19-VR", std::string("CSX 3.19-VR"),
		"abi-current", std::nullopt,
		"compiler-current", std::nullopt, features, entries);
	assert(missingAbi.size() == 1);
	assert(missingAbi.front().kind == CacheMismatch::Kind::ShaderAbi);

	// A precompiled cache has no local runtime-compiler identity. Its bytecode
	// remains valid when the player's d3dcompiler differs from the build host.
	const auto precompiled = ClassifyMismatches(
		"CSX 3.19-VR", std::string("CSX 3.19-VR"),
		"abi-current", std::string("abi-current"),
		"compiler-current", std::nullopt, features, entries);
	assert(precompiled.empty());

	const auto changedCompiler = ClassifyMismatches(
		"CSX 3.19-VR", std::string("CSX 3.19-VR"),
		"abi-current", std::string("abi-current"),
		"compiler-current", std::string("compiler-old"), features, entries);
	assert(changedCompiler.size() == 1);
	assert(changedCompiler.front().kind == CacheMismatch::Kind::ShaderCompiler);

	const std::vector<FeatureState> versionOnlyFeature{
		{ "WaterEffects", "Water Effects", true, "2.0.0", "WATER_EFFECTS", "" }
	};
	const std::map<std::string, CacheIniEntry> versionOnlyEntries{
		{ "WaterEffects", { true, std::string("1.0.0"), std::nullopt } }
	};
	const auto versionOnly = ClassifyMismatches(
		"CSX 3.20-VR", std::string("CSX 3.19-VR"),
		"abi-current", std::string("abi-current"),
		"compiler-current", std::string("compiler-current"),
		versionOnlyFeature, versionOnlyEntries);
	assert(versionOnly.empty());

	const std::vector<FeatureState> scopedAbiFeature{
		{ "WaterEffects", "Water Effects", true, "2.0.0", "WATER_EFFECTS", "water-bindings-v2" }
	};
	const std::map<std::string, CacheIniEntry> scopedAbiEntries{
		{ "WaterEffects", { true, std::string("1.0.0"), std::string("water-bindings-v1") } }
	};
	const auto changedScopedAbi = ClassifyMismatches(
		"CSX 3.20-VR", std::string("CSX 3.19-VR"),
		"abi-current", std::string("abi-current"),
		"compiler-current", std::string("compiler-current"),
		scopedAbiFeature, scopedAbiEntries);
	assert(changedScopedAbi.size() == 1);
	assert(changedScopedAbi.front().kind == CacheMismatch::Kind::FeatureShaderAbi);

	const auto testRoot = std::filesystem::temp_directory_path() / "csx-selective-cache-invalidation-test";
	std::filesystem::remove_all(testRoot);
	const auto cacheRoot = testRoot / "ShaderCache";
	const auto shaderRoot = testRoot / "Shaders";
	std::filesystem::create_directories(cacheRoot / "Water");
	std::filesystem::create_directories(cacheRoot / "Lighting");
	std::filesystem::create_directories(cacheRoot / "VolumetricLighting");
	std::filesystem::create_directories(shaderRoot);
	{
		std::ofstream(shaderRoot / "Water.hlsl") << "#if defined(UNIFIED_WATER)\n#endif\n";
		std::ofstream(shaderRoot / "Lighting.hlsl") << "float4 main() : SV_Target { return 1; }\n";
		std::ofstream(cacheRoot / "Water" / "1.pso") << "water";
		std::ofstream(cacheRoot / "Lighting" / "1.pso") << "lighting";
		// Runtime ImageSpace technique directory. Its source is remapped from an
		// IS*.hlsl family by the packaged-cache builder, so same-name lookup is
		// intentionally unavailable and must conservatively rebuild it.
		std::ofstream(cacheRoot / "VolumetricLighting" / "1.pso") << "imagespace";
	}

	const auto waterPlan = PlanCacheFamilies(cacheRoot, shaderRoot, { "UNIFIED_WATER" });
	assert(waterPlan.has_value());
	assert(waterPlan->affected.size() == 2);
	assert(waterPlan->affected.front().filename() == "VolumetricLighting");
	assert(waterPlan->affected.back().filename() == "Water");
	assert(waterPlan->retained.size() == 1);
	assert(waterPlan->retained.front().filename() == "Lighting");
	assert(waterPlan->unclassified.size() == 1);
	assert(waterPlan->unclassified.front().filename() == "VolumetricLighting");

	size_t deleted = 0;
	size_t kept = 0;
	(void)deleted;
	(void)kept;
	assert(TryPartialInvalidation(cacheRoot, shaderRoot, { "UNIFIED_WATER" }, &deleted, &kept));
	assert(deleted == 2);
	assert(kept == 1);
	assert(!std::filesystem::exists(cacheRoot / "Water"));
	assert(!std::filesystem::exists(cacheRoot / "VolumetricLighting"));
	assert(std::filesystem::exists(cacheRoot / "Lighting" / "1.pso"));
	std::filesystem::remove_all(testRoot);
	return 0;
}
