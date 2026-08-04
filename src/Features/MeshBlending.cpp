#include "MeshBlending.h"

#include "../Globals.h"
#include "../ShaderCache.h"
#include "../State.h"
#include "../Util.h"
#include "../Utils/FileSystem.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MeshBlending::OverrideRule,
	Model,
	NodePath);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MeshBlending::Settings,
	Enabled,
	BlendStrength,
	BlendWidth,
	DepthBias,
	MaximumGap,
	DetectionMode,
	MaximumDistance,
	BoundsExpansion,
	RequireOverlappingBounds,
	DeveloperLogging);

namespace
{
	using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;
	using MaterialFeature = RE::BSShaderMaterial::Feature;

	constexpr std::uint32_t kRuleFileSchemaVersion = 1u;

	std::filesystem::path GetRuleFilePath()
	{
		return Util::PathHelpers::GetCommunityShaderPath() / "MeshBlendingRules.json";
	}

	constexpr std::uint64_t ToMask(ShaderFlag a_flag)
	{
		return static_cast<std::uint64_t>(a_flag);
	}

	constexpr std::uint64_t kExcludedShaderFlags =
		ToMask(ShaderFlag::kSkinned) |
		ToMask(ShaderFlag::kTempRefraction) |
		ToMask(ShaderFlag::kFace) |
		ToMask(ShaderFlag::kMultiTextureLandscape) |
		ToMask(ShaderFlag::kRefraction) |
		ToMask(ShaderFlag::kRefractionFalloff) |
		ToMask(ShaderFlag::kEyeReflect) |
		ToMask(ShaderFlag::kHairTint) |
		ToMask(ShaderFlag::kScreendoorAlphaFade) |
		ToMask(ShaderFlag::kFaceGenRGBTint) |
		ToMask(ShaderFlag::kEnvMap) |
		ToMask(ShaderFlag::kProjectedUV) |
		ToMask(ShaderFlag::kDecal) |
		ToMask(ShaderFlag::kDynamicDecal) |
		ToMask(ShaderFlag::kSoftEffect) |
		ToMask(ShaderFlag::kLODLandscape) |
		ToMask(ShaderFlag::kLODObjects) |
		ToMask(ShaderFlag::kCharacterLighting) |
		ToMask(ShaderFlag::kMultiIndexSnow) |
		ToMask(ShaderFlag::kBillboard) |
		ToMask(ShaderFlag::kWeaponBlood) |
		ToMask(ShaderFlag::kPremultAlpha) |
		ToMask(ShaderFlag::kCloudLOD) |
		ToMask(ShaderFlag::kMenuScreen) |
		ToMask(ShaderFlag::kSoftLighting) |
		ToMask(ShaderFlag::kRimLighting) |
		ToMask(ShaderFlag::kBackLighting) |
		ToMask(ShaderFlag::kSnow) |
		ToMask(ShaderFlag::kTreeAnim) |
		ToMask(ShaderFlag::kEffectLighting) |
		ToMask(ShaderFlag::kHDLODObjects);

	bool IsFinite(float a_value)
	{
		return std::isfinite(a_value);
	}

	bool IsFiniteBound(const RE::NiBound& a_bound)
	{
		return IsFinite(a_bound.center.x) &&
		       IsFinite(a_bound.center.y) &&
		       IsFinite(a_bound.center.z) &&
		       IsFinite(a_bound.radius) &&
		       a_bound.radius > 0.0f;
	}

	bool IsSafeMaterialFeature(MaterialFeature a_feature)
	{
		switch (a_feature) {
		case MaterialFeature::kDefault:
		case MaterialFeature::kParallax:
		case MaterialFeature::kParallaxOcc:
		case MaterialFeature::kMultilayerParallax:
			return true;
		default:
			return false;
		}
	}

	bool HasWildcard(std::string_view a_pattern)
	{
		return a_pattern.find_first_of("*?") != std::string_view::npos;
	}

	bool IsValidUtf8(std::string_view a_value)
	{
		auto byteAt = [&](std::size_t a_index) {
			return static_cast<std::uint8_t>(a_value[a_index]);
		};
		auto isContinuationInRange = [&](std::size_t a_index, std::uint8_t a_minimum, std::uint8_t a_maximum) {
			return a_index < a_value.size() && byteAt(a_index) >= a_minimum && byteAt(a_index) <= a_maximum;
		};
		auto isContinuation = [&](std::size_t a_index) {
			return isContinuationInRange(a_index, 0x80u, 0xBFu);
		};

		for (std::size_t index = 0u; index < a_value.size();) {
			const auto lead = byteAt(index);
			if (lead <= 0x7Fu) {
				++index;
			} else if (lead >= 0xC2u && lead <= 0xDFu && isContinuation(index + 1u)) {
				index += 2u;
			} else if (lead == 0xE0u && isContinuationInRange(index + 1u, 0xA0u, 0xBFu) && isContinuation(index + 2u)) {
				index += 3u;
			} else if (((lead >= 0xE1u && lead <= 0xECu) || (lead >= 0xEEu && lead <= 0xEFu)) &&
					   isContinuation(index + 1u) && isContinuation(index + 2u)) {
				index += 3u;
			} else if (lead == 0xEDu && isContinuationInRange(index + 1u, 0x80u, 0x9Fu) && isContinuation(index + 2u)) {
				index += 3u;
			} else if (lead == 0xF0u && isContinuationInRange(index + 1u, 0x90u, 0xBFu) &&
					   isContinuation(index + 2u) && isContinuation(index + 3u)) {
				index += 4u;
			} else if (lead >= 0xF1u && lead <= 0xF3u && isContinuation(index + 1u) &&
					   isContinuation(index + 2u) && isContinuation(index + 3u)) {
				index += 4u;
			} else if (lead == 0xF4u && isContinuationInRange(index + 1u, 0x80u, 0x8Fu) &&
					   isContinuation(index + 2u) && isContinuation(index + 3u)) {
				index += 4u;
			} else {
				return false;
			}
		}
		return true;
	}

	bool WildcardMatch(std::string_view a_pattern, std::string_view a_value)
	{
		std::size_t patternIndex = 0u;
		std::size_t valueIndex = 0u;
		std::size_t starIndex = std::string_view::npos;
		std::size_t retryValueIndex = 0u;

		while (valueIndex < a_value.size()) {
			if (patternIndex < a_pattern.size() &&
				(a_pattern[patternIndex] == '?' || a_pattern[patternIndex] == a_value[valueIndex])) {
				++patternIndex;
				++valueIndex;
			} else if (patternIndex < a_pattern.size() && a_pattern[patternIndex] == '*') {
				starIndex = patternIndex++;
				retryValueIndex = valueIndex;
			} else if (starIndex != std::string_view::npos) {
				patternIndex = starIndex + 1u;
				valueIndex = ++retryValueIndex;
			} else {
				return false;
			}
		}

		while (patternIndex < a_pattern.size() && a_pattern[patternIndex] == '*') {
			++patternIndex;
		}
		return patternIndex == a_pattern.size();
	}

	std::string NormalizePath(std::string_view a_path, bool a_isModelPath)
	{
		std::string normalized;
		normalized.reserve(a_path.size() + (a_isModelPath ? 7u : 0u));
		bool previousSlash = false;
		for (char value : a_path) {
			char character = value == '\\' ? '/' : value;
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
			if (character == '/') {
				if (previousSlash) {
					continue;
				}
				previousSlash = true;
			} else {
				previousSlash = false;
			}
			normalized.push_back(character);
		}

		while (normalized.starts_with("./")) {
			normalized.erase(0u, 2u);
		}
		while (!normalized.empty() && normalized.front() == '/') {
			normalized.erase(normalized.begin());
		}
		if (a_isModelPath) {
			if (normalized.starts_with("data/")) {
				normalized.erase(0u, 5u);
			}
			if (!normalized.empty() && !normalized.starts_with("meshes/")) {
				normalized.insert(0u, "meshes/");
			}
		}
		while (!normalized.empty() && normalized.back() == '/') {
			normalized.pop_back();
		}
		return normalized;
	}

