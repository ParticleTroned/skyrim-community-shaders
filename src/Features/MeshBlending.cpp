#include "MeshBlending.h"

#include "../Globals.h"
#include "../ShaderCache.h"
#include "../State.h"
#include "../Util.h"
#include "../Utils/FileSystem.h"
#include "../Utils/Form.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>
#include <tuple>

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
	ProjectedSnowEdgeWidth,
	LandscapeLayerBlending,
	DetectionMode,
	MaximumDistance,
	BoundsExpansion,
	RequireOverlappingBounds,
	DeveloperLogging);

namespace
{
	using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;
	using MaterialFeature = RE::BSShaderMaterial::Feature;

	constexpr std::uint32_t kRuleFileSchemaVersion = 4u;
	constexpr std::uint32_t kOldestSupportedRuleFileSchemaVersion = 1u;

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

	std::string NormalizeIdentity(std::string_view a_value)
	{
		std::string normalized;
		normalized.reserve(a_value.size());
		for (char character : a_value) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
			normalized.push_back(character);
		}
		return normalized;
	}

	std::string NormalizeTexturePath(std::string_view a_path)
	{
		auto normalized = NormalizePath(a_path, false);
		if (normalized.starts_with("data/")) {
			normalized.erase(0u, 5u);
		}
		if (!normalized.empty() && !normalized.starts_with("textures/")) {
			normalized.insert(0u, "textures/");
		}
		return normalized;
	}

	std::string GetLandscapeDiffusePath(const RE::TESLandTexture* a_texture)
	{
		if (!a_texture) {
			return {};
		}
		auto* textureSet = Util::GetSeasonalSwap(a_texture->textureSet);
		if (!textureSet) {
			return {};
		}
		const char* diffuse = textureSet->GetTexturePath(RE::BSTextureSet::Textures::kDiffuse);
		return diffuse && *diffuse ? NormalizeTexturePath(diffuse) : std::string{};
	}

	bool IsSafeRuleString(std::string_view a_value, std::size_t a_maximumLength)
	{
		return a_value.size() <= a_maximumLength && IsValidUtf8(a_value) &&
		       std::none_of(a_value.begin(), a_value.end(), [](char character) {
			       const auto value = static_cast<unsigned char>(character);
			       return value < static_cast<unsigned char>(' ') || value == 0x7Fu;
		       });
	}

	bool IsPortableLandscapeFormKey(std::string_view a_form)
	{
		const auto separator = a_form.find('~');
		if (separator == std::string_view::npos || separator != a_form.rfind('~') ||
			separator <= 2u || separator + 1u >= a_form.size() ||
			a_form[0] != '0' || (a_form[1] != 'x' && a_form[1] != 'X') ||
			!IsSafeRuleString(a_form, 512u)) {
			return false;
		}
		std::uint32_t localFormID = 0u;
		const auto* first = a_form.data() + 2u;
		const auto* last = a_form.data() + separator;
		const auto [position, error] = std::from_chars(first, last, localFormID, 16);
		if (error != std::errc{} || position != last || localFormID == 0u || localFormID > 0x00FFFFFFu) {
			return false;
		}
		return true;
	}

	RE::TESLandTexture* ResolveLandscapeForm(std::string_view a_form)
	{
		if (!IsPortableLandscapeFormKey(a_form)) {
			return nullptr;
		}
		const auto components = Util::ParseSpid(std::string(a_form));
		if (components.localFormId == 0u || components.pluginName.empty()) {
			return nullptr;
		}
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return nullptr;
		}
		auto* form = dataHandler->LookupForm(components.localFormId, components.pluginName);
		if (!form || form->GetFormType() != RE::FormType::LandTexture) {
			return nullptr;
		}
		return static_cast<RE::TESLandTexture*>(form);
	}

	std::string BuildLandscapeDiagnosticKey(
		std::string_view a_form,
		std::string_view a_editorID,
		std::string_view a_diffuse)
	{
		if (!a_form.empty()) {
			return std::string("form:").append(NormalizeIdentity(a_form));
		}
		if (!a_editorID.empty()) {
			return std::string("editor:").append(NormalizeIdentity(a_editorID));
		}
		if (!a_diffuse.empty()) {
			return std::string("diffuse:").append(NormalizeTexturePath(a_diffuse));
		}
		return {};
	}

	const char* DetectionModeName(std::uint32_t a_mode)
	{
		switch (static_cast<MeshBlending::DetectionMode>(a_mode)) {
		case MeshBlending::DetectionMode::kDisabled:
			return "Disabled";
		case MeshBlending::DetectionMode::kAllowListOnly:
			return "Allow list only";
		case MeshBlending::DetectionMode::kStrictAutomatic:
			return "Automatic";
		default:
			return "Invalid";
		}
	}
}

std::pair<std::string, std::vector<std::string>> MeshBlending::GetFeatureSummary()
{
	return {
		"Blends eligible NIF transition shapes and compatible soft/hard LTEX layers without an extra render pass.",
		{ "No additional render pass or geometry",
			"Conservative static-object classifier with deny-first overrides",
			"Fast LAND/LTEX material rules with bounded classification",
			"Bounded cache, traversal limits, and a configurable player-distance bubble",
			"Flat-screen and VR eye-correct depth sampling" }
	};
}

void MeshBlending::SanitizeSettings()
{
	settings.Enabled = settings.Enabled ? 1u : 0u;
	settings.RequireOverlappingBounds = settings.RequireOverlappingBounds ? 1u : 0u;
	settings.DeveloperLogging = settings.DeveloperLogging ? 1u : 0u;
	settings.LandscapeLayerBlending = settings.LandscapeLayerBlending ? 1u : 0u;
	if (settings.DetectionMode > static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic)) {
		settings.DetectionMode = static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic);
	}

	settings.BlendStrength = IsFinite(settings.BlendStrength) ? std::clamp(settings.BlendStrength, 0.0f, 1.0f) : 1.0f;
	settings.BlendWidth = IsFinite(settings.BlendWidth) ? std::clamp(settings.BlendWidth, 0.01f, 256.0f) : 12.0f;
	settings.DepthBias = IsFinite(settings.DepthBias) ? std::clamp(settings.DepthBias, 0.0f, 128.0f) : 0.25f;
	settings.MaximumGap = IsFinite(settings.MaximumGap) ? std::clamp(settings.MaximumGap, 0.01f, 4096.0f) : 64.0f;
	settings.MaximumGap = std::max(settings.MaximumGap, settings.DepthBias + settings.BlendWidth);
	settings.ProjectedSnowEdgeWidth = IsFinite(settings.ProjectedSnowEdgeWidth) ? std::clamp(settings.ProjectedSnowEdgeWidth, 0.0f, 16.0f) : 2.0f;
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
	sanitizeRules(detectedAllowList);
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

	std::vector<OverrideRule> effectiveAllowList;
	effectiveAllowList.reserve(allowList.size() + detectedAllowList.size());
	effectiveAllowList.insert(effectiveAllowList.end(), allowList.begin(), allowList.end());
	effectiveAllowList.insert(effectiveAllowList.end(), detectedAllowList.begin(), detectedAllowList.end());
	compile(effectiveAllowList, compiledAllowList, compiledExactAllowRules);
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

std::optional<MeshBlending::LandscapeMaterialKind> MeshBlending::ParseLandscapeMaterialKind(std::string_view a_kind)
{
	std::string normalized;
	normalized.reserve(a_kind.size());
	for (char character : a_kind) {
		if (character >= 'A' && character <= 'Z') {
			character = static_cast<char>(character - 'A' + 'a');
		}
		if (character != ' ' && character != '-' && character != '_') {
			normalized.push_back(character);
		}
	}
	for (auto value = static_cast<std::uint8_t>(LandscapeMaterialKind::kUnknown);
		 value <= static_cast<std::uint8_t>(LandscapeMaterialKind::kMetal);
		 ++value) {
		const auto kind = static_cast<LandscapeMaterialKind>(value);
		if (NormalizeIdentity(CanonicalLandscapeMaterialKind(kind)) == normalized) {
			return kind;
		}
	}
	return std::nullopt;
}

const char* MeshBlending::CanonicalLandscapeMaterialKind(LandscapeMaterialKind a_kind)
{
	switch (a_kind) {
	case LandscapeMaterialKind::kUnknown: return "Unknown";
	case LandscapeMaterialKind::kSoft: return "Soft";
	case LandscapeMaterialKind::kSnow: return "Snow";
	case LandscapeMaterialKind::kDirt: return "Dirt";
	case LandscapeMaterialKind::kAsh: return "Ash";
	case LandscapeMaterialKind::kGravel: return "Gravel";
	case LandscapeMaterialKind::kMud: return "Mud";
	case LandscapeMaterialKind::kSand: return "Sand";
	case LandscapeMaterialKind::kGrass: return "Grass";
	case LandscapeMaterialKind::kGroundCover: return "GroundCover";
	case LandscapeMaterialKind::kHard: return "Hard";
	case LandscapeMaterialKind::kIce: return "Ice";
	case LandscapeMaterialKind::kStone: return "Stone";
	case LandscapeMaterialKind::kBoulder: return "Boulder";
	case LandscapeMaterialKind::kWood: return "Wood";
	case LandscapeMaterialKind::kGlass: return "Glass";
	case LandscapeMaterialKind::kMetal: return "Metal";
	default: return "Unknown";
	}
}

