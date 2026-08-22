#include "Utils/CacheInvalidation.h"

#include <cassert>

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
	return 0;
}