	const char* DetectionModeName(std::uint32_t a_mode)
	{
		switch (static_cast<MeshBlending::DetectionMode>(a_mode)) {
		case MeshBlending::DetectionMode::kDisabled:
			return "Disabled";
		case MeshBlending::DetectionMode::kAllowListOnly:
			return "Allow list only";
		case MeshBlending::DetectionMode::kStrictAutomatic:
			return "Strict automatic";
		default:
			return "Invalid";
		}
	}
}

std::pair<std::string, std::vector<std::string>> MeshBlending::GetFeatureSummary()
{
	return {
		"Fades eligible translucent static NIF shapes into a plausible opaque sibling using the existing scene depth.",
		{ "No additional render pass or geometry",
			"Conservative static-object classifier with deny-first overrides",
			"Bounded cache, traversal limits, and a configurable player-distance bubble",
			"Flat-screen and VR eye-correct depth sampling" }
	};
}

void MeshBlending::SanitizeSettings()
{
	settings.Enabled = settings.Enabled ? 1u : 0u;
	settings.RequireOverlappingBounds = settings.RequireOverlappingBounds ? 1u : 0u;
	settings.DeveloperLogging = settings.DeveloperLogging ? 1u : 0u;
	if (settings.DetectionMode > static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic)) {
		settings.DetectionMode = static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic);
	}

	settings.BlendStrength = IsFinite(settings.BlendStrength) ? std::clamp(settings.BlendStrength, 0.0f, 1.0f) : 1.0f;
	settings.BlendWidth = IsFinite(settings.BlendWidth) ? std::clamp(settings.BlendWidth, 0.01f, 256.0f) : 12.0f;
	settings.DepthBias = IsFinite(settings.DepthBias) ? std::clamp(settings.DepthBias, 0.0f, 128.0f) : 0.25f;
	settings.MaximumGap = IsFinite(settings.MaximumGap) ? std::clamp(settings.MaximumGap, 0.01f, 4096.0f) : 64.0f;
	settings.MaximumGap = std::max(settings.MaximumGap, settings.DepthBias + settings.BlendWidth);
	settings.MaximumDistance = IsFinite(settings.MaximumDistance) ? std::clamp(settings.MaximumDistance, 0.0f, 65536.0f) : 8192.0f;
	settings.BoundsExpansion = IsFinite(settings.BoundsExpansion) ? std::clamp(settings.BoundsExpansion, 0.0f, 1024.0f) : 32.0f;

	auto sanitizeRules = [](std::vector<OverrideRule>& a_rules) {
		if (a_rules.size() > kMaximumRules) {
			a_rules.resize(kMaximumRules);
		}
		for (auto& rule : a_rules) {
			if (rule.Model.size() > kMaximumRuleLength) {
				rule.Model.resize(kMaximumRuleLength);
			}
			if (rule.NodePath.size() > kMaximumRuleLength) {
				rule.NodePath.resize(kMaximumRuleLength);
			}
		}
	};
	sanitizeRules(allowList);
	sanitizeRules(denyList);
}

void MeshBlending::RebuildRules()
{
	auto compile = [](const std::vector<OverrideRule>& a_source,
					   std::vector<CompiledRule>& a_destination,
					   ExactRuleSet& a_exactDestination) {
		a_destination.clear();
		a_destination.reserve(a_source.size());
		a_exactDestination.clear();
		for (const auto& sourceRule : a_source) {
			CompiledRule rule;
			rule.model = NormalizePath(sourceRule.Model, true);
			rule.nodePath = NormalizePath(sourceRule.NodePath, false);
			if (rule.model.empty() && rule.nodePath.empty()) {
				continue;
			}
			rule.modelHasWildcard = HasWildcard(rule.model);
			rule.nodeHasWildcard = HasWildcard(rule.nodePath);
			if (!rule.model.empty() && !rule.nodePath.empty() &&
				!rule.modelHasWildcard && !rule.nodeHasWildcard) {
				a_exactDestination.emplace(std::move(rule.model), std::move(rule.nodePath));
				continue;
			}
			a_destination.push_back(std::move(rule));
		}
	};

	compile(allowList, compiledAllowList, compiledExactAllowRules);
	compile(denyList, compiledDenyList, compiledExactDenyRules);
	compiledExactRuleModels.clear();
	for (const auto& [modelPath, nodePath] : compiledExactAllowRules) {
		(void)nodePath;
		compiledExactRuleModels.insert(modelPath);
	}
	for (const auto& [modelPath, nodePath] : compiledExactDenyRules) {
		(void)nodePath;
		compiledExactRuleModels.insert(modelPath);
	}
	flexibleRulesNeedNodePath =
		std::any_of(compiledAllowList.begin(), compiledAllowList.end(), [](const auto& a_rule) { return !a_rule.nodePath.empty(); }) ||
		std::any_of(compiledDenyList.begin(), compiledDenyList.end(), [](const auto& a_rule) { return !a_rule.nodePath.empty(); });
	InvalidateClassificationCache();
}

void MeshBlending::InvalidateClassificationCache()
{
	classificationCache.fill({});
	++policyGeneration;
	if (policyGeneration == 0u) {
		policyGeneration = 1u;
	}
}

void MeshBlending::BeginDiagnosticsFrame(std::uint32_t a_frame)
{
	if (diagnostics.currentFrame != a_frame) {
		diagnostics.lastFrameActive = diagnostics.currentFrameActive;
		diagnostics.currentFrameActive = 0u;
		diagnostics.currentFrame = a_frame;
	}
}

MeshBlending::PerFrame MeshBlending::GetCommonBufferData() const
{
	const float blendWidth = IsFinite(settings.BlendWidth) ? std::clamp(settings.BlendWidth, 0.01f, 256.0f) : 12.0f;
	const float depthBias = IsFinite(settings.DepthBias) ? std::clamp(settings.DepthBias, 0.0f, 128.0f) : 0.25f;
	const float maximumGap = std::max(
		IsFinite(settings.MaximumGap) ? std::clamp(settings.MaximumGap, 0.01f, 4096.0f) : 64.0f,
		depthBias + blendWidth);
	return {
		IsFinite(settings.BlendStrength) ? std::clamp(settings.BlendStrength, 0.0f, 1.0f) : 1.0f,
		blendWidth,
		depthBias,
		maximumGap
	};
}

bool MeshBlending::IsRuntimeEnabled() const
{
	if (!loaded || !ruleFileMutationAllowed || !globals::shaderCache || !globals::shaderCache->IsEnabled() || settings.Enabled == 0u ||
		!IsFinite(settings.BlendStrength) || settings.BlendStrength <= 0.0f) {
		return false;
	}
	switch (static_cast<DetectionMode>(settings.DetectionMode)) {
	case DetectionMode::kAllowListOnly:
		return !compiledAllowList.empty() || !compiledExactAllowRules.empty();
	case DetectionMode::kStrictAutomatic:
		return true;
	default:
		return false;
	}
}

bool MeshBlending::IsDiscoveryCaptureActive() const
{
	return loaded && ruleFileMutationAllowed && discoveryCaptureEnabled.load(std::memory_order_relaxed);
}

bool MeshBlending::IsDiscoveryCaptureSaturated() const
{
	std::scoped_lock lock(discoveryMutex);
	return discoveredRuleKeys.size() >= kMaximumRules;
}