MeshBlending::LandscapeMaterialClass MeshBlending::GetLandscapeMaterialClass(LandscapeMaterialKind a_kind)
{
	switch (a_kind) {
	case LandscapeMaterialKind::kSoft:
	case LandscapeMaterialKind::kSnow:
	case LandscapeMaterialKind::kDirt:
	case LandscapeMaterialKind::kAsh:
	case LandscapeMaterialKind::kGravel:
	case LandscapeMaterialKind::kMud:
	case LandscapeMaterialKind::kSand:
	case LandscapeMaterialKind::kGrass:
	case LandscapeMaterialKind::kGroundCover:
		return LandscapeMaterialClass::kSoft;
	case LandscapeMaterialKind::kHard:
	case LandscapeMaterialKind::kIce:
	case LandscapeMaterialKind::kStone:
	case LandscapeMaterialKind::kBoulder:
	case LandscapeMaterialKind::kWood:
	case LandscapeMaterialKind::kGlass:
	case LandscapeMaterialKind::kMetal:
		return LandscapeMaterialClass::kHard;
	default:
		return LandscapeMaterialClass::kUnknown;
	}
}

void MeshBlending::RebuildLandscapeRules()
{
	const std::unique_lock rulesLock(landscapeRulesMutex);
	compiledLandscapeForms.clear();
	compiledLandscapeEditorIDs.clear();
	compiledLandscapeDiffusePaths.clear();
	compiledSurfaceMaterials.clear();
	compiledBlendPairs.clear();
	for (const auto& assignment : landscapeAssignments) {
		const auto kind = ParseLandscapeMaterialKind(assignment.Kind);
		if (!kind) {
			continue;
		}
		if (!assignment.Form.empty()) {
			if (auto* form = ResolveLandscapeForm(assignment.Form)) {
				compiledLandscapeForms.insert_or_assign(form->GetFormID(), *kind);
			}
		}
		if (!assignment.EditorID.empty()) {
			compiledLandscapeEditorIDs.insert_or_assign(NormalizeIdentity(assignment.EditorID), *kind);
		}
		if (!assignment.Diffuse.empty()) {
			compiledLandscapeDiffusePaths.insert_or_assign(NormalizeTexturePath(assignment.Diffuse), *kind);
		}
	}
	for (const auto& rule : surfaceMaterialRules) {
		const auto kind = ParseLandscapeMaterialKind(rule.Kind);
		const auto material = NormalizeIdentity(rule.Material);
		if (kind && !material.empty()) {
			compiledSurfaceMaterials.insert_or_assign(material, *kind);
		}
	}

	constexpr auto firstKind = static_cast<std::uint8_t>(LandscapeMaterialKind::kSoft);
	constexpr auto lastKind = static_cast<std::uint8_t>(LandscapeMaterialKind::kMetal);
	auto patternMatches = [&](LandscapeMaterialKind a_actual, LandscapeMaterialKind a_pattern) {
		return a_actual == a_pattern ||
		       (a_pattern == LandscapeMaterialKind::kSoft && GetLandscapeMaterialClass(a_actual) == LandscapeMaterialClass::kSoft) ||
		       (a_pattern == LandscapeMaterialKind::kHard && GetLandscapeMaterialClass(a_actual) == LandscapeMaterialClass::kHard);
	};
	auto expandPair = [&](LandscapeMaterialKind a_sourcePattern, LandscapeMaterialKind a_receiverPattern) {
		for (auto sourceValue = firstKind; sourceValue <= lastKind; ++sourceValue) {
			const auto source = static_cast<LandscapeMaterialKind>(sourceValue);
			if (GetLandscapeMaterialClass(source) == LandscapeMaterialClass::kUnknown || !patternMatches(source, a_sourcePattern)) {
				continue;
			}
			for (auto receiverValue = firstKind; receiverValue <= lastKind; ++receiverValue) {
				const auto receiver = static_cast<LandscapeMaterialKind>(receiverValue);
				if (GetLandscapeMaterialClass(receiver) != LandscapeMaterialClass::kUnknown && patternMatches(receiver, a_receiverPattern)) {
					compiledBlendPairs.emplace(source, receiver);
				}
			}
		}
	};
	if (blendPairsExplicit) {
		for (const auto& rule : blendPairRules) {
			const auto source = ParseLandscapeMaterialKind(rule.Source);
			const auto receiver = ParseLandscapeMaterialKind(rule.Receiver);
			if (source && receiver) {
				expandPair(*source, *receiver);
			}
		}
	} else {
		expandPair(LandscapeMaterialKind::kSoft, LandscapeMaterialKind::kHard);
		expandPair(LandscapeMaterialKind::kSoft, LandscapeMaterialKind::kSoft);
		expandPair(LandscapeMaterialKind::kHard, LandscapeMaterialKind::kHard);
	}
	InvalidateLandscapeRegistry();
}

void MeshBlending::InvalidateLandscapeRegistry()
{
	std::scoped_lock lock(landscapeRegistryMutex);
	if (landscapePolicyGeneration.fetch_add(1u, std::memory_order_acq_rel) == std::numeric_limits<std::uint32_t>::max()) {
		landscapePolicyGeneration.store(1u, std::memory_order_release);
	}
	for (auto& entry : landscapeRegistry) {
		entry.sequence.fetch_add(1u, std::memory_order_acq_rel);
		entry.geometry.store(0u, std::memory_order_relaxed);
		entry.shaderProperty.store(0u, std::memory_order_relaxed);
		entry.material.store(0u, std::memory_order_relaxed);
		entry.packedClasses.store(0u, std::memory_order_relaxed);
		entry.policy.store(0u, std::memory_order_relaxed);
		entry.writeSerial.store(0u, std::memory_order_relaxed);
		entry.sequence.fetch_add(1u, std::memory_order_release);
	}
	landscapeRegistryWriteSerial.store(1u, std::memory_order_relaxed);
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
		maximumGap,
		settings.Enabled != 0u && settings.LandscapeLayerBlending != 0u ? 1u : 0u,
		IsFinite(settings.ProjectedSnowEdgeWidth) ? std::clamp(settings.ProjectedSnowEdgeWidth, 0.0f, 16.0f) : 2.0f,
		0.0f,
		0.0f
	};
}