void MeshBlending::SetDiscoveryCaptureEnabled(bool a_enabled)
{
	if (a_enabled && IsDiscoveryCaptureSaturated()) {
		discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
		return;
	}
	if (discoveryCaptureEnabled.exchange(a_enabled, std::memory_order_relaxed) == a_enabled) {
		return;
	}

	// Starting must revisit cached rejections, and stopping must discard capture-only
	// automatic classifications before normal policy operation resumes.
	InvalidateClassificationCache();
	logger::info("[Mesh Blending] Discovery capture {}", a_enabled ? "started" : "stopped");
}

void MeshBlending::CaptureDiscoveredRule(std::string_view a_modelPath, std::string_view a_nodePath)
{
	if (a_modelPath.empty() || a_nodePath.empty() ||
		a_modelPath.size() > kMaximumRuleLength || a_nodePath.size() > kMaximumRuleLength ||
		!IsValidUtf8(a_modelPath) || !IsValidUtf8(a_nodePath) ||
		a_modelPath.find_first_of("*?") != std::string_view::npos ||
		a_nodePath.find_first_of("*?") != std::string_view::npos) {
		std::scoped_lock lock(discoveryMutex);
		++discoveryDropped;
		return;
	}

	const std::pair key{ std::string(a_modelPath), std::string(a_nodePath) };
	std::scoped_lock lock(discoveryMutex);
	if (discoveredRuleKeys.contains(key)) {
		++discoveryDuplicateObservations;
		return;
	}
	if (discoveredRuleKeys.size() >= kMaximumRules) {
		++discoveryDropped;
		discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
		return;
	}
	discoveredRuleKeys.insert(key);
	if (discoveredRuleKeys.size() >= kMaximumRules) {
		// Stop before subsequent cache misses can repeat the bounded sibling search
		// when the session can no longer retain another candidate.
		discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
	}
}

void MeshBlending::EnsureRuleFileLoaded()
{
	if (ruleFileLoadAttempted) {
		return;
	}
	ruleFileLoadAttempted = true;
	LoadRuleFile();
}

bool MeshBlending::LoadRuleFile()
{
	json ruleFile;
	std::string error;
	const auto result = Util::FileHelpers::ReadJsonFile(GetRuleFilePath(), ruleFile, error);
	if (result == Util::FileHelpers::JsonFileReadResult::NotFound) {
		allowList.clear();
		denyList.clear();
		ruleFileMutationAllowed = true;
		ruleFileLoadError.clear();
		RebuildRules();
		return true;
	}
	if (result == Util::FileHelpers::JsonFileReadResult::Error) {
		ruleFileMutationAllowed = false;
		discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
		ruleFileLoadError = std::format("Could not read the rule file: {}", error);
		RebuildRules();
		logger::warn("[Mesh Blending] Could not read {}: {}", GetRuleFilePath().string(), error);
		return false;
	}

	try {
		if (!ruleFile.is_object() ||
			ruleFile.at("SchemaVersion").get<std::uint32_t>() != kRuleFileSchemaVersion) {
			ruleFileMutationAllowed = false;
			discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
			ruleFileLoadError = "The rule file has an unsupported schema.";
			RebuildRules();
			logger::warn("[Mesh Blending] Unsupported rule schema in {}", GetRuleFilePath().string());
			return false;
		}
		auto loadedAllowList = ruleFile.value("AllowList", std::vector<OverrideRule>{});
		auto loadedDenyList = ruleFile.value("DenyList", std::vector<OverrideRule>{});
		auto rulesAreValid = [](const std::vector<OverrideRule>& a_rules) {
			return a_rules.size() <= kMaximumRules &&
			       std::all_of(a_rules.begin(), a_rules.end(), [](const auto& a_rule) {
					   return a_rule.Model.size() <= kMaximumRuleLength &&
				              a_rule.NodePath.size() <= kMaximumRuleLength;
				   });
		};
		if (!rulesAreValid(loadedAllowList) || !rulesAreValid(loadedDenyList)) {
			ruleFileMutationAllowed = false;
			discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
			ruleFileLoadError = "The rule file exceeds the supported rule count or path length.";
			RebuildRules();
			logger::warn("[Mesh Blending] Rule limits exceeded in {}", GetRuleFilePath().string());
			return false;
		}
		allowList = std::move(loadedAllowList);
		denyList = std::move(loadedDenyList);
		ruleFileMutationAllowed = true;
		ruleFileLoadError.clear();
		SanitizeSettings();
		RebuildRules();
	} catch (const json::exception& exception) {
		ruleFileMutationAllowed = false;
		discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
		ruleFileLoadError = std::format("The rule file is invalid: {}", exception.what());
		RebuildRules();
		logger::warn("[Mesh Blending] Invalid rule file {}: {}", GetRuleFilePath().string(), exception.what());
		return false;
	}

	logger::info(
		"[Mesh Blending] Loaded {} allow and {} deny rules from {}",
		allowList.size(),
		denyList.size(),
		GetRuleFilePath().string());
	return true;
}

bool MeshBlending::WriteRuleFile(
	const std::vector<OverrideRule>& a_allowList,
	const std::vector<OverrideRule>& a_denyList,
	std::string& a_error) const
{
	if (!ruleFileMutationAllowed) {
		a_error = std::format(
			"{} Fix or remove MeshBlendingRules.json, then restart before saving.",
			ruleFileLoadError);
		return false;
	}
	try {
		const json ruleFile{
			{ "SchemaVersion", kRuleFileSchemaVersion },
			{ "AllowList", a_allowList },
			{ "DenyList", a_denyList }
		};
		return Util::FileHelpers::WriteTextFileAtomic(GetRuleFilePath(), ruleFile.dump(1), a_error);
	} catch (const json::exception& exception) {
		a_error = exception.what();
		return false;
	}
}

MeshBlending::DiscoverySaveResult MeshBlending::SaveDiscoveredRules()
{
	DiscoverySaveResult result;
	// Re-read only for an explicit mutation so edits made by a mod author while
	// the game is running are merged instead of being replaced by stale policy.
	if (!ruleFileMutationAllowed || !LoadRuleFile()) {
		result.error = std::format("{} The existing file was not changed.", ruleFileLoadError);
		return result;
	}
	std::set<std::pair<std::string, std::string>> discoveredSnapshot;
	{
		std::scoped_lock lock(discoveryMutex);
		discoveredSnapshot = discoveredRuleKeys;
	}

	std::vector<OverrideRule> additions;
	additions.reserve(discoveredSnapshot.size());
	for (const auto& [modelPath, nodePath] : discoveredSnapshot) {
		if (MatchesRules(compiledDenyList, compiledExactDenyRules, modelPath, nodePath)) {
			++result.denied;
			continue;
		}
		if (MatchesRules(compiledAllowList, compiledExactAllowRules, modelPath, nodePath)) {
			++result.alreadyAllowed;
			continue;
		}
		additions.push_back({ modelPath, nodePath });
	}

	const std::size_t remainingCapacity = kMaximumRules - std::min(allowList.size(), kMaximumRules);
	if (additions.size() > remainingCapacity) {
		result.capacityRejected = additions.size() - remainingCapacity;
		result.error = std::format(
			"{} candidates exceed the remaining allow-list capacity; nothing was saved.",
			result.capacityRejected);
		return result;
	}

	auto proposedAllowList = allowList;
	proposedAllowList.insert(proposedAllowList.end(), additions.begin(), additions.end());
	if (!WriteRuleFile(proposedAllowList, denyList, result.error)) {
		return result;
	}

	allowList = std::move(proposedAllowList);
	result.added = additions.size();
	result.success = true;
	RebuildRules();
	logger::info(
		"[Mesh Blending] Saved {} new candidates ({} already allowed, {} denied) to {}",
		result.added,
		result.alreadyAllowed,
		result.denied,
		GetRuleFilePath().string());
	return result;
}

bool MeshBlending::ClearSavedRules(std::string& a_error)
{
	// Refresh first so a deny rule authored during this session is preserved.
	if (!ruleFileMutationAllowed || !LoadRuleFile()) {
		a_error = std::format("{} The existing file was not changed.", ruleFileLoadError);
		return false;
	}
	if (!WriteRuleFile({}, denyList, a_error)) {
		return false;
	}
	allowList.clear();
	RebuildRules();
	logger::info("[Mesh Blending] Cleared saved allow rules in {}", GetRuleFilePath().string());
	return true;
}

void MeshBlending::ClearDiscoveredRules()
{
	std::scoped_lock lock(discoveryMutex);
	discoveredRuleKeys.clear();
	discoveryDuplicateObservations = 0u;
	discoveryDropped = 0u;
}

bool MeshBlending::BuildSourceState(RE::BSRenderPass* a_pass, SourceState& a_source) const
{
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty) {
		return false;
	}

	a_source.geometry = a_pass->geometry;
	auto& runtimeData = a_source.geometry->GetGeometryRuntimeData();
	a_source.alphaProperty = runtimeData.alphaProperty.get();
	if (!a_source.alphaProperty ||
		a_source.alphaProperty->GetRTTI() != globals::rtti::NiAlphaPropertyRTTI.get() ||
		!a_source.alphaProperty->GetAlphaBlending() ||
		a_source.alphaProperty->GetAlphaTesting() ||
		a_source.alphaProperty->GetSrcBlendMode() != RE::NiAlphaProperty::AlphaFunction::kSrcAlpha ||
		a_source.alphaProperty->GetDestBlendMode() != RE::NiAlphaProperty::AlphaFunction::kInvSrcAlpha) {
		return false;
	}
	if (!runtimeData.rendererData || runtimeData.skinInstance) {
		return false;
	}

	auto* shaderProperty = runtimeData.shaderProperty.get();
	if (!shaderProperty || shaderProperty != a_pass->shaderProperty ||
		shaderProperty->GetRTTI() != globals::rtti::BSLightingShaderPropertyRTTI.get()) {
		return false;
	}
	a_source.shaderProperty = static_cast<RE::BSLightingShaderProperty*>(shaderProperty);
	a_source.shaderFlags = a_source.shaderProperty->flags.underlying();
	if ((a_source.shaderFlags & ToMask(ShaderFlag::kZBufferTest)) == 0u ||
		(a_source.shaderFlags & ToMask(ShaderFlag::kZBufferWrite)) != 0u ||
		(a_source.shaderFlags & kExcludedShaderFlags) != 0u) {
		return false;
	}

	a_source.material = a_source.shaderProperty->material;
	if (!a_source.material ||
		a_source.material->GetType() != RE::BSShaderMaterial::Type::kLighting ||
		!IsSafeMaterialFeature(a_source.material->GetFeature()) ||
		!IsFinite(a_source.shaderProperty->alpha) ||
		a_source.shaderProperty->alpha <= 0.0f ||
		!IsFiniteBound(a_source.geometry->worldBound)) {
		return false;
	}

	return true;
}

bool MeshBlending::ResolveStaticOwner(SourceState& a_source) const
{
	a_source.owner = a_source.geometry->GetUserData();
	if (!a_source.owner) {
		return false;
	}
	auto* baseObject = a_source.owner->GetBaseObject();
	if (!baseObject || baseObject->GetFormType() != RE::FormType::Static) {
		return false;
	}

	a_source.root = a_source.owner->Get3D();
	if (!a_source.root) {
		return false;
	}

	bool belongsToRoot = false;
	auto* current = static_cast<RE::NiAVObject*>(a_source.geometry);
	for (std::size_t depth = 0u; current && depth < kMaximumRootDepth; ++depth) {
		if (current == a_source.root) {
			belongsToRoot = true;
			break;
		}
		current = current->parent;
	}
	if (!belongsToRoot) {
		return false;
	}

	if (const auto* modelSwap = a_source.owner->extraList.GetByType<RE::ExtraModelSwap>();
		modelSwap && modelSwap->modelSwap) {
		const char* path = modelSwap->modelSwap->GetModel();
		if (path && path[0] != '\0') {
			a_source.model = modelSwap->modelSwap;
			a_source.modelPath = path;
		}
	}
	if (!a_source.model) {
		a_source.model = baseObject->As<RE::TESModel>();
		a_source.modelPath = a_source.model ? a_source.model->GetModel() : nullptr;
	}
	return true;
}

bool MeshBlending::IsInsideDistanceBubble(const RE::NiBound& a_bound, std::uint32_t a_frame)
{
	if (settings.MaximumDistance <= 0.0f) {
		return true;
	}
	if (cachedEyeFrame != a_frame) {
		cachedEyePosition = Util::GetAverageEyePosition();
		cachedEyeFrame = a_frame;
	}
	if (!IsFinite(cachedEyePosition.x) || !IsFinite(cachedEyePosition.y) || !IsFinite(cachedEyePosition.z)) {
		return true;
	}

	const double x = static_cast<double>(a_bound.center.x) - cachedEyePosition.x;
	const double y = static_cast<double>(a_bound.center.y) - cachedEyePosition.y;
	const double z = static_cast<double>(a_bound.center.z) - cachedEyePosition.z;
	const double maximumDistance = static_cast<double>(settings.MaximumDistance) + std::max(0.0f, a_bound.radius);
	return x * x + y * y + z * z <= maximumDistance * maximumDistance;
}

MeshBlending::Signature MeshBlending::BuildSourceSignature(const SourceState& a_source) const
{
	const auto& runtimeData = a_source.geometry->GetGeometryRuntimeData();
	const auto& bound = a_source.geometry->worldBound;
	Signature signature;
	signature.geometry = reinterpret_cast<std::uintptr_t>(a_source.geometry);
	signature.parent = reinterpret_cast<std::uintptr_t>(a_source.geometry->parent);
	signature.shaderProperty = reinterpret_cast<std::uintptr_t>(a_source.shaderProperty);
	signature.alphaProperty = reinterpret_cast<std::uintptr_t>(a_source.alphaProperty);
	signature.material = reinterpret_cast<std::uintptr_t>(a_source.material);
	signature.rendererData = reinterpret_cast<std::uintptr_t>(runtimeData.rendererData);
	signature.skinInstance = reinterpret_cast<std::uintptr_t>(runtimeData.skinInstance.get());
	signature.shaderFlags = a_source.shaderFlags;
	signature.materialFeature = static_cast<std::uint32_t>(a_source.material->GetFeature());
	signature.parentIndex = a_source.geometry->parentIndex;
	signature.boundXBits = std::bit_cast<std::uint32_t>(bound.center.x);
	signature.boundYBits = std::bit_cast<std::uint32_t>(bound.center.y);
	signature.boundZBits = std::bit_cast<std::uint32_t>(bound.center.z);
	signature.boundRadiusBits = std::bit_cast<std::uint32_t>(bound.radius);
	signature.policyGeneration = policyGeneration;
	signature.alphaFlags = a_source.alphaProperty->alphaFlags;
	return signature;
}

void MeshBlending::CompleteOwnershipSignature(const SourceState& a_source, Signature& a_signature) const
{
	a_signature.owner = reinterpret_cast<std::uintptr_t>(a_source.owner);
	a_signature.root = reinterpret_cast<std::uintptr_t>(a_source.root);
	a_signature.model = reinterpret_cast<std::uintptr_t>(a_source.model);
	a_signature.modelPath = reinterpret_cast<std::uintptr_t>(a_source.modelPath);
	a_signature.ownerFormID = a_source.owner->GetFormID();
}