bool MeshBlending::IsRuntimeEnabled() const
{
	if (!loaded || !ruleFileMutationAllowed.load(std::memory_order_acquire) ||
		!globals::shaderCache || !globals::shaderCache->IsEnabled() || settings.Enabled == 0u ||
		!IsFinite(settings.BlendStrength) || settings.BlendStrength <= 0.0f) {
		return false;
	}
	if (settings.LandscapeLayerBlending != 0u) {
		return true;
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
	return loaded && ruleFileMutationAllowed.load(std::memory_order_acquire) &&
	       discoveryCaptureEnabled.load(std::memory_order_relaxed);
}

bool MeshBlending::IsDiscoveryCaptureSaturated() const
{
	std::scoped_lock lock(discoveryMutex);
	return discoveredRuleKeys.size() >= kMaximumRules &&
	       discoveredLandscapeIdentities.size() >= kMaximumLandscapeIdentities;
}

void MeshBlending::SetDiscoveryCaptureEnabled(bool a_enabled)
{
	if (a_enabled && !ruleFileMutationAllowed.load(std::memory_order_acquire)) {
		discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
		return;
	}
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
	std::scoped_lock lock(discoveryMutex);
	if (!discoveryCaptureEnabled.load(std::memory_order_relaxed) ||
		!ruleFileMutationAllowed.load(std::memory_order_acquire)) {
		return;
	}
	if (a_modelPath.empty() || a_nodePath.empty() ||
		!IsSafeRuleString(a_modelPath, kMaximumRuleLength) ||
		!IsSafeRuleString(a_nodePath, kMaximumRuleLength) ||
		a_modelPath.find_first_of("*?") != std::string_view::npos ||
		a_nodePath.find_first_of("*?") != std::string_view::npos) {
		++discoveryDropped;
		return;
	}

	const std::pair key{ std::string(a_modelPath), std::string(a_nodePath) };
	if (discoveredRuleKeys.contains(key)) {
		++discoveryDuplicateObservations;
		return;
	}
	if (discoveredRuleKeys.size() >= kMaximumRules) {
		++discoveryDropped;
		if (discoveredLandscapeIdentities.size() >= kMaximumLandscapeIdentities) {
			discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
		}
		return;
	}
	discoveredRuleKeys.insert(key);
	if (discoveredRuleKeys.size() >= kMaximumRules &&
		discoveredLandscapeIdentities.size() >= kMaximumLandscapeIdentities) {
		discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
	}
}

void MeshBlending::CaptureDiscoveredLandscape(RE::TESLandTexture* a_texture, LandscapeMaterialKind a_kind)
{
	if (!a_texture || !discoveryCaptureEnabled.load(std::memory_order_relaxed) ||
		!ruleFileMutationAllowed.load(std::memory_order_acquire)) {
		return;
	}

	LandscapeDiagnostic diagnostic;
	diagnostic.Kind = CanonicalLandscapeMaterialKind(a_kind);
	if (a_texture->GetFormID() != 0u) {
		diagnostic.Form = Util::GetFormFileKey(a_texture);
		if (diagnostic.Form == "Invalid" || !IsSafeRuleString(diagnostic.Form, kMaximumRuleLength)) {
			diagnostic.Form.clear();
		}
	}
	if (const char* editorID = a_texture->GetFormEditorID(); editorID && *editorID) {
		diagnostic.EditorID = editorID;
	}
	if (!IsSafeRuleString(diagnostic.EditorID, kMaximumRuleLength)) {
		diagnostic.EditorID.clear();
	}
	diagnostic.Diffuse = GetLandscapeDiffusePath(a_texture);
	if (!IsSafeRuleString(diagnostic.Diffuse, kMaximumRuleLength)) {
		diagnostic.Diffuse.clear();
	}
	if (const auto* material = a_texture->materialType) {
		diagnostic.Material = Util::GetFormFileKey(material);
		if (diagnostic.Material == "Invalid" || !IsSafeRuleString(diagnostic.Material, kMaximumRuleLength)) {
			diagnostic.Material.clear();
		}
		if (diagnostic.Material.empty()) {
			if (const char* editorID = material->GetFormEditorID(); editorID && *editorID) {
				diagnostic.Material = editorID;
			} else if (const char* name = material->materialName.c_str(); name && *name) {
				diagnostic.Material = name;
			}
			if (!IsSafeRuleString(diagnostic.Material, kMaximumRuleLength)) {
				diagnostic.Material.clear();
			}
		}
	}

	const auto key = BuildLandscapeDiagnosticKey(
		diagnostic.Form,
		diagnostic.EditorID,
		diagnostic.Diffuse);
	std::scoped_lock lock(discoveryMutex);
	if (!discoveryCaptureEnabled.load(std::memory_order_relaxed) ||
		!ruleFileMutationAllowed.load(std::memory_order_acquire)) {
		return;
	}
	if (key.empty()) {
		++landscapeDiscoveryDropped;
		return;
	}
	if (auto found = discoveredLandscapeIdentities.find(key); found != discoveredLandscapeIdentities.end()) {
		found->second = std::move(diagnostic);
		++landscapeDiscoveryDuplicateObservations;
		return;
	}
	if (discoveredLandscapeIdentities.size() >= kMaximumLandscapeIdentities) {
		++landscapeDiscoveryDropped;
		if (discoveredRuleKeys.size() >= kMaximumRules) {
			discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
		}
		return;
	}
	discoveredLandscapeIdentities.emplace(key, std::move(diagnostic));
	if (discoveredLandscapeIdentities.size() >= kMaximumLandscapeIdentities &&
		discoveredRuleKeys.size() >= kMaximumRules) {
		discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
	}
}

MeshBlending::LandscapeMaterialKind MeshBlending::ClassifySurfaceMaterial(const RE::BGSMaterialType* a_material) const
{
	std::array<const RE::BGSMaterialType*, kMaximumMaterialParentDepth> chain{};
	std::size_t chainLength = 0u;
	for (auto* current = a_material; current;) {
		if (chainLength >= chain.size() ||
			std::find(chain.begin(), chain.begin() + chainLength, current) != chain.begin() + chainLength) {
			// A corrupt or unexpectedly deep parent graph must not turn a heuristic
			// substring into a material classification.
			return LandscapeMaterialKind::kUnknown;
		}
		chain[chainLength++] = current;
		current = current->parentType;
	}

	auto findExactRule = [&](std::string_view a_identity) -> std::optional<LandscapeMaterialKind> {
		if (a_identity.empty()) {
			return std::nullopt;
		}
		const auto found = compiledSurfaceMaterials.find(NormalizeIdentity(a_identity));
		return found != compiledSurfaceMaterials.end() ? std::optional(found->second) : std::nullopt;
	};
	for (std::size_t index = 0u; index < chainLength; ++index) {
		const auto* material = chain[index];
		const auto formKey = Util::GetFormFileKey(material);
		if (formKey != "Invalid") {
			if (const auto kind = findExactRule(formKey)) {
				return *kind;
			}
		}
		if (const char* editorID = material->GetFormEditorID(); editorID && *editorID) {
			if (const auto kind = findExactRule(editorID)) {
				return *kind;
			}
		}
		if (const char* name = material->materialName.c_str(); name && *name) {
			if (const auto kind = findExactRule(name)) {
				return *kind;
			}
		}
	}

	auto classifyName = [](std::string_view a_name) {
		const auto name = NormalizeIdentity(a_name);
		auto contains = [&](std::string_view a_token) { return name.find(a_token) != std::string::npos; };
		// Deposited material takes precedence for compound engine material names
		// such as stone-with-snow.
		if (contains("snow")) {
			return LandscapeMaterialKind::kSnow;
		}
		if (contains("ash")) {
			return LandscapeMaterialKind::kAsh;
		}
		if (contains("dirt") || contains("soil") || contains("earth")) {
			return LandscapeMaterialKind::kDirt;
		}
		if (contains("gravel")) {
			return LandscapeMaterialKind::kGravel;
		}
		if (contains("mud")) {
			return LandscapeMaterialKind::kMud;
		}
		if (contains("sandstone")) {
			return LandscapeMaterialKind::kStone;
		}
		if (contains("sand")) {
			return LandscapeMaterialKind::kSand;
		}
		if (contains("grass")) {
			return LandscapeMaterialKind::kGrass;
		}
		if (contains("groundcover") || contains("ground cover") || contains("moss") || contains("lichen")) {
			return LandscapeMaterialKind::kGroundCover;
		}
		if (contains("ice")) {
			return LandscapeMaterialKind::kIce;
		}
		if (contains("boulder")) {
			return LandscapeMaterialKind::kBoulder;
		}
		if (contains("stone") || contains("rock")) {
			return LandscapeMaterialKind::kStone;
		}
		if (contains("wood") || contains("timber")) {
			return LandscapeMaterialKind::kWood;
		}
		if (contains("glass")) {
			return LandscapeMaterialKind::kGlass;
		}
		if (contains("metal") || contains("iron") || contains("steel") || contains("copper") || contains("bronze")) {
			return LandscapeMaterialKind::kMetal;
		}
		// Generic fallbacks must follow concrete hard materials so compound names
		// such as SoftWood and SoftStone retain their physical material class.
		if (contains("soft")) {
			return LandscapeMaterialKind::kSoft;
		}
		if (contains("hard")) {
			return LandscapeMaterialKind::kHard;
		}
		return LandscapeMaterialKind::kUnknown;
	};

	for (std::size_t index = 0u; index < chainLength; ++index) {
		const auto* material = chain[index];
		if (const char* editorID = material->GetFormEditorID(); editorID && *editorID) {
			const auto kind = classifyName(editorID);
			if (kind != LandscapeMaterialKind::kUnknown) {
				return kind;
			}
		}
		if (const char* name = material->materialName.c_str(); name && *name) {
			const auto kind = classifyName(name);
			if (kind != LandscapeMaterialKind::kUnknown) {
				return kind;
			}
		}
	}
	return LandscapeMaterialKind::kUnknown;
}

MeshBlending::LandscapeMaterialKind MeshBlending::ClassifyLandscapeTexture(RE::TESLandTexture* a_texture) const
{
	if (!a_texture) {
		return LandscapeMaterialKind::kUnknown;
	}
	if (const auto found = compiledLandscapeForms.find(a_texture->GetFormID()); found != compiledLandscapeForms.end()) {
		return found->second;
	}
	if (const char* editorID = a_texture->GetFormEditorID(); editorID && *editorID) {
		const auto found = compiledLandscapeEditorIDs.find(NormalizeIdentity(editorID));
		if (found != compiledLandscapeEditorIDs.end()) {
			return found->second;
		}
	}
	if (const auto diffuse = GetLandscapeDiffusePath(a_texture); !diffuse.empty()) {
		const auto found = compiledLandscapeDiffusePaths.find(diffuse);
		if (found != compiledLandscapeDiffusePaths.end()) {
			return found->second;
		}
	}
	return ClassifySurfaceMaterial(a_texture->materialType);
}

void MeshBlending::StoreLandscapeClasses(
	RE::BSGeometry* a_geometry,
	RE::BSLightingShaderProperty* a_shaderProperty,
	RE::BSShaderMaterial* a_material,
	std::uint32_t a_packedClasses)
{
	if (!a_geometry || !a_shaderProperty || !a_material) {
		return;
	}
	const auto geometry = reinterpret_cast<std::uintptr_t>(a_geometry);
	const auto shaderProperty = reinterpret_cast<std::uintptr_t>(a_shaderProperty);
	const auto material = reinterpret_cast<std::uintptr_t>(a_material);
	const auto mixedKey = geometry ^ std::rotl(shaderProperty, 17) ^ std::rotl(material, 33);
	const std::size_t setStart = ((mixedKey >> 4u) & (kLandscapeRegistrySetCount - 1u)) * kLandscapeRegistryWays;

	std::scoped_lock lock(landscapeRegistryMutex);
	std::size_t replacement = setStart;
	std::uint64_t oldestSerial = std::numeric_limits<std::uint64_t>::max();
	for (std::size_t way = 0u; way < kLandscapeRegistryWays; ++way) {
		const auto index = setStart + way;
		auto& entry = landscapeRegistry[index];
		if (entry.geometry.load(std::memory_order_relaxed) == geometry &&
			entry.shaderProperty.load(std::memory_order_relaxed) == shaderProperty &&
			entry.material.load(std::memory_order_relaxed) == material) {
			replacement = index;
			break;
		}
		const auto serial = entry.writeSerial.load(std::memory_order_relaxed);
		if (serial < oldestSerial) {
			oldestSerial = serial;
			replacement = index;
		}
	}

	auto& entry = landscapeRegistry[replacement];
	entry.sequence.fetch_add(1u, std::memory_order_acq_rel);
	entry.geometry.store(geometry, std::memory_order_relaxed);
	entry.shaderProperty.store(shaderProperty, std::memory_order_relaxed);
	entry.material.store(material, std::memory_order_relaxed);
	entry.packedClasses.store(a_packedClasses, std::memory_order_relaxed);
	entry.policy.store(landscapePolicyGeneration.load(std::memory_order_acquire), std::memory_order_relaxed);
	entry.writeSerial.store(landscapeRegistryWriteSerial.fetch_add(1u, std::memory_order_relaxed), std::memory_order_relaxed);
	entry.sequence.fetch_add(1u, std::memory_order_release);
}

bool MeshBlending::TryGetLandscapeClasses(RE::BSRenderPass* a_pass, std::uint32_t& a_packedClasses)
{
	a_packedClasses = 0u;
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty) {
		return false;
	}
	auto& runtimeData = a_pass->geometry->GetGeometryRuntimeData();
	auto* property = runtimeData.shaderProperty.get();
	if (property != a_pass->shaderProperty ||
		property->GetRTTI() != globals::rtti::BSLightingShaderPropertyRTTI.get()) {
		return false;
	}
	auto* lightingProperty = static_cast<RE::BSLightingShaderProperty*>(property);
	auto* shaderMaterial = lightingProperty->material;
	if (!shaderMaterial) {
		return false;
	}

	const auto geometry = reinterpret_cast<std::uintptr_t>(a_pass->geometry);
	const auto shaderProperty = reinterpret_cast<std::uintptr_t>(lightingProperty);
	const auto material = reinterpret_cast<std::uintptr_t>(shaderMaterial);
	const auto mixedKey = geometry ^ std::rotl(shaderProperty, 17) ^ std::rotl(material, 33);
	const std::size_t setStart = ((mixedKey >> 4u) & (kLandscapeRegistrySetCount - 1u)) * kLandscapeRegistryWays;
	const auto expectedPolicy = landscapePolicyGeneration.load(std::memory_order_acquire);
	for (std::size_t way = 0u; way < kLandscapeRegistryWays; ++way) {
		auto& entry = landscapeRegistry[setStart + way];
		const auto sequenceBefore = entry.sequence.load(std::memory_order_acquire);
		if ((sequenceBefore & 1u) != 0u) {
			continue;
		}
		const auto storedGeometry = entry.geometry.load(std::memory_order_relaxed);
		const auto storedProperty = entry.shaderProperty.load(std::memory_order_relaxed);
		const auto storedMaterial = entry.material.load(std::memory_order_relaxed);
		const auto storedClasses = entry.packedClasses.load(std::memory_order_relaxed);
		const auto storedPolicy = entry.policy.load(std::memory_order_relaxed);
		const auto sequenceAfter = entry.sequence.load(std::memory_order_acquire);
		if (sequenceBefore != sequenceAfter || (sequenceAfter & 1u) != 0u) {
			continue;
		}
		if (storedGeometry == geometry && storedProperty == shaderProperty && storedMaterial == material &&
			storedPolicy == expectedPolicy) {
			a_packedClasses = storedClasses;
			return true;
		}
	}
	return false;
}