bool MeshBlending::TryGetCachedClassification(
	const Signature& a_signature,
	std::uint32_t a_frame,
	bool a_sourceStateOnly,
	Classification& a_classification)
{
	const auto mixedKey = (a_signature.geometry >> 4u) ^ (a_signature.geometry >> 17u) ^ (a_signature.geometry >> 29u);
	const std::size_t setStart = (mixedKey & (kCacheSetCount - 1u)) * kCacheWays;
	for (std::size_t way = 0u; way < kCacheWays; ++way) {
		auto& entry = classificationCache[setStart + way];
		if (!entry.occupied ||
			(a_sourceStateOnly ? !entry.signature.HasSameSourceState(a_signature) : !(entry.signature == a_signature))) {
			continue;
		}
		// Jitter revalidation by stable geometry bits so a cell full of shapes does
		// not synchronously retraverse every NIF on the same frame.
		const auto validationFrames = a_sourceStateOnly ?
		                                  kSourceCacheValidationFrames + static_cast<std::uint32_t>((a_signature.geometry >> 4u) & 0x3Fu) :
		                                  kCacheValidationFrames + static_cast<std::uint32_t>((a_signature.geometry >> 4u) & 0xFFu);
		if (a_frame - entry.classifiedFrame >= validationFrames) {
			entry.occupied = false;
			return false;
		}
		entry.lastUsedFrame = a_frame;
		a_classification = entry.classification;
		return true;
	}
	return false;
}

void MeshBlending::StoreClassification(
	const Signature& a_signature,
	std::uint32_t a_frame,
	Classification a_classification)
{
	const auto mixedKey = (a_signature.geometry >> 4u) ^ (a_signature.geometry >> 17u) ^ (a_signature.geometry >> 29u);
	const std::size_t setStart = (mixedKey & (kCacheSetCount - 1u)) * kCacheWays;
	std::size_t replacement = setStart;
	for (std::size_t way = 0u; way < kCacheWays; ++way) {
		const std::size_t index = setStart + way;
		if (!classificationCache[index].occupied) {
			replacement = index;
			break;
		}
		if (classificationCache[index].lastUsedFrame < classificationCache[replacement].lastUsedFrame) {
			replacement = index;
		}
	}
	if (classificationCache[replacement].occupied && globals::state && globals::state->IsDeveloperMode()) {
		++diagnostics.cacheEvictions;
	}
	classificationCache[replacement] = {
		a_signature,
		a_classification,
		a_frame,
		a_frame,
		true
	};
}

std::string MeshBlending::BuildModelPath(const SourceState& a_source) const
{
	return a_source.modelPath ? NormalizePath(a_source.modelPath, true) : std::string{};
}

std::string MeshBlending::BuildNodePath(const SourceState& a_source) const
{
	std::array<const RE::NiAVObject*, kMaximumRootDepth> path{};
	std::size_t pathLength = 0u;
	for (auto* current = static_cast<const RE::NiAVObject*>(a_source.geometry);
		current && pathLength < path.size();
		current = current->parent) {
		path[pathLength++] = current;
		if (current == a_source.root) {
			break;
		}
	}
	if (pathLength == 0u || path[pathLength - 1u] != a_source.root) {
		return {};
	}

	std::string result;
	result.reserve(pathLength * 24u);
	for (std::size_t index = pathLength; index > 0u; --index) {
		const auto* object = path[index - 1u];
		if (!result.empty()) {
			result.push_back('/');
		}
		const char* name = object->name.c_str();
		result.append(name && *name ? name : "#");
		if (object != a_source.root) {
			result.push_back('[');
			char number[16]{};
			const auto conversion = std::to_chars(std::begin(number), std::end(number), object->parentIndex);
			if (conversion.ec == std::errc{}) {
				result.append(number, conversion.ptr);
			}
			result.push_back(']');
		}
	}
	return NormalizePath(result, false);
}

bool MeshBlending::MatchesRules(
	const std::vector<CompiledRule>& a_rules,
	const ExactRuleSet& a_exactRules,
	std::string_view a_model,
	std::string_view a_nodePath) const
{
	if (!a_model.empty() && !a_nodePath.empty() &&
		a_exactRules.contains(std::pair{ a_model, a_nodePath })) {
		return true;
	}
	for (const auto& rule : a_rules) {
		const bool modelMatches = rule.model.empty() ||
		                          (rule.modelHasWildcard ? WildcardMatch(rule.model, a_model) : rule.model == a_model);
		if (!modelMatches) {
			continue;
		}
		const bool nodeMatches = rule.nodePath.empty() ||
		                         (rule.nodeHasWildcard ? WildcardMatch(rule.nodePath, a_nodePath) : rule.nodePath == a_nodePath);
		if (nodeMatches) {
			return true;
		}
	}
	return false;
}

bool MeshBlending::BoundsOverlap(const RE::NiBound& a_source, const RE::NiBound& a_receiver) const
{
	if (!IsFiniteBound(a_source) || !IsFiniteBound(a_receiver)) {
		return false;
	}
	const double x = static_cast<double>(a_source.center.x) - a_receiver.center.x;
	const double y = static_cast<double>(a_source.center.y) - a_receiver.center.y;
	const double z = static_cast<double>(a_source.center.z) - a_receiver.center.z;
	const double maximumDistance = static_cast<double>(a_source.radius) + a_receiver.radius + settings.BoundsExpansion;
	return x * x + y * y + z * z <= maximumDistance * maximumDistance;
}

bool MeshBlending::IsSafeReceiver(const SourceState& a_source, RE::BSGeometry* a_receiver) const
{
	if (!a_receiver || a_receiver == a_source.geometry || a_receiver->GetUserData() != a_source.owner) {
		return false;
	}
	if (settings.RequireOverlappingBounds != 0u &&
		!BoundsOverlap(a_source.geometry->worldBound, a_receiver->worldBound)) {
		return false;
	}

	const auto& runtimeData = a_receiver->GetGeometryRuntimeData();
	if (!runtimeData.rendererData || runtimeData.skinInstance) {
		return false;
	}
	auto* shaderProperty = runtimeData.shaderProperty.get();
	if (!shaderProperty || shaderProperty->GetRTTI() != globals::rtti::BSLightingShaderPropertyRTTI.get()) {
		return false;
	}
	auto* lightingProperty = static_cast<RE::BSLightingShaderProperty*>(shaderProperty);
	const std::uint64_t flags = lightingProperty->flags.underlying();
	if ((flags & ToMask(ShaderFlag::kZBufferTest)) == 0u ||
		(flags & ToMask(ShaderFlag::kZBufferWrite)) == 0u ||
		(flags & kExcludedShaderFlags) != 0u ||
		!IsFinite(lightingProperty->alpha) || lightingProperty->alpha < 0.999f) {
		return false;
	}

	auto* material = lightingProperty->material;
	if (!material || material->GetType() != RE::BSShaderMaterial::Type::kLighting ||
		!IsSafeMaterialFeature(material->GetFeature())) {
		return false;
	}

	if (auto* alphaProperty = runtimeData.alphaProperty.get()) {
		if (alphaProperty->GetRTTI() != globals::rtti::NiAlphaPropertyRTTI.get() ||
			alphaProperty->GetAlphaBlending() || alphaProperty->GetAlphaTesting()) {
			return false;
		}
	}

	return true;
}

bool MeshBlending::HasOpaqueSibling(const SourceState& a_source, bool& a_hitTraversalLimit)
{
	a_hitTraversalLimit = false;
	std::array<RE::NiAVObject*, kMaximumTraversalObjects> stack{};
	std::size_t stackSize = 1u;
	std::size_t visited = 0u;
	stack[0] = a_source.root;
	const bool collectDiagnostics = globals::state && globals::state->IsDeveloperMode();
	if (collectDiagnostics) {
		++diagnostics.rootTraversals;
	}

#ifdef TRACY_ENABLE
	ZoneScopedN("MeshBlending - Classify NIF");
#endif

	while (stackSize > 0u) {
		if (visited++ >= kMaximumTraversalObjects) {
			a_hitTraversalLimit = true;
			return false;
		}
		auto* object = stack[--stackSize];
		if (!object) {
			continue;
		}
		if (collectDiagnostics) {
			++diagnostics.nodesVisited;
		}
		if (auto* geometry = object->AsGeometry(); geometry && IsSafeReceiver(a_source, geometry)) {
			return true;
		}
		if (auto* node = object->AsNode()) {
			for (const auto& child : node->GetChildren()) {
				if (!child) {
					continue;
				}
				if (stackSize >= stack.size()) {
					a_hitTraversalLimit = true;
					return false;
				}
				stack[stackSize++] = child.get();
			}
		}
	}
	return false;
}

MeshBlending::Classification MeshBlending::ClassifyOnCacheMiss(
	const SourceState& a_source,
	bool a_captureDiscovery)
{
	if (a_source.root->HasAnimation()) {
		return Classification::kRejected;
	}

	const std::string modelPath = BuildModelPath(a_source);
	const bool logClassification = settings.DeveloperLogging != 0u &&
	                               globals::state && globals::state->IsDeveloperMode() &&
	                               classificationLogs < kMaximumClassificationLogs;
	const bool exactModelNeedsNodePath = compiledExactRuleModels.contains(modelPath);
	std::string nodePath = (flexibleRulesNeedNodePath || exactModelNeedsNodePath) ? BuildNodePath(a_source) : std::string{};

	if (MatchesRules(compiledDenyList, compiledExactDenyRules, modelPath, nodePath)) {
		return Classification::kRejected;
	}
	if (MatchesRules(compiledAllowList, compiledExactAllowRules, modelPath, nodePath)) {
		if (logClassification) {
			if (nodePath.empty()) {
				nodePath = BuildNodePath(a_source);
			}
			logger::debug("[Mesh Blending] allow-list match: model='{}', node='{}'", modelPath, nodePath);
			++classificationLogs;
		}
		return Classification::kAllowedByRule;
	}
	const bool automaticBlendingEnabled =
		settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic);
	const bool captureHasCapacity = a_captureDiscovery && !IsDiscoveryCaptureSaturated();
	if (a_captureDiscovery && !captureHasCapacity) {
		discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
	}
	if (!automaticBlendingEnabled && !captureHasCapacity) {
		return Classification::kRejected;
	}
	if (modelPath.empty()) {
		return Classification::kRejected;
	}
	bool hitTraversalLimit = false;
	if (!HasOpaqueSibling(a_source, hitTraversalLimit)) {
		if (hitTraversalLimit && globals::state->IsDeveloperMode()) {
			++diagnostics.traversalLimitRejects;
		}
		return Classification::kRejected;
	}
	if (captureHasCapacity) {
		if (nodePath.empty()) {
			nodePath = BuildNodePath(a_source);
		}
		CaptureDiscoveredRule(modelPath, nodePath);
	} else if (logClassification) {
		if (nodePath.empty()) {
			nodePath = BuildNodePath(a_source);
		}
		logger::debug("[Mesh Blending] automatic match: model='{}', node='{}'", modelPath, nodePath);
		++classificationLogs;
	}
	return Classification::kAutomatic;
}

void MeshBlending::PrepareLightingDraw(RE::BSRenderPass* a_pass)
{
	auto* state = globals::state;
	if (!state) {
		return;
	}
	constexpr std::uint32_t descriptorMask = static_cast<std::uint32_t>(State::ExtraShaderDescriptors::MeshBlending);
	constexpr std::uint32_t inWorldMask = static_cast<std::uint32_t>(State::ExtraShaderDescriptors::InWorld);
	constexpr std::uint32_t reflectionMask = static_cast<std::uint32_t>(State::ExtraShaderDescriptors::IsReflections);
	auto& descriptor = state->permutationData.ExtraShaderDescriptor;
	if ((descriptor & descriptorMask) != 0u) {
		descriptor &= ~descriptorMask;
	}

	const bool runtimeEnabled = IsRuntimeEnabled();
	const bool discoveryCaptureActive = IsDiscoveryCaptureActive();
	if (!runtimeEnabled && !discoveryCaptureActive) {
		return;
	}
#ifdef TRACY_ENABLE
	ZoneScopedN("MeshBlending - Prepare Lighting Draw");
#endif
	const bool collectDiagnostics = state->IsDeveloperMode();
	BeginDiagnosticsFrame(state->frameCount);
	if (collectDiagnostics) {
		++diagnostics.drawCalls;
	}
	if (!state->inWorld || (descriptor & inWorldMask) == 0u || (descriptor & reflectionMask) != 0u) {
		if (collectDiagnostics) {
			++diagnostics.scopeRejects;
		}
		return;
	}

	SourceState source;
	if (!BuildSourceState(a_pass, source)) {
		if (collectDiagnostics) {
			++diagnostics.sourceRejects;
		}
		return;
	}
	if (!IsInsideDistanceBubble(source.geometry->worldBound, state->frameCount)) {
		if (collectDiagnostics) {
			++diagnostics.distanceRejects;
		}
		return;
	}
	Signature signature = BuildSourceSignature(source);
	const bool sourceStateCacheAllowed =
		compiledAllowList.empty() && compiledDenyList.empty() &&
		compiledExactAllowRules.empty() && compiledExactDenyRules.empty();
	Classification classification = Classification::kRejected;
	bool cacheHit = sourceStateCacheAllowed &&
	                TryGetCachedClassification(signature, state->frameCount, true, classification);
	if (cacheHit && collectDiagnostics) {
		++diagnostics.preOwnerCacheHits;
	}
	if (!cacheHit) {
		if (collectDiagnostics) {
			++diagnostics.ownerResolutionAttempts;
		}
		if (!ResolveStaticOwner(source)) {
			if (collectDiagnostics) {
				++diagnostics.sourceRejects;
				if (sourceStateCacheAllowed) {
					++diagnostics.cacheMisses;
				}
			}
			if (sourceStateCacheAllowed) {
				StoreClassification(signature, state->frameCount, Classification::kRejected);
			}
			return;
		}
		CompleteOwnershipSignature(source, signature);
		if (!sourceStateCacheAllowed) {
			cacheHit = TryGetCachedClassification(signature, state->frameCount, false, classification);
			if (cacheHit && collectDiagnostics) {
				++diagnostics.fullSignatureCacheHits;
			}
		}
	}
	if (cacheHit) {
		if (collectDiagnostics) {
			++diagnostics.cacheHits;
		}
	} else {
		if (collectDiagnostics) {
			++diagnostics.cacheMisses;
		}
		classification = ClassifyOnCacheMiss(source, discoveryCaptureActive);
		StoreClassification(signature, state->frameCount, classification);
	}
	if (classification == Classification::kRejected) {
		return;
	}

	if (collectDiagnostics) {
		if (classification == Classification::kAllowedByRule) {
			++diagnostics.ruleAccepts;
		} else {
			++diagnostics.automaticAccepts;
		}
	}

	const bool automaticBlendingEnabled =
		settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic);
	if (!runtimeEnabled ||
		(classification == Classification::kAutomatic && !automaticBlendingEnabled)) {
		return;
	}

	if (collectDiagnostics) {
		++diagnostics.activeDraws;
	}
	++diagnostics.currentFrameActive;
	descriptor |= descriptorMask;
}