void MeshBlending::CaptureLandscapeMaterials(RE::TESObjectLAND* a_land)
{
	if (!a_land || !a_land->loadedData) {
		return;
	}
	const std::shared_lock rulesLock(landscapeRulesMutex);
	static const auto defaultLandTextureAddress = REL::Relocation<RE::TESLandTexture**>(RELOCATION_ID(514783, 400936));
	auto* defaultLandTexture = *defaultLandTextureAddress;
	constexpr auto classMask = static_cast<std::uint32_t>(State::ExtraFeatureDescriptors::MeshBlendingLandscapeClasses);

	for (std::size_t quadrant = 0u; quadrant < 4u; ++quadrant) {
		auto* mesh = a_land->loadedData->mesh[quadrant];
		if (!mesh) {
			continue;
		}
		const auto& children = mesh->GetChildren();
		auto* geometry = children.empty() || !children[0] ? nullptr : children[0]->AsGeometry();
		if (!geometry) {
			continue;
		}
		auto* property = geometry->GetGeometryRuntimeData().shaderProperty.get();
		if (!property || property->GetRTTI() != globals::rtti::BSLightingShaderPropertyRTTI.get()) {
			continue;
		}
		auto* lightingProperty = static_cast<RE::BSLightingShaderProperty*>(property);
		if (!lightingProperty->material) {
			continue;
		}

		std::array<RE::TESLandTexture*, State::MeshBlendingLandscapeClassCount> textures{};
		std::array<LandscapeMaterialKind, State::MeshBlendingLandscapeClassCount> kinds{};
		auto* defaultTexture = a_land->loadedData->defQuadTextures[quadrant];
		textures[0] = !defaultTexture || defaultTexture->GetFormID() == 0u ? defaultLandTexture : defaultTexture;
		for (std::size_t layer = 1u; layer < textures.size(); ++layer) {
			auto* overlay = a_land->loadedData->quadTextures[quadrant][layer - 1u];
			textures[layer] = overlay && overlay->GetFormID() == 0u ? defaultLandTexture : overlay;
		}

		std::uint32_t packedClasses = 0u;
		for (std::size_t layer = 0u; layer < textures.size(); ++layer) {
			if (!textures[layer]) {
				continue;
			}
			kinds[layer] = ClassifyLandscapeTexture(textures[layer]);
			CaptureDiscoveredLandscape(textures[layer], kinds[layer]);
			const auto materialClass = static_cast<std::uint32_t>(GetLandscapeMaterialClass(kinds[layer]));
			packedClasses |= materialClass <<
			                 (State::MeshBlendingLandscapeClassShift + State::MeshBlendingLandscapeClassBits * layer);
		}

		bool allPairsAllowed = true;
		for (std::size_t first = 0u; first < kinds.size() && allPairsAllowed; ++first) {
			const auto firstClass = GetLandscapeMaterialClass(kinds[first]);
			if (!textures[first] || firstClass == LandscapeMaterialClass::kUnknown) {
				continue;
			}
			for (std::size_t second = first + 1u; second < kinds.size(); ++second) {
				const auto secondClass = GetLandscapeMaterialClass(kinds[second]);
				if (!textures[second] || secondClass == LandscapeMaterialClass::kUnknown) {
					continue;
				}
				LandscapeMaterialKind source = kinds[first];
				LandscapeMaterialKind receiver = kinds[second];
				if (firstClass == LandscapeMaterialClass::kHard && secondClass == LandscapeMaterialClass::kSoft) {
					std::swap(source, receiver);
				}
				const bool sameClass = firstClass == secondClass;
				if (!compiledBlendPairs.contains({ source, receiver }) &&
					(!sameClass || !compiledBlendPairs.contains({ receiver, source }))) {
					allPairsAllowed = false;
					break;
				}
			}
		}
		if (!allPairsAllowed) {
			packedClasses = 0u;
		}
		StoreLandscapeClasses(geometry, lightingProperty, lightingProperty->material, packedClasses & classMask);
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
	discoveryCaptureEnabled.store(false, std::memory_order_release);
	ruleFileMutationAllowed.store(false, std::memory_order_release);
	{
		// Drain an in-flight render-thread recorder before policy is replaced.
		std::scoped_lock lock(discoveryMutex);
	}

	RuleFileState state;
	bool notFound = false;
	std::string error;
	if (!ReadRuleFileState(state, notFound, error)) {
		ruleFileLoadError = std::move(error);
		logger::warn("[Mesh Blending] {} ({})", ruleFileLoadError, GetRuleFilePath().string());
		return false;
	}

	ApplyRuleFileState(std::move(state));
	ruleFileLoadError.clear();
	ruleFileMutationAllowed.store(true, std::memory_order_release);
	logger::info(
		"[Mesh Blending] Loaded {} manual, {} detected, {} deny, and {} LTEX assignment rules{}",
		allowList.size(),
		detectedAllowList.size(),
		denyList.size(),
		landscapeAssignments.size(),
		notFound ? " (new rule file)" : "");
	return true;
}

bool MeshBlending::ReadRuleFileState(
	RuleFileState& a_state,
	bool& a_notFound,
	std::string& a_error) const
{
	a_state = {};
	a_notFound = false;
	json ruleFile;
	const auto readResult = Util::FileHelpers::ReadJsonFile(GetRuleFilePath(), ruleFile, a_error);
	if (readResult == Util::FileHelpers::JsonFileReadResult::NotFound) {
		a_notFound = true;
		a_state.document = json::object({ { "SchemaVersion", kRuleFileSchemaVersion } });
		a_error.clear();
		return true;
	}
	if (readResult == Util::FileHelpers::JsonFileReadResult::Error) {
		a_error = std::format("Could not read the rule file: {}", a_error);
		return false;
	}
	if (!ruleFile.is_object()) {
		a_error = "The rule file root must be a JSON object.";
		return false;
	}
	if (!ruleFile.contains("SchemaVersion") ||
		(!ruleFile["SchemaVersion"].is_number_unsigned() && !ruleFile["SchemaVersion"].is_number_integer())) {
		a_error = "The rule file has no integer SchemaVersion.";
		return false;
	}

	try {
		const auto schemaVersion = ruleFile.at("SchemaVersion").get<std::int64_t>();
		if (schemaVersion < static_cast<std::int64_t>(kOldestSupportedRuleFileSchemaVersion) ||
			schemaVersion > static_cast<std::int64_t>(kRuleFileSchemaVersion)) {
			a_error = std::format("Unsupported MeshBlendingRules.json schema {}.", schemaVersion);
			return false;
		}

		a_state.document = ruleFile;
		auto readString = [&](const json& a_object,
							  std::string_view a_context,
							  const char* a_name,
							  std::string& a_destination,
							  bool a_required) {
			if (!a_object.contains(a_name)) {
				if (a_required) {
					a_error = std::format("{}.{} is required.", a_context, a_name);
					return false;
				}
				a_destination.clear();
				return true;
			}
			if (!a_object.at(a_name).is_string()) {
				a_error = std::format("{}.{} must be a string.", a_context, a_name);
				return false;
			}
			a_destination = a_object.at(a_name).get<std::string>();
			if (!IsSafeRuleString(a_destination, kMaximumRuleLength)) {
				a_error = std::format("{}.{} is not a bounded UTF-8 string.", a_context, a_name);
				return false;
			}
			return true;
		};

		auto parseOverrideRules = [&](const char* a_name, std::vector<OverrideRule>& a_destination, bool a_exact) {
			if (!ruleFile.contains(a_name)) {
				return true;
			}
			const auto& array = ruleFile.at(a_name);
			if (!array.is_array() || array.size() > kMaximumRules) {
				a_error = std::format("{} must be an array with at most {} entries.", a_name, kMaximumRules);
				return false;
			}
			for (std::size_t index = 0u; index < array.size(); ++index) {
				const auto& item = array[index];
				const auto context = std::format("{}[{}]", a_name, index);
				if (!item.is_object()) {
					a_error = std::format("{} must be an object.", context);
					return false;
				}
				OverrideRule rule;
				if (!readString(item, context, "Model", rule.Model, false) ||
					!readString(item, context, "NodePath", rule.NodePath, false) ||
					(rule.Model.empty() && rule.NodePath.empty())) {
					if (a_error.empty()) {
						a_error = std::format("{} must identify a model or node.", context);
					}
					return false;
				}
				if (a_exact && (rule.Model.empty() || rule.NodePath.empty() || HasWildcard(rule.Model) || HasWildcard(rule.NodePath))) {
					a_error = std::format("{} must be an exact model/node pair.", context);
					return false;
				}
				if (a_exact) {
					rule.Model = NormalizePath(rule.Model, true);
					rule.NodePath = NormalizePath(rule.NodePath, false);
				}
				a_destination.push_back(std::move(rule));
			}
			return true;
		};

		if (!parseOverrideRules("AllowList", a_state.allowList, false) ||
			!parseOverrideRules("DetectedAllowList", a_state.detectedAllowList, true) ||
			!parseOverrideRules("DenyList", a_state.denyList, false)) {
			return false;
		}
		std::sort(a_state.detectedAllowList.begin(), a_state.detectedAllowList.end(), [](const auto& a_left, const auto& a_right) {
			return std::tie(a_left.Model, a_left.NodePath) < std::tie(a_right.Model, a_right.NodePath);
		});
		a_state.detectedAllowList.erase(
			std::unique(a_state.detectedAllowList.begin(), a_state.detectedAllowList.end(), [](const auto& a_left, const auto& a_right) {
				return a_left.Model == a_right.Model && a_left.NodePath == a_right.NodePath;
			}),
			a_state.detectedAllowList.end());
		if (a_state.allowList.size() + a_state.detectedAllowList.size() > kMaximumRules) {
			a_error = std::format("AllowList and DetectedAllowList together exceed {} entries.", kMaximumRules);
			return false;
		}

		auto parseLandscapeAssignments = [&]() {
			if (!ruleFile.contains("LandscapeAssignments")) {
				return true;
			}
			const auto& array = ruleFile.at("LandscapeAssignments");
			if (!array.is_array() || array.size() > kMaximumLandscapeIdentities) {
				a_error = std::format("LandscapeAssignments must contain at most {} entries.", kMaximumLandscapeIdentities);
				return false;
			}
			for (std::size_t index = 0u; index < array.size(); ++index) {
				const auto& item = array[index];
				const auto context = std::format("LandscapeAssignments[{}]", index);
				if (!item.is_object()) {
					a_error = std::format("{} must be an object.", context);
					return false;
				}
				LandscapeAssignment assignment;
				if (!readString(item, context, "Kind", assignment.Kind, true) ||
					!readString(item, context, "Form", assignment.Form, false) ||
					!readString(item, context, "EditorID", assignment.EditorID, false) ||
					!readString(item, context, "Diffuse", assignment.Diffuse, false)) {
					return false;
				}
				const auto kind = ParseLandscapeMaterialKind(assignment.Kind);
				if (!kind) {
					a_error = std::format("{}.Kind is not recognized.", context);
					return false;
				}
				assignment.Kind = CanonicalLandscapeMaterialKind(*kind);
				if (!assignment.Form.empty()) {
					if (!IsPortableLandscapeFormKey(assignment.Form)) {
						assignment.Form.clear();
					} else if (auto* form = ResolveLandscapeForm(assignment.Form)) {
						assignment.Form = Util::GetFormFileKey(form);
					}
				}
				assignment.Diffuse = NormalizeTexturePath(assignment.Diffuse);
				if (!assignment.Form.empty() || !assignment.EditorID.empty() || !assignment.Diffuse.empty()) {
					a_state.landscapeAssignments.push_back(std::move(assignment));
				}
			}
			return true;
		};
		if (!parseLandscapeAssignments()) {
			return false;
		}

		if (ruleFile.contains("SurfaceMaterials")) {
			const auto& array = ruleFile.at("SurfaceMaterials");
			if (!array.is_array() || array.size() > kMaximumRules) {
				a_error = std::format("SurfaceMaterials must contain at most {} entries.", kMaximumRules);
				return false;
			}
			for (std::size_t index = 0u; index < array.size(); ++index) {
				const auto& item = array[index];
				const auto context = std::format("SurfaceMaterials[{}]", index);
				SurfaceMaterialRule rule;
				if (!item.is_object() || !readString(item, context, "Material", rule.Material, true) ||
					!readString(item, context, "Kind", rule.Kind, true) || rule.Material.empty()) {
					if (a_error.empty()) {
						a_error = std::format("{} is invalid.", context);
					}
					return false;
				}
				const auto kind = ParseLandscapeMaterialKind(rule.Kind);
				if (!kind) {
					a_error = std::format("{}.Kind is not recognized.", context);
					return false;
				}
				rule.Kind = CanonicalLandscapeMaterialKind(*kind);
				a_state.surfaceMaterialRules.push_back(std::move(rule));
			}
		}

		a_state.blendPairsExplicit = ruleFile.contains("BlendPairs");
		if (a_state.blendPairsExplicit) {
			const auto& array = ruleFile.at("BlendPairs");
			if (!array.is_array() || array.size() > kMaximumRules) {
				a_error = std::format("BlendPairs must contain at most {} entries.", kMaximumRules);
				return false;
			}
			for (std::size_t index = 0u; index < array.size(); ++index) {
				const auto& item = array[index];
				const auto context = std::format("BlendPairs[{}]", index);
				BlendPairRule rule;
				if (!item.is_object()) {
					a_error = std::format("{} must be an object.", context);
					return false;
				}
				if (!readString(item, context, "Source", rule.Source, true) ||
					!readString(item, context, "Receiver", rule.Receiver, true)) {
					return false;
				}
				const auto source = ParseLandscapeMaterialKind(rule.Source);
				const auto receiver = ParseLandscapeMaterialKind(rule.Receiver);
				if (!source || !receiver) {
					a_error = std::format("{} contains an unknown material kind.", context);
					return false;
				}
				const auto sourceClass = GetLandscapeMaterialClass(*source);
				const auto receiverClass = GetLandscapeMaterialClass(*receiver);
				if (sourceClass == LandscapeMaterialClass::kUnknown ||
					receiverClass == LandscapeMaterialClass::kUnknown) {
					a_error = std::format("{} cannot use Unknown as a blend endpoint.", context);
					return false;
				}
				if (sourceClass == LandscapeMaterialClass::kHard &&
					receiverClass == LandscapeMaterialClass::kSoft) {
					a_error = std::format("{} cannot blend a hard source over a soft receiver.", context);
					return false;
				}
				rule.Source = CanonicalLandscapeMaterialKind(*source);
				rule.Receiver = CanonicalLandscapeMaterialKind(*receiver);
				a_state.blendPairRules.push_back(std::move(rule));
			}
		}

		if (ruleFile.contains("DiscoveryDiagnostics")) {
			const auto& array = ruleFile.at("DiscoveryDiagnostics");
			if (!array.is_array() || array.size() > kMaximumLandscapeIdentities) {
				a_error = std::format("DiscoveryDiagnostics must contain at most {} entries.", kMaximumLandscapeIdentities);
				return false;
			}
			std::map<std::string, LandscapeDiagnostic, std::less<>> diagnosticsByIdentity;
			for (std::size_t index = 0u; index < array.size(); ++index) {
				const auto& item = array[index];
				const auto context = std::format("DiscoveryDiagnostics[{}]", index);
				LandscapeDiagnostic diagnostic;
				if (!item.is_object()) {
					a_error = std::format("{} must be an object.", context);
					return false;
				}
				if (!readString(item, context, "Kind", diagnostic.Kind, true) ||
					!readString(item, context, "Form", diagnostic.Form, false) ||
					!readString(item, context, "EditorID", diagnostic.EditorID, false) ||
					!readString(item, context, "Diffuse", diagnostic.Diffuse, false) ||
					!readString(item, context, "Material", diagnostic.Material, false)) {
					return false;
				}
				const auto kind = ParseLandscapeMaterialKind(diagnostic.Kind);
				if (!kind) {
					a_error = std::format("{}.Kind is not recognized.", context);
					return false;
				}
				diagnostic.Kind = CanonicalLandscapeMaterialKind(*kind);
				if (!diagnostic.Form.empty()) {
					if (!IsPortableLandscapeFormKey(diagnostic.Form)) {
						diagnostic.Form.clear();
					} else if (auto* form = ResolveLandscapeForm(diagnostic.Form)) {
						diagnostic.Form = Util::GetFormFileKey(form);
					}
				}
				diagnostic.Diffuse = NormalizeTexturePath(diagnostic.Diffuse);
				if (const auto key = BuildLandscapeDiagnosticKey(diagnostic.Form, diagnostic.EditorID, diagnostic.Diffuse);
					!key.empty()) {
					diagnosticsByIdentity.insert_or_assign(key, std::move(diagnostic));
				}
			}
			a_state.discoveryDiagnostics.reserve(diagnosticsByIdentity.size());
			for (auto& [key, diagnostic] : diagnosticsByIdentity) {
				(void)key;
				a_state.discoveryDiagnostics.push_back(std::move(diagnostic));
			}
		}
	} catch (const json::exception& exception) {
		a_error = std::format("The rule file is invalid: {}", exception.what());
		return false;
	}

	return true;
}

void MeshBlending::ApplyRuleFileState(RuleFileState&& a_state)
{
	const bool landscapePolicyChanged =
		!landscapeRulesInitialized ||
		landscapeAssignments != a_state.landscapeAssignments ||
		surfaceMaterialRules != a_state.surfaceMaterialRules ||
		blendPairRules != a_state.blendPairRules ||
		blendPairsExplicit != a_state.blendPairsExplicit;
	allowList = std::move(a_state.allowList);
	detectedAllowList = std::move(a_state.detectedAllowList);
	denyList = std::move(a_state.denyList);
	landscapeAssignments = std::move(a_state.landscapeAssignments);
	surfaceMaterialRules = std::move(a_state.surfaceMaterialRules);
	blendPairRules = std::move(a_state.blendPairRules);
	savedDiscoveryDiagnostics = std::move(a_state.discoveryDiagnostics);
	blendPairsExplicit = a_state.blendPairsExplicit;
	ruleFileDocument = std::move(a_state.document);
	SanitizeSettings();
	RebuildRules();
	if (landscapePolicyChanged) {
		RebuildLandscapeRules();
		landscapeRulesInitialized = true;
	}
}

bool MeshBlending::WriteRuleFile(const RuleFileState& a_state, std::string& a_error) const
{
	try {
		json ruleFile = a_state.document.is_object() ? a_state.document : json::object();
		ruleFile["SchemaVersion"] = kRuleFileSchemaVersion;
		ruleFile["AllowList"] = a_state.allowList;
		ruleFile["DenyList"] = a_state.denyList;
		if (a_state.detectedAllowList.empty()) {
			ruleFile.erase("DetectedAllowList");
		} else {
			ruleFile["DetectedAllowList"] = a_state.detectedAllowList;
		}

		auto emitLandscapeAssignments = [&]() {
			json array = json::array();
			for (const auto& entry : a_state.landscapeAssignments) {
				array.push_back({
					{ "Kind", entry.Kind },
					{ "Form", entry.Form },
					{ "EditorID", entry.EditorID },
					{ "Diffuse", entry.Diffuse }
				});
			}
			return array;
		};
		auto emitSurfaceMaterials = [&]() {
			json array = json::array();
			for (const auto& entry : a_state.surfaceMaterialRules) {
				array.push_back({ { "Material", entry.Material }, { "Kind", entry.Kind } });
			}
			return array;
		};
		ruleFile["LandscapeAssignments"] = emitLandscapeAssignments();
		ruleFile["SurfaceMaterials"] = emitSurfaceMaterials();

		if (a_state.blendPairsExplicit) {
			json array = json::array();
			for (const auto& entry : a_state.blendPairRules) {
				array.push_back({ { "Source", entry.Source }, { "Receiver", entry.Receiver } });
			}
			ruleFile["BlendPairs"] = std::move(array);
		} else {
			ruleFile.erase("BlendPairs");
		}

		if (a_state.discoveryDiagnostics.empty()) {
			ruleFile.erase("DiscoveryDiagnostics");
		} else {
			json array = json::array();
			for (const auto& entry : a_state.discoveryDiagnostics) {
				array.push_back({
					{ "Kind", entry.Kind },
					{ "Form", entry.Form },
					{ "EditorID", entry.EditorID },
					{ "Diffuse", entry.Diffuse },
					{ "Material", entry.Material }
				});
			}
			ruleFile["DiscoveryDiagnostics"] = std::move(array);
		}
		return Util::FileHelpers::WriteTextFileAtomic(GetRuleFilePath(), ruleFile.dump(1), a_error);
	} catch (const json::exception& exception) {
		a_error = exception.what();
		return false;
	}
}

MeshBlending::DiscoverySaveResult MeshBlending::SaveDiscoveredRules()
{
	DiscoverySaveResult result;
	SetDiscoveryCaptureEnabled(false);
	if (!ruleFileMutationAllowed.exchange(false, std::memory_order_acq_rel)) {
		result.error = std::format("{} The existing file was not changed.", ruleFileLoadError);
		return result;
	}
	std::set<std::pair<std::string, std::string>> discoveredSnapshot;
	std::map<std::string, LandscapeDiagnostic, std::less<>> landscapeSnapshot;
	{
		std::scoped_lock lock(discoveryMutex);
		discoveredSnapshot = discoveredRuleKeys;
		landscapeSnapshot = discoveredLandscapeIdentities;
	}

	// Re-read only for an explicit mutation so edits made by a mod author while
	// the game is running are merged instead of being replaced by stale policy.
	RuleFileState diskState;
	bool notFound = false;
	if (!ReadRuleFileState(diskState, notFound, result.error)) {
		ruleFileLoadError = result.error;
		result.error.append(" The existing file was not changed.");
		return result;
	}
	(void)notFound;
	ApplyRuleFileState(std::move(diskState));

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

	const std::size_t occupiedAllowCapacity = std::min(allowList.size() + detectedAllowList.size(), kMaximumRules);
	const std::size_t remainingCapacity = kMaximumRules - occupiedAllowCapacity;
	if (additions.size() > remainingCapacity) {
		result.capacityRejected = additions.size() - remainingCapacity;
		additions.resize(remainingCapacity);
	}

	auto proposedDetectedAllowList = detectedAllowList;
	proposedDetectedAllowList.insert(proposedDetectedAllowList.end(), additions.begin(), additions.end());
	std::sort(proposedDetectedAllowList.begin(), proposedDetectedAllowList.end(), [](const auto& a_left, const auto& a_right) {
		return std::tie(a_left.Model, a_left.NodePath) < std::tie(a_right.Model, a_right.NodePath);
	});
	proposedDetectedAllowList.erase(
		std::unique(proposedDetectedAllowList.begin(), proposedDetectedAllowList.end(), [](const auto& a_left, const auto& a_right) {
			return a_left.Model == a_right.Model && a_left.NodePath == a_right.NodePath;
		}),
		proposedDetectedAllowList.end());

	std::map<std::string, LandscapeDiagnostic, std::less<>> mergedLandscape;
	for (const auto& diagnostic : savedDiscoveryDiagnostics) {
		const auto key = BuildLandscapeDiagnosticKey(diagnostic.Form, diagnostic.EditorID, diagnostic.Diffuse);
		if (!key.empty()) {
			mergedLandscape[key] = diagnostic;
		}
	}
	for (const auto& [key, diagnostic] : landscapeSnapshot) {
		if (key.empty()) {
			continue;
		}
		if (mergedLandscape.contains(key)) {
			mergedLandscape[key] = diagnostic;
			++result.landscapeRefreshed;
		} else if (mergedLandscape.size() < kMaximumLandscapeIdentities) {
			mergedLandscape.emplace(key, diagnostic);
			++result.landscapeAdded;
		} else {
			++result.landscapeCapacityRejected;
		}
	}

	RuleFileState proposed;
	proposed.allowList = allowList;
	proposed.detectedAllowList = std::move(proposedDetectedAllowList);
	proposed.denyList = denyList;
	proposed.landscapeAssignments = landscapeAssignments;
	proposed.surfaceMaterialRules = surfaceMaterialRules;
	proposed.blendPairRules = blendPairRules;
	proposed.blendPairsExplicit = blendPairsExplicit;
	proposed.document = ruleFileDocument;
	proposed.discoveryDiagnostics.reserve(mergedLandscape.size());
	for (auto& [key, diagnostic] : mergedLandscape) {
		(void)key;
		proposed.discoveryDiagnostics.push_back(std::move(diagnostic));
	}
	if (!WriteRuleFile(proposed, result.error)) {
		ruleFileMutationAllowed.store(true, std::memory_order_release);
		return result;
	}

	ApplyRuleFileState(std::move(proposed));
	result.added = additions.size();
	result.success = true;
	ruleFileLoadError.clear();
	ruleFileMutationAllowed.store(true, std::memory_order_release);
	ClearDiscoveredRules();
	logger::info(
		"[Mesh Blending] Saved {} NIF and {} new LTEX identities ({} LTEX refreshed) to {}",
		result.added,
		result.landscapeAdded,
		result.landscapeRefreshed,
		GetRuleFilePath().string());
	return result;
}

bool MeshBlending::ClearSavedRules(std::string& a_error)
{
	SetDiscoveryCaptureEnabled(false);
	if (!ruleFileMutationAllowed.exchange(false, std::memory_order_acq_rel)) {
		a_error = std::format("{} The existing file was not changed.", ruleFileLoadError);
		return false;
	}
	{
		std::scoped_lock lock(discoveryMutex);
	}

	RuleFileState diskState;
	bool notFound = false;
	if (!ReadRuleFileState(diskState, notFound, a_error)) {
		ruleFileLoadError = a_error;
		a_error.append(" The existing file was not changed.");
		return false;
	}
	(void)notFound;
	diskState.detectedAllowList.clear();
	diskState.discoveryDiagnostics.clear();
	if (!WriteRuleFile(diskState, a_error)) {
		ruleFileMutationAllowed.store(true, std::memory_order_release);
		return false;
	}
	ApplyRuleFileState(std::move(diskState));
	ClearDiscoveredRules();
	ruleFileLoadError.clear();
	ruleFileMutationAllowed.store(true, std::memory_order_release);
	logger::info("[Mesh Blending] Cleared generated material identities in {}", GetRuleFilePath().string());
	return true;
}

void MeshBlending::ClearDiscoveredRules()
{
	std::scoped_lock lock(discoveryMutex);
	discoveredRuleKeys.clear();
	discoveredLandscapeIdentities.clear();
	discoveryDuplicateObservations = 0u;
	discoveryDropped = 0u;
	landscapeDiscoveryDuplicateObservations = 0u;
	landscapeDiscoveryDropped = 0u;
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
	signature.owner = reinterpret_cast<std::uintptr_t>(a_source.geometry->GetUserData());
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

MeshBlending::Classification MeshBlending::GetSourceClassification(
	SourceState& a_source,
	std::uint32_t a_frame,
	bool a_captureDiscovery,
	bool a_collectDiagnostics)
{
	Signature signature = BuildSourceSignature(a_source);
	const bool sourceStateCacheAllowed =
		compiledAllowList.empty() && compiledDenyList.empty() &&
		compiledExactAllowRules.empty() && compiledExactDenyRules.empty();
	Classification classification = Classification::kRejected;
	bool cacheHit = sourceStateCacheAllowed &&
	                TryGetCachedClassification(signature, a_frame, true, classification);
	if (cacheHit && a_collectDiagnostics) {
		++diagnostics.preOwnerCacheHits;
	}
	if (!cacheHit) {
		if (a_collectDiagnostics) {
			++diagnostics.ownerResolutionAttempts;
		}
		if (!ResolveStaticOwner(a_source)) {
			if (a_collectDiagnostics) {
				++diagnostics.sourceRejects;
				if (sourceStateCacheAllowed) {
					++diagnostics.cacheMisses;
				}
			}
			if (sourceStateCacheAllowed) {
				StoreClassification(signature, a_frame, Classification::kRejected);
			}
			return Classification::kRejected;
		}
		CompleteOwnershipSignature(a_source, signature);
		if (!sourceStateCacheAllowed) {
			cacheHit = TryGetCachedClassification(signature, a_frame, false, classification);
			if (cacheHit && a_collectDiagnostics) {
				++diagnostics.fullSignatureCacheHits;
			}
		}
	}
	if (cacheHit) {
		if (a_collectDiagnostics) {
			++diagnostics.cacheHits;
		}
	} else {
		if (a_collectDiagnostics) {
			++diagnostics.cacheMisses;
		}
		classification = ClassifyOnCacheMiss(a_source, a_captureDiscovery);
		StoreClassification(signature, a_frame, classification);
	}
	if (a_collectDiagnostics) {
		if (classification == Classification::kAllowedByRule) {
			++diagnostics.ruleAccepts;
		} else if (classification == Classification::kAutomatic) {
			++diagnostics.automaticAccepts;
		}
	}
	return classification;
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
	bool captureHasCapacity = false;
	if (a_captureDiscovery) {
		std::scoped_lock lock(discoveryMutex);
		captureHasCapacity = discoveredRuleKeys.size() < kMaximumRules;
		if (!captureHasCapacity && discoveredLandscapeIdentities.size() >= kMaximumLandscapeIdentities) {
			discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
		}
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

bool MeshBlending::TryPrepareProjectedSnow(RE::BSRenderPass* a_pass)
{
	auto* state = globals::state;
	if (!state || !a_pass || !a_pass->geometry || !a_pass->shaderProperty) {
		return false;
	}

	const bool discoveryCaptureActive = IsDiscoveryCaptureActive();
	const bool automaticBlendingEnabled =
		settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic);
	const bool allowListBlendingEnabled =
		settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kAllowListOnly) &&
		(!compiledAllowList.empty() || !compiledExactAllowRules.empty());
	const bool runtimePolicyEnabled =
		IsRuntimeEnabled() && settings.ProjectedSnowEdgeWidth > 0.0f &&
		(automaticBlendingEnabled || allowListBlendingEnabled);
	if (!runtimePolicyEnabled && !discoveryCaptureActive) {
		return false;
	}

	SourceState source;
	source.geometry = a_pass->geometry;
	auto& runtimeData = source.geometry->GetGeometryRuntimeData();
	source.alphaProperty = runtimeData.alphaProperty.get();
	if (!source.alphaProperty ||
		source.alphaProperty->GetRTTI() != globals::rtti::NiAlphaPropertyRTTI.get() ||
		!source.alphaProperty->GetAlphaBlending() || source.alphaProperty->GetAlphaTesting() ||
		source.alphaProperty->GetSrcBlendMode() != RE::NiAlphaProperty::AlphaFunction::kSrcAlpha ||
		source.alphaProperty->GetDestBlendMode() != RE::NiAlphaProperty::AlphaFunction::kInvSrcAlpha ||
		!runtimeData.rendererData || runtimeData.skinInstance) {
		return false;
	}
	auto* property = runtimeData.shaderProperty.get();
	if (!property || property != a_pass->shaderProperty ||
		property->GetRTTI() != globals::rtti::BSLightingShaderPropertyRTTI.get()) {
		return false;
	}
	source.shaderProperty = static_cast<RE::BSLightingShaderProperty*>(property);
	source.shaderFlags = source.shaderProperty->flags.underlying();
	constexpr auto requiredFlags = ToMask(ShaderFlag::kProjectedUV) |
	                               ToMask(ShaderFlag::kMultiIndexSnow) |
	                               ToMask(ShaderFlag::kZBufferTest);
	constexpr auto projectedSnowExceptions = ToMask(ShaderFlag::kProjectedUV) | ToMask(ShaderFlag::kMultiIndexSnow);
	if ((source.shaderFlags & requiredFlags) != requiredFlags ||
		(source.shaderFlags & ToMask(ShaderFlag::kZBufferWrite)) != 0u ||
		(source.shaderFlags & (kExcludedShaderFlags & ~projectedSnowExceptions)) != 0u) {
		return false;
	}
	source.material = source.shaderProperty->material;
	if (!source.material || source.material->GetType() != RE::BSShaderMaterial::Type::kLighting ||
		source.material->GetFeature() != MaterialFeature::kMultiIndexTriShapeSnow ||
		!IsFinite(source.shaderProperty->alpha) || source.shaderProperty->alpha <= 0.0f ||
		!IsFiniteBound(source.geometry->worldBound)) {
		return false;
	}
	if (!IsInsideDistanceBubble(source.geometry->worldBound, state->frameCount)) {
		if (state->IsDeveloperMode()) {
			++diagnostics.distanceRejects;
		}
		return false;
	}

	const auto classification = GetSourceClassification(
		source,
		state->frameCount,
		discoveryCaptureActive,
		state->IsDeveloperMode());
	if (classification == Classification::kRejected) {
		return false;
	}
	if (!runtimePolicyEnabled ||
		(classification == Classification::kAutomatic && !automaticBlendingEnabled)) {
		return false;
	}

	state->permutationData.ExtraShaderDescriptor |=
		static_cast<std::uint32_t>(State::ExtraShaderDescriptors::MeshBlending);
	if (state->IsDeveloperMode()) {
		++diagnostics.activeDraws;
	}
	++diagnostics.currentFrameActive;
	return true;
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
	constexpr std::uint32_t landscapeClassMask = static_cast<std::uint32_t>(State::ExtraFeatureDescriptors::MeshBlendingLandscapeClasses);
	auto& descriptor = state->permutationData.ExtraShaderDescriptor;
	auto& featureDescriptor = state->permutationData.ExtraFeatureDescriptor;
	descriptor &= ~descriptorMask;
	featureDescriptor &= ~landscapeClassMask;

	const bool runtimeEnabled = IsRuntimeEnabled();
	const bool discoveryCaptureActive = IsDiscoveryCaptureActive();
	if (!runtimeEnabled && !discoveryCaptureActive) {
		return;
	}
	const auto passShaderFlags = a_pass && a_pass->shaderProperty ? a_pass->shaderProperty->flags.underlying() : 0u;
	const bool lodLandscapePass = (passShaderFlags & ToMask(ShaderFlag::kLODLandscape)) != 0u;
	const bool landscapePass = !lodLandscapePass &&
	                           (passShaderFlags & ToMask(ShaderFlag::kMultiTextureLandscape)) != 0u;
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
	if (landscapePass || lodLandscapePass) {
		// Capture is performed only when LAND streams or its final material is
		// replaced. A visible draw does one exact, lock-free registry probe; the
		// disabled toggle deliberately performs no lookup.
		if (landscapePass && runtimeEnabled && settings.LandscapeLayerBlending != 0u) {
			std::uint32_t packedClasses = 0u;
			if (TryGetLandscapeClasses(a_pass, packedClasses)) {
				if (collectDiagnostics) {
					++diagnostics.landscapeRegistryHits;
				}
				featureDescriptor |= packedClasses & landscapeClassMask;
				if ((packedClasses & landscapeClassMask) != 0u) {
					if (collectDiagnostics) {
						++diagnostics.activeDraws;
					}
					++diagnostics.currentFrameActive;
				}
			} else if (collectDiagnostics) {
				++diagnostics.landscapeRegistryMisses;
			}
		}
		return;
	}
	const auto projectedSnowFlags = ToMask(ShaderFlag::kProjectedUV) | ToMask(ShaderFlag::kMultiIndexSnow);
	if ((passShaderFlags & projectedSnowFlags) == projectedSnowFlags) {
		TryPrepareProjectedSnow(a_pass);
		return;
	}

	bool nifRuntimeEnabled = false;
	if (runtimeEnabled) {
		switch (static_cast<DetectionMode>(settings.DetectionMode)) {
		case DetectionMode::kAllowListOnly:
			nifRuntimeEnabled = !compiledAllowList.empty() || !compiledExactAllowRules.empty();
			break;
		case DetectionMode::kStrictAutomatic:
			nifRuntimeEnabled = true;
			break;
		default:
			break;
		}
	}
	if (!nifRuntimeEnabled && !discoveryCaptureActive) {
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
	const auto classification = GetSourceClassification(
		source,
		state->frameCount,
		discoveryCaptureActive,
		collectDiagnostics);
	if (classification == Classification::kRejected) {
		return;
	}

	const bool automaticBlendingEnabled =
		settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic);
	if (!nifRuntimeEnabled ||
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
	ImGui::Text("LAND registry: %llu hits, %llu misses",
		static_cast<unsigned long long>(diagnostics.landscapeRegistryHits),
		static_cast<unsigned long long>(diagnostics.landscapeRegistryMisses));
	ImGui::TreePop();
}

void MeshBlending::DrawDiscoverySettings()
{
	static Util::ConfirmationPopup savePopup{
		"Save Detected Materials?##MeshBlending",
		"This atomically merges captured exact NIF model/node pairs and streamed LTEX identities into "
		"MeshBlendingRules.json. Generated entries remain separate from manually authored policy.",
		"Save Detected Materials"
	};
	static Util::ConfirmationPopup clearPopup{
		"Clear Detected Materials?##MeshBlending",
		"This removes only generated DetectedAllowList and DiscoveryDiagnostics entries plus the current session capture. "
		"Manual AllowList, DenyList, LandscapeAssignments, SurfaceMaterials, BlendPairs, and unknown top-level extension fields are preserved.",
		"Clear Detected Materials"
	};

	ImGui::SeparatorText("Material recording");
	auto* state = globals::state;
	const bool mutationBlocked = state && state->IsPersistentMutationBlocked();
	const bool ruleFileBlocked = !ruleFileMutationAllowed.load(std::memory_order_acquire);
	const bool captureSaturated = IsDiscoveryCaptureSaturated();
	bool captureEnabled = discoveryCaptureEnabled.load(std::memory_order_relaxed);
	if (mutationBlocked || ruleFileBlocked || captureSaturated) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Checkbox("Record Visible Material Identities", &captureEnabled)) {
		SetDiscoveryCaptureEnabled(captureEnabled);
		if (captureEnabled && !discoveryCaptureEnabled.load(std::memory_order_relaxed)) {
			discoveryStatus = "Both bounded recording categories are full. Save or clear the current results.";
		} else {
			discoveryStatus.clear();
		}
	}
	if (mutationBlocked || ruleFileBlocked || captureSaturated) {
		ImGui::EndDisabled();
	}
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Records only NIF identities that are rendered inside the view/distance gates and LTEX identities from streamed LAND cells.\n"
			"It does not scan directories or open every NIF. Both categories are bounded and deduplicated in memory.\n"
			"No disk I/O occurs until Save Detected Materials is pressed. Recording is session-only and pauses performance A/B measurement.");
	}

	std::size_t discoveredNifCount = 0u;
	std::size_t nifDuplicateCount = 0u;
	std::size_t nifDroppedCount = 0u;
	std::size_t discoveredLandscapeCount = 0u;
	std::size_t landscapeDuplicateCount = 0u;
	std::size_t landscapeDroppedCount = 0u;
	std::size_t compiledLandscapePairCount = 0u;
	{
		std::scoped_lock lock(discoveryMutex);
		discoveredNifCount = discoveredRuleKeys.size();
		nifDuplicateCount = discoveryDuplicateObservations;
		nifDroppedCount = discoveryDropped;
		discoveredLandscapeCount = discoveredLandscapeIdentities.size();
		landscapeDuplicateCount = landscapeDiscoveryDuplicateObservations;
		landscapeDroppedCount = landscapeDiscoveryDropped;
	}
	{
		const std::shared_lock lock(landscapeRulesMutex);
		compiledLandscapePairCount = compiledBlendPairs.size();
	}
	ImGui::Text(
		"Session NIF: %zu / %zu unique; %zu duplicates; %zu dropped",
		discoveredNifCount,
		kMaximumRules,
		nifDuplicateCount,
		nifDroppedCount);
	ImGui::Text(
		"Session LTEX: %zu / %zu unique; %zu duplicates; %zu dropped",
		discoveredLandscapeCount,
		kMaximumLandscapeIdentities,
		landscapeDuplicateCount,
		landscapeDroppedCount);
	ImGui::Text(
		"Persisted: %zu manual allow, %zu detected allow, %zu deny; %zu LTEX assignments, %zu surface rules, "
		"%zu authored pair rules (%s; %zu compiled), %zu diagnostics",
		allowList.size(),
		detectedAllowList.size(),
		denyList.size(),
		landscapeAssignments.size(),
		surfaceMaterialRules.size(),
		blendPairRules.size(),
		blendPairsExplicit ? "explicit policy" : "built-in defaults",
		compiledLandscapePairCount,
		savedDiscoveryDiagnostics.size());
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
	if (nifDroppedCount != 0u || landscapeDroppedCount != 0u) {
		ImGui::TextWrapped("Some identities were not retained because their independent category was full or the identity was unsafe for JSON.");
	}
	if (captureSaturated) {
		ImGui::TextWrapped("Recording stopped because both independent categories reached their session limits.");
	}

	const bool hasSessionCapture = discoveredNifCount != 0u || discoveredLandscapeCount != 0u;
	const bool hasGeneratedData = !detectedAllowList.empty() || !savedDiscoveryDiagnostics.empty();
	if (mutationBlocked || ruleFileBlocked || !hasSessionCapture) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Save Detected Materials")) {
		savePopup.Request();
	}
	if (mutationBlocked || ruleFileBlocked || !hasSessionCapture) {
		ImGui::EndDisabled();
	}
	ImGui::SameLine();
	if (mutationBlocked || ruleFileBlocked || (!hasSessionCapture && !hasGeneratedData)) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Clear Detected Materials")) {
		clearPopup.Request();
	}
	if (mutationBlocked || ruleFileBlocked || (!hasSessionCapture && !hasGeneratedData)) {
		ImGui::EndDisabled();
	}

	if (!discoveryStatus.empty()) {
		ImGui::TextWrapped("%s", discoveryStatus.c_str());
	}

	if (savePopup.Draw()) {
		if (globals::state && globals::state->IsPersistentMutationBlocked()) {
			discoveryStatus = "Nothing was changed because the game is saving or loading.";
		} else {
			const auto result = SaveDiscoveredRules();
			if (result.success) {
				discoveryStatus = std::format(
					"Saved {} new NIF identities and {} new LTEX identities; {} NIF identities were already allowed, {} were denied, "
					"{} LTEX identities were refreshed, and {} NIF / {} LTEX identities exceeded capacity.",
					result.added,
					result.landscapeAdded,
					result.alreadyAllowed,
					result.denied,
					result.landscapeRefreshed,
					result.capacityRejected,
					result.landscapeCapacityRejected);
			} else {
				discoveryStatus = std::format("Nothing was changed: {}", result.error);
			}
		}
	}
	if (clearPopup.Draw()) {
		if (globals::state && globals::state->IsPersistentMutationBlocked()) {
			discoveryStatus = "Nothing was changed because the game is saving or loading.";
		} else {
			std::string error;
			if (ClearSavedRules(error)) {
				discoveryStatus = "Cleared generated detected identities and the current capture; manual policy was preserved.";
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
	static constexpr const char* detectionModes[] = { "Disabled", "Allow list only", "Automatic" };
	if (ImGui::Combo("Detection mode", &detectionMode, detectionModes, static_cast<int>(std::size(detectionModes)))) {
		settings.DetectionMode = static_cast<std::uint32_t>(detectionMode);
		classifierChanged = true;
	}
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Automatic is the default and uses conservative static-owner, render-state, bounds, and sibling gates. Deny rules always win.");
	}

	ImGui::SliderFloat("Blend Strength", &settings.BlendStrength, 0.0f, 1.0f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Controls NIF intersection fades and LAND layer remapping. Zero disables runtime blending work.");
	}
	bool landscapeLayerBlending = settings.LandscapeLayerBlending != 0u;
	if (ImGui::Checkbox("Fast LAND/LTEX material blending", &landscapeLayerBlending)) {
		settings.LandscapeLayerBlending = landscapeLayerBlending ? 1u : 0u;
	}
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Rebalances already-active LAND texture weights using bounded material rules: soft deposits over hard, "
			"established soft/soft overlap equalizes, and hard/hard stays crisp. Unknown identities preserve authored weights.");
	}
	ImGui::SliderFloat("Projected snow edge width", &settings.ProjectedSnowEdgeWidth, 0.0f, 8.0f, "%.2f pixels");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Softens accepted multi-index projected-snow cutoffs. Zero preserves the original hard discard.");
	}
	ImGui::SliderFloat("NIF blend width", &settings.BlendWidth, 0.25f, 128.0f, "%.2f units");
	ImGui::SliderFloat("NIF depth bias", &settings.DepthBias, 0.0f, 16.0f, "%.2f units");
	ImGui::SliderFloat("NIF maximum gap", &settings.MaximumGap, 1.0f, 256.0f, "%.1f units");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Maximum gap is clamped above bias + width so the fade cannot jump discontinuously.");
	}

	if (ImGui::TreeNodeEx("Performance and safety")) {
		ImGui::SliderFloat("NIF culling distance", &settings.MaximumDistance, 0.0f, 32768.0f, "%.0f units");
		if (auto tooltip = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Static NIF shapes outside this camera/eye-centered bubble are rejected before root lookup. "
				"LAND is already bounded by submitted nearby terrain draws. Set to 0 for unlimited NIF distance.");
		}
		classifierChanged |= ImGui::SliderFloat("Bounds expansion", &settings.BoundsExpansion, 0.0f, 256.0f, "%.0f units");
		ImGui::TextDisabled("Automatic NIF blending always requires a plausible opaque sibling.");
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
		ImGui::TextWrapped("Automatic NIF detection is active. Deny rules always win; explicit allows still pass every render-state and static-owner safety gate.");
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
	if (!ruleFileMutationAllowed.load(std::memory_order_acquire)) {
		ImGui::TextWrapped("Mesh Blending is disabled until MeshBlendingRules.json is fixed or removed and the game is restarted.");
	}
}

void MeshBlending::DrawPerformanceSettings(bool a_advanced)
{
	bool enabled = settings.Enabled != 0u;
	if (ImGui::Checkbox("Enable", &enabled)) {
		settings.Enabled = enabled ? 1u : 0u;
	}
	ImGui::SliderFloat("NIF culling distance", &settings.MaximumDistance, 0.0f, 32768.0f, "%.0f units");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Applies only to NIF classification. LAND work is already limited to submitted nearby terrain draws.");
	}
	if (IsDiscoveryCaptureActive()) {
		ImGui::TextDisabled("Stop Record Visible Material Identities before running a feature-cost measurement.");
	}
	if (a_advanced) {
		ImGui::Text("Mode: %s; active draws: %u last frame, %u this frame", DetectionModeName(settings.DetectionMode), diagnostics.lastFrameActive, diagnostics.currentFrameActive);
		ImGui::Text("LAND registry: %llu hits, %llu misses",
			static_cast<unsigned long long>(diagnostics.landscapeRegistryHits),
			static_cast<unsigned long long>(diagnostics.landscapeRegistryMisses));
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