void MeshBlending::DrawDiagnostics() const
{
	if (!globals::state || !globals::state->IsDeveloperMode() || !ImGui::TreeNode("Classifier diagnostics")) {
		return;
	}
	ImGui::Text("Active draws: %u last frame, %u this frame", diagnostics.lastFrameActive, diagnostics.currentFrameActive);
	ImGui::Text("Lifetime active draws: %llu", static_cast<unsigned long long>(diagnostics.activeDraws));
	ImGui::Text("Cache: %llu hits, %llu misses, %llu evictions",
		static_cast<unsigned long long>(diagnostics.cacheHits),
		static_cast<unsigned long long>(diagnostics.cacheMisses),
		static_cast<unsigned long long>(diagnostics.cacheEvictions));
	ImGui::Text("Cache paths: %llu pre-owner hits, %llu policy hits, %llu owner resolutions",
		static_cast<unsigned long long>(diagnostics.preOwnerCacheHits),
		static_cast<unsigned long long>(diagnostics.fullSignatureCacheHits),
		static_cast<unsigned long long>(diagnostics.ownerResolutionAttempts));
	ImGui::Text("Classifier: %llu traversals, %llu objects visited, %llu capped",
		static_cast<unsigned long long>(diagnostics.rootTraversals),
		static_cast<unsigned long long>(diagnostics.nodesVisited),
		static_cast<unsigned long long>(diagnostics.traversalLimitRejects));
	ImGui::Text("Rejects: %llu scope, %llu source, %llu distance",
		static_cast<unsigned long long>(diagnostics.scopeRejects),
		static_cast<unsigned long long>(diagnostics.sourceRejects),
		static_cast<unsigned long long>(diagnostics.distanceRejects));
	ImGui::Text("Accepts: %llu allow-list, %llu automatic",
		static_cast<unsigned long long>(diagnostics.ruleAccepts),
		static_cast<unsigned long long>(diagnostics.automaticAccepts));
	ImGui::TreePop();
}

void MeshBlending::DrawDiscoverySettings()
{
	static Util::ConfirmationPopup savePopup{
		"Save Detected Meshes?##MeshBlending",
		"This promotes the captured exact model/node candidates into the Mesh Blending allow list. "
		"This writes MeshBlendingRules.json immediately; it does not wait for Save Settings. Saved allow rules avoid "
		"repeating the automatic sibling search. Strict automatic, the default mode, previews candidates while they are "
		"being discovered.",
		"Save Detected Meshes"
	};
	static Util::ConfirmationPopup clearPopup{
		"Clear Saved Allow List?##MeshBlending",
		"This stops discovery, clears the current capture, and empties the saved AllowList in MeshBlendingRules.json. "
		"Manually authored deny rules are preserved.",
		"Clear Saved Allow List"
	};

	ImGui::SeparatorText("Mesh discovery");
	auto* state = globals::state;
	const bool mutationBlocked = state && state->IsPersistentMutationBlocked();
	const bool ruleFileBlocked = !ruleFileMutationAllowed;
	const bool captureSaturated = IsDiscoveryCaptureSaturated();
	bool captureEnabled = discoveryCaptureEnabled.load(std::memory_order_relaxed);
	if (mutationBlocked || ruleFileBlocked || captureSaturated) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Checkbox("Discover Blendable Meshes", &captureEnabled)) {
		SetDiscoveryCaptureEnabled(captureEnabled);
		if (captureEnabled && !discoveryCaptureEnabled.load(std::memory_order_relaxed)) {
			discoveryStatus = "Discovery is full. Save the current results before starting another session.";
		} else {
			discoveryStatus.clear();
		}
	}
	if (mutationBlocked || ruleFileBlocked || captureSaturated) {
		ImGui::EndDisabled();
	}
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"While active, records unique exact model/node pairs that pass strict automatic safety checks.\n"
			"It scans only rendered meshes inside the culling distance and costs additional CPU time while active.\n"
			"The toggle is session-only and does not change the saved Detection mode.\n"
			"Strict automatic previews candidates while recording; Allow list only records without applying unsaved candidates.");
	}

	std::size_t discoveredCount = 0u;
	std::size_t duplicateCount = 0u;
	std::size_t droppedCount = 0u;
	{
		std::scoped_lock lock(discoveryMutex);
		discoveredCount = discoveredRuleKeys.size();
		duplicateCount = discoveryDuplicateObservations;
		droppedCount = discoveryDropped;
	}
	ImGui::Text(
		"Detected this session: %zu / %zu unique; %zu duplicate observations; %zu dropped",
		discoveredCount,
		kMaximumRules,
		duplicateCount,
		droppedCount);
	ImGui::Text("Saved rules: %zu allow, %zu deny", allowList.size(), denyList.size());
	ImGui::TextDisabled("Rule file: Data/SKSE/Plugins/CommunityShaders/MeshBlendingRules.json");
	if (ruleFileBlocked) {
		ImGui::TextWrapped(
			"Mesh Blending and discovery are disabled, and the existing rule file is protected from replacement: %s "
			"Fix or remove it, then restart.",
			ruleFileLoadError.c_str());
	}
	if (mutationBlocked) {
		ImGui::TextDisabled("Rule-file changes are paused while the game is saving or loading.");
	}
	if (droppedCount != 0u) {
		ImGui::TextWrapped("Some candidates were not retained because the capture was full or a path was unsafe for an exact JSON rule.");
	}
	if (discoveredCount >= kMaximumRules) {
		ImGui::TextWrapped("Discovery stopped at the session limit. Save the current results before starting another session.");
	}

	if (mutationBlocked || ruleFileBlocked || discoveredCount == 0u) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Save Detected Meshes")) {
		savePopup.Request();
	}
	if (mutationBlocked || ruleFileBlocked || discoveredCount == 0u) {
		ImGui::EndDisabled();
	}
	ImGui::SameLine();
	if (mutationBlocked || ruleFileBlocked || (discoveredCount == 0u && allowList.empty())) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Clear Saved Allow List")) {
		clearPopup.Request();
	}
	if (mutationBlocked || ruleFileBlocked || (discoveredCount == 0u && allowList.empty())) {
		ImGui::EndDisabled();
	}

	if (!discoveryStatus.empty()) {
		ImGui::TextWrapped("%s", discoveryStatus.c_str());
	}

	if (savePopup.Draw()) {
		if (globals::state && globals::state->IsPersistentMutationBlocked()) {
			discoveryStatus = "Nothing was changed because the game is saving or loading.";
		} else {
			SetDiscoveryCaptureEnabled(false);
			const auto result = SaveDiscoveredRules();
			if (result.success) {
				discoveryStatus = std::format(
					"Saved {} new exact rules; {} were already allowed and {} were denied.",
					result.added,
					result.alreadyAllowed,
					result.denied);
				ClearDiscoveredRules();
			} else {
				discoveryStatus = std::format("Nothing was changed: {}", result.error);
			}
		}
	}
	if (clearPopup.Draw()) {
		if (globals::state && globals::state->IsPersistentMutationBlocked()) {
			discoveryStatus = "Nothing was changed because the game is saving or loading.";
		} else {
			SetDiscoveryCaptureEnabled(false);
			std::string error;
			if (ClearSavedRules(error)) {
				ClearDiscoveredRules();
				discoveryStatus = "Cleared the saved allow list and current discovery capture.";
			} else {
				discoveryStatus = std::format("Could not clear MeshBlendingRules.json: {}", error);
			}
		}
	}
}

void MeshBlending::DrawSettings()
{
	bool enabled = settings.Enabled != 0u;
	if (ImGui::Checkbox("Enable", &enabled)) {
		settings.Enabled = enabled ? 1u : 0u;
	}

	bool classifierChanged = false;
	int detectionMode = static_cast<int>(settings.DetectionMode);
	static constexpr const char* detectionModes[] = { "Disabled", "Allow list only", "Strict automatic" };
	if (ImGui::Combo("Detection mode", &detectionMode, detectionModes, static_cast<int>(std::size(detectionModes)))) {
		settings.DetectionMode = static_cast<std::uint32_t>(detectionMode);
		classifierChanged = true;
	}
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Strict automatic is the default and uses conservative static-owner, material, bounds, and sibling gates. Deny rules always win.");
	}

	ImGui::SliderFloat("Blend Strength", &settings.BlendStrength, 0.0f, 1.0f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Controls how strongly the intersection fade is applied. Zero disables runtime blending work.");
	}
	ImGui::SliderFloat("Blend Width", &settings.BlendWidth, 0.25f, 128.0f, "%.2f units");
	ImGui::SliderFloat("Depth bias", &settings.DepthBias, 0.0f, 16.0f, "%.2f units");
	ImGui::SliderFloat("Maximum gap", &settings.MaximumGap, 1.0f, 256.0f, "%.1f units");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Maximum gap is clamped above bias + width so the fade cannot jump discontinuously.");
	}

	if (ImGui::TreeNodeEx("Performance and safety")) {
		ImGui::SliderFloat("Culling Distance", &settings.MaximumDistance, 0.0f, 32768.0f, "%.0f units");
		if (auto tooltip = Util::HoverTooltipWrapper()) {
			ImGui::Text("Shapes outside this camera/eye-centered bubble are rejected before root lookup. Set to 0 for unlimited distance.");
		}
		classifierChanged |= ImGui::SliderFloat("Bounds expansion", &settings.BoundsExpansion, 0.0f, 256.0f, "%.0f units");
		ImGui::TextDisabled("Strict automatic always requires a plausible opaque sibling.");
		if (globals::state && globals::state->IsDeveloperMode()) {
			bool requireBounds = settings.RequireOverlappingBounds != 0u;
			if (ImGui::Checkbox("Require overlapping bounds", &requireBounds)) {
				settings.RequireOverlappingBounds = requireBounds ? 1u : 0u;
				classifierChanged = true;
			}
			if (auto tooltip = Util::HoverTooltipWrapper()) {
				ImGui::Text("Disabling overlap is a developer-only relaxation and can increase false matches.");
			}
		}
		ImGui::TreePop();
	}

	DrawDiscoverySettings();

	const std::size_t compiledAllowCount = compiledAllowList.size() + compiledExactAllowRules.size();
	const std::size_t compiledDenyCount = compiledDenyList.size() + compiledExactDenyRules.size();
	ImGui::TextDisabled("Policy rules are read from MeshBlendingRules.json.");
	ImGui::Text("Compiled rules: %zu allow, %zu deny", compiledAllowCount, compiledDenyCount);
	if (settings.Enabled != 0u && settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kAllowListOnly) && compiledAllowCount == 0u) {
		ImGui::TextDisabled("Inactive: allow-list-only mode has no compiled allow rules.");
	}
	if (settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic)) {
		ImGui::TextWrapped("Strict automatic detection is active. Deny rules always win; explicit allows still pass every render-state and static-owner safety gate.");
	}
	if (globals::state && globals::state->IsDeveloperMode()) {
		bool developerLogging = settings.DeveloperLogging != 0u;
		if (ImGui::Checkbox("Log accepted cache misses", &developerLogging)) {
			settings.DeveloperLogging = developerLogging ? 1u : 0u;
		}
		DrawDiagnostics();
	}

	SanitizeSettings();
	if (classifierChanged) {
		InvalidateClassificationCache();
	}
}

void MeshBlending::DrawEssentialSettings()
{
	bool enabled = settings.Enabled != 0u;
	if (ImGui::Checkbox("Enable", &enabled)) {
		settings.Enabled = enabled ? 1u : 0u;
	}
	const std::size_t compiledAllowCount = compiledAllowList.size() + compiledExactAllowRules.size();
	const std::size_t compiledDenyCount = compiledDenyList.size() + compiledExactDenyRules.size();
	ImGui::TextDisabled("Mode: %s; compiled rules: %zu allow, %zu deny", DetectionModeName(settings.DetectionMode), compiledAllowCount, compiledDenyCount);
	if (settings.Enabled != 0u && settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kAllowListOnly) && compiledAllowCount == 0u) {
		ImGui::TextDisabled("Inactive until an allow rule is configured.");
	}
	if (IsDiscoveryCaptureActive()) {
		ImGui::TextDisabled("Discovery is active; open the full Mesh Blending settings to stop it.");
	}
	if (!ruleFileMutationAllowed) {
		ImGui::TextWrapped("Mesh Blending is disabled until MeshBlendingRules.json is fixed or removed and the game is restarted.");
	}
}

void MeshBlending::DrawPerformanceSettings(bool a_advanced)
{
	bool enabled = settings.Enabled != 0u;
	if (ImGui::Checkbox("Enable", &enabled)) {
		settings.Enabled = enabled ? 1u : 0u;
	}
	ImGui::SliderFloat("Culling Distance", &settings.MaximumDistance, 0.0f, 32768.0f, "%.0f units");
	if (IsDiscoveryCaptureActive()) {
		ImGui::TextDisabled("Stop Discover Blendable Meshes before running a feature-cost measurement.");
	}
	if (a_advanced) {
		ImGui::Text("Mode: %s; active draws: %u last frame, %u this frame", DetectionModeName(settings.DetectionMode), diagnostics.lastFrameActive, diagnostics.currentFrameActive);
		if (diagnostics.lastFrameActive == 0u && diagnostics.currentFrameActive == 0u) {
			ImGui::TextDisabled("No mesh-blending draws are visible; A/B measures idle overhead only.");
		}
	}
	SanitizeSettings();
}

json MeshBlending::CapturePerformanceSettingsState() const
{
	return json{
		{ "Enabled", settings.Enabled },
		{ "MaximumDistance", settings.MaximumDistance }
	};
}

void MeshBlending::SetPerformanceCostMeasurementEnabled(bool a_enabled)
{
	// Discovery is separate CPU instrumentation and would invalidate both legs of
	// the feature-cost comparison. Its session state is restored afterward.
	SetDiscoveryCaptureEnabled(false);
	settings.Enabled = a_enabled ? 1u : 0u;
}

json MeshBlending::CapturePerformanceCostMeasurementState() const
{
	return json{
		{ "Settings", settings },
		{ "DiscoveryCaptureEnabled", discoveryCaptureEnabled.load(std::memory_order_relaxed) }
	};
}

void MeshBlending::RestorePerformanceCostMeasurementState(const json& a_state)
{
	if (!a_state.is_object()) {
		return;
	}
	try {
		settings = a_state.contains("Settings") ? a_state.at("Settings").get<Settings>() : a_state.get<Settings>();
		SanitizeSettings();
		InvalidateClassificationCache();
		SetDiscoveryCaptureEnabled(a_state.value("DiscoveryCaptureEnabled", false));
	} catch (const json::exception& exception) {
		logger::warn("[Mesh Blending] Ignoring invalid performance state: {}", exception.what());
	}
}

void MeshBlending::LoadSettings(json& a_json)
{
	try {
		settings = a_json.get<Settings>();
	} catch (const json::exception& exception) {
		logger::warn("[Mesh Blending] Invalid settings; restoring safe defaults: {}", exception.what());
		settings = {};
	}
	// Rules have their own atomically written file and are never sourced from
	// SettingsUser.json, even if stale fields remain there from a development build.
	SanitizeSettings();
	EnsureRuleFileLoaded();
	InvalidateClassificationCache();
}

void MeshBlending::SaveSettings(json& a_json)
{
	SanitizeSettings();
	a_json = settings;
}

void MeshBlending::RestoreDefaultSettings()
{
	settings = {};
	SanitizeSettings();
	EnsureRuleFileLoaded();
	InvalidateClassificationCache();
}
