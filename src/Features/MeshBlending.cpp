#include "MeshBlending.h"

#include "../Globals.h"
#include "../ShaderCache.h"
#include "../State.h"
#include "../Util.h"
#include "../Utils/FileSystem.h"
#include "../Utils/Finite.h"
#include "../Utils/Game.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <tuple>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MeshBlending::OverrideRule,
	Model,
	NodePath);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MeshBlending::Settings,
	Enabled,
	NifEnabled,
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
	static_assert(CSX::MeshBlendingPolicy::EnableMask == static_cast<std::uint32_t>(State::ExtraShaderDescriptors::MeshBlending));
	using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;
	using MaterialFeature = RE::BSShaderMaterial::Feature;

	constexpr std::uint32_t kRuleFileSchemaVersion = 4u;
	constexpr std::uint32_t kOldestSupportedRuleFileSchemaVersion = 1u;
	constexpr std::uintmax_t kMaximumRuleFileBytes = 4u * 1024u * 1024u;

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

	RE::NiAVObject* GetLinkedChild(RE::NiNode* a_parent, std::uint32_t a_index)
	{
		// Sparse child arrays report a non-null count as size(), not an index bound.
		if (!a_parent || a_index >= a_parent->GetChildren().capacity())
			return nullptr;
		auto* child = a_parent->GetChildren()[static_cast<std::uint16_t>(a_index)].get();
		return child && child->parent == a_parent && child->parentIndex == a_index ? child : nullptr;
	}

	RE::TESObjectREFR* GetBoundedOwner(RE::NiAVObject* a_object, std::size_t a_maximumDepth)
	{
		return CSX::MeshBlendingPolicy::ResolveBoundedOwner(a_object, a_maximumDepth, [](const RE::NiAVObject* a_current) { return Util::GetOwnUserData(a_current); }, [](RE::NiAVObject* a_current) -> RE::NiAVObject* {
				auto* parent = a_current->parent;
				return GetLinkedChild(parent, a_current->parentIndex) == a_current ? parent : nullptr; });
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

	std::string NormalizePath(std::string_view a_path, bool a_isModelPath)
	{
		return CSX::MeshBlendingPolicy::NormalizePath(a_path, a_isModelPath);
	}

	bool IsSafeRuleString(std::string_view a_value, std::size_t a_maximumLength)
	{
		return a_value.size() <= a_maximumLength && IsValidUtf8(a_value) &&
		       std::none_of(a_value.begin(), a_value.end(), [](char character) {
				   const auto value = static_cast<unsigned char>(character);
				   return value < static_cast<unsigned char>(' ') || value == 0x7Fu;
			   });
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
		"Blends eligible NIF transition overlays without an extra render pass.",
		{ "Conservative static-object selection with deny-first rules",
			"Generated rules retain current receiver validation",
			"Bounded cache and traversal with a configurable distance limit",
			"Flat-screen and VR eye-correct depth sampling" }
	};
}

void MeshBlending::SanitizeSettings()
{
	settings = Sanitize(settings);
}

MeshBlending::Settings MeshBlending::Sanitize(Settings a_settings)
{
	a_settings.Enabled = a_settings.Enabled ? 1u : 0u;
	a_settings.NifEnabled = a_settings.NifEnabled ? 1u : 0u;
	a_settings.RequireOverlappingBounds = a_settings.RequireOverlappingBounds ? 1u : 0u;
	a_settings.DeveloperLogging = a_settings.DeveloperLogging ? 1u : 0u;
	if (a_settings.DetectionMode > static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic)) {
		a_settings.DetectionMode = static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic);
	}
	a_settings.BlendStrength = Util::ClampFinite(a_settings.BlendStrength, 0.0f, 1.0f, 1.0f);
	a_settings.BlendWidth = Util::ClampFinite(a_settings.BlendWidth, 0.01f, 256.0f, 12.0f);
	a_settings.DepthBias = Util::ClampFinite(a_settings.DepthBias, 0.0f, 128.0f, 0.25f);
	a_settings.MaximumGap = Util::ClampFinite(a_settings.MaximumGap, 0.01f, 4096.0f, 64.0f);
	a_settings.MaximumGap = std::max(a_settings.MaximumGap, a_settings.DepthBias + a_settings.BlendWidth);
	a_settings.MaximumDistance = Util::ClampFinite(a_settings.MaximumDistance, 0.0f, 65536.0f, 8192.0f);
	a_settings.BoundsExpansion = Util::ClampFinite(a_settings.BoundsExpansion, 0.0f, 1024.0f, 32.0f);
	return a_settings;
}

MeshBlending::CompiledRules MeshBlending::CompileRules(const RuleFileState& a_state)
{
	CompiledRules compiled;
	for (const auto& rule : a_state.allowList)
		compiled.authored.Add(rule.Model, rule.NodePath);
	for (const auto& rule : a_state.denyList)
		compiled.denied.Add(rule.Model, rule.NodePath);
	for (const auto& rule : a_state.detectedAllowList)
		compiled.AddDetected(rule.Model, rule.NodePath);
	compiled.BuildNodeIndex();
	return compiled;
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
	const auto sanitized = Sanitize(settings);
	return {
		sanitized.NifEnabled != 0u ? sanitized.BlendStrength : 0.0f,
		sanitized.BlendWidth,
		sanitized.DepthBias,
		sanitized.MaximumGap,
		0u,
		0.0f,
		0.0f,
		0.0f
	};
}

bool MeshBlending::IsRuntimeEnabled() const
{
	if (!loaded || !ruleFileMutationAllowed.load(std::memory_order_acquire) ||
		!globals::shaderCache || !globals::shaderCache->IsEnabled() || settings.Enabled == 0u) {
		return false;
	}
	return IsOverlayPolicyEnabled() &&
	       ((settings.NifEnabled != 0u && IsFinite(settings.BlendStrength) && settings.BlendStrength > 0.0f));
}

bool MeshBlending::IsOverlayPolicyEnabled() const
{
	switch (static_cast<DetectionMode>(settings.DetectionMode)) {
	case DetectionMode::kAllowListOnly:
		return compiledRules.HasAllowRules();
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
	return discoveredRuleKeys.size() >= kMaximumRules;
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
		discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
		return;
	}
	discoveredRuleKeys.insert(key);
	if (discoveredRuleKeys.size() >= kMaximumRules) {
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

	auto compiled = CompileRules(state);
	ApplyRuleFileState(std::move(state), std::move(compiled));
	ruleFileLoadError.clear();
	ruleFileMutationAllowed.store(true, std::memory_order_release);
	logger::info(
		"[Mesh Blending] Loaded {} manual, {} detected, and {} deny rules{}",
		allowList.size(),
		detectedAllowList.size(),
		denyList.size(),
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
	const auto ruleFilePath = GetRuleFilePath();
	std::optional<std::string> sourceContents;
	if (!Util::FileHelpers::ReadTextFileBounded(
			ruleFilePath, kMaximumRuleFileBytes, sourceContents, a_error)) {
		return false;
	}
	if (!sourceContents) {
		a_notFound = true;
		a_state.document = json::object({ { "SchemaVersion", kRuleFileSchemaVersion } });
		a_state.sourceContents.reset();
		a_error.clear();
		return true;
	}
	std::string rawContents = std::move(*sourceContents);
	json ruleFile = json::parse(rawContents, nullptr, false);
	if (ruleFile.is_discarded()) {
		a_error = "The rule file is not valid JSON.";
		return false;
	}
	a_state.sourceContents = std::move(rawContents);
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
					!readString(item, context, "NodePath", rule.NodePath, false)) {
					return false;
				}
				auto canonical = CSX::MeshBlendingPolicy::CanonicalizeOverrideSelectors(rule.Model, rule.NodePath);
				if (canonical.ModelCollapsed()) {
					a_error = std::format("{}.Model has no effective path after canonicalization.", context);
					return false;
				}
				if (canonical.NodePathCollapsed()) {
					a_error = std::format("{}.NodePath has no effective path after canonicalization.", context);
					return false;
				}
				if (!canonical.HasSelector()) {
					a_error = std::format("{} must identify an effective model or node after canonicalization.", context);
					return false;
				}
				if (a_exact) {
					if (canonical.model.empty()) {
						a_error = std::format("{}.Model has no effective path after canonicalization.", context);
						return false;
					}
					if (canonical.nodePath.empty()) {
						a_error = std::format("{}.NodePath has no effective path after canonicalization.", context);
						return false;
					}
					if (!canonical.IsExactPair()) {
						a_error = std::format("{} must be an exact model/node pair.", context);
						return false;
					}
					rule.Model = std::move(canonical.model);
					rule.NodePath = std::move(canonical.nodePath);
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

	} catch (const json::exception& exception) {
		a_error = std::format("The rule file is invalid: {}", exception.what());
		return false;
	}

	return true;
}

void MeshBlending::ApplyRuleFileState(RuleFileState&& a_state, CompiledRules&& a_compiledRules)
{
	allowList = std::move(a_state.allowList);
	detectedAllowList = std::move(a_state.detectedAllowList);
	denyList = std::move(a_state.denyList);
	ruleFileDocument = std::move(a_state.document);
	compiledRules = std::move(a_compiledRules);
	InvalidateClassificationCache();
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

		return Util::FileHelpers::WriteTextFileAtomicIfUnchanged(
			GetRuleFilePath(),
			ruleFile.dump(1),
			a_state.sourceContents,
			kMaximumRuleFileBytes,
			a_error);
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
	{
		std::scoped_lock lock(discoveryMutex);
		discoveredSnapshot = discoveredRuleKeys;
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
	const auto stagedRules = CompileRules(diskState);

	std::vector<OverrideRule> additions;
	additions.reserve(discoveredSnapshot.size());
	for (const auto& [modelPath, nodePath] : discoveredSnapshot) {
		const auto match = stagedRules.Match(modelPath, nodePath);
		if (match == CSX::MeshBlendingPolicy::RuleMatch::kDeny) {
			++result.denied;
			continue;
		}
		if (match == CSX::MeshBlendingPolicy::RuleMatch::kAuthored || match == CSX::MeshBlendingPolicy::RuleMatch::kDetected) {
			++result.alreadyAllowed;
			continue;
		}
		additions.push_back({ modelPath, nodePath });
	}

	const std::size_t occupiedAllowCapacity = std::min(diskState.allowList.size() + diskState.detectedAllowList.size(), kMaximumRules);
	const std::size_t remainingCapacity = kMaximumRules - occupiedAllowCapacity;
	if (additions.size() > remainingCapacity) {
		result.capacityRejected = additions.size() - remainingCapacity;
		additions.resize(remainingCapacity);
	}

	auto proposedDetectedAllowList = std::move(diskState.detectedAllowList);
	proposedDetectedAllowList.insert(proposedDetectedAllowList.end(), additions.begin(), additions.end());
	std::sort(proposedDetectedAllowList.begin(), proposedDetectedAllowList.end(), [](const auto& a_left, const auto& a_right) {
		return std::tie(a_left.Model, a_left.NodePath) < std::tie(a_right.Model, a_right.NodePath);
	});
	proposedDetectedAllowList.erase(
		std::unique(proposedDetectedAllowList.begin(), proposedDetectedAllowList.end(), [](const auto& a_left, const auto& a_right) {
			return a_left.Model == a_right.Model && a_left.NodePath == a_right.NodePath;
		}),
		proposedDetectedAllowList.end());

	RuleFileState proposed = std::move(diskState);
	proposed.detectedAllowList = std::move(proposedDetectedAllowList);
	auto compiled = CompileRules(proposed);
	if (!WriteRuleFile(proposed, result.error)) {
		ruleFileMutationAllowed.store(true, std::memory_order_release);
		return result;
	}

	ApplyRuleFileState(std::move(proposed), std::move(compiled));
	result.added = additions.size();
	result.success = true;
	ruleFileLoadError.clear();
	ruleFileMutationAllowed.store(true, std::memory_order_release);
	ClearDiscoveredRules();
	logger::info(
		"[Mesh Blending] Saved {} NIF identities to {}",
		result.added,
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
		// Wait for an in-flight observation after disabling capture.
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
	auto compiled = CompileRules(diskState);
	if (!WriteRuleFile(diskState, a_error)) {
		ruleFileMutationAllowed.store(true, std::memory_order_release);
		return false;
	}
	ApplyRuleFileState(std::move(diskState), std::move(compiled));
	ClearDiscoveredRules();
	ruleFileLoadError.clear();
	ruleFileMutationAllowed.store(true, std::memory_order_release);
	logger::info("[Mesh Blending] Cleared detected NIF identities in {}", GetRuleFilePath().string());
	return true;
}

void MeshBlending::ClearDiscoveredRules()
{
	std::scoped_lock lock(discoveryMutex);
	discoveredRuleKeys.clear();
	discoveryDuplicateObservations = 0u;
	discoveryDropped = 0u;
}

bool MeshBlending::BuildOverlaySourceState(RE::BSRenderPass* a_pass, SourceState& a_source) const
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
		(a_source.shaderFlags & ToMask(ShaderFlag::kZBufferWrite)) != 0u) {
		return false;
	}

	a_source.material = a_source.shaderProperty->material;
	if (!a_source.material ||
		a_source.material->GetType() != RE::BSShaderMaterial::Type::kLighting ||
		!IsFinite(a_source.shaderProperty->alpha) ||
		a_source.shaderProperty->alpha <= 0.0f ||
		!IsFiniteBound(a_source.geometry->worldBound)) {
		return false;
	}
	a_source.owner = GetBoundedOwner(a_source.geometry, kMaximumRootDepth);
	return true;
}

bool MeshBlending::BuildSourceState(RE::BSRenderPass* a_pass, SourceState& a_source) const
{
	return BuildOverlaySourceState(a_pass, a_source) &&
	       (a_source.shaderFlags & kExcludedShaderFlags) == 0u &&
	       IsSafeMaterialFeature(a_source.material->GetFeature());
}

bool MeshBlending::ResolveStaticOwner(SourceState& a_source) const
{
	a_source.nodePathLength = 0u;
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
		const char* name = current->name.c_str();
		a_source.nodeParts[a_source.nodePathLength++] = { name ? name : "", current->parentIndex };
		if (current == a_source.root) {
			belongsToRoot = true;
			break;
		}
		auto* parent = current->parent;
		if (GetLinkedChild(parent, current->parentIndex) != current) {
			return false;
		}
		current = parent;
	}
	if (!belongsToRoot) {
		return false;
	}
	std::reverse(a_source.nodeParts.begin(), a_source.nodeParts.begin() + a_source.nodePathLength);

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
	signature.owner = reinterpret_cast<std::uintptr_t>(a_source.owner);
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

MeshBlending::CacheEntry* MeshBlending::FindCachedClassification(
	const Signature& a_signature,
	std::uint32_t a_frame,
	bool a_sourceStateOnly)
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
			return nullptr;
		}
		entry.lastUsedFrame = a_frame;
		return &entry;
	}
	return nullptr;
}

void MeshBlending::StoreClassification(
	const SourceState& a_source,
	const Signature& a_signature,
	std::uint32_t a_frame,
	Classification a_classification,
	RE::BSGeometry* a_receiver)
{
	const auto mixedKey = (a_signature.geometry >> 4u) ^ (a_signature.geometry >> 17u) ^ (a_signature.geometry >> 29u);
	const std::size_t setStart = (mixedKey & (kCacheSetCount - 1u)) * kCacheWays;
	std::size_t replacement = setStart;
	for (std::size_t way = 0u; way < kCacheWays; ++way) {
		const std::size_t index = setStart + way;
		if (!classificationCache[index].occupied || classificationCache[index].signature.geometry == a_signature.geometry) {
			replacement = index;
			break;
		}
		if (classificationCache[index].lastUsedFrame < classificationCache[replacement].lastUsedFrame) {
			replacement = index;
		}
	}
	if (classificationCache[replacement].occupied && classificationCache[replacement].signature.geometry != a_signature.geometry &&
		globals::state && globals::state->IsDeveloperMode()) {
		++diagnostics.cacheEvictions;
	}
	classificationCache[replacement] = {
		a_signature,
		compiledRules.HasAnyRules() ?
			CSX::MeshBlendingPolicy::RuleIdentity{ BuildModelPath(a_source), CSX::MeshBlendingPolicy::BuildNodeCacheIdentity(a_source.NodePath()) } :
			CSX::MeshBlendingPolicy::RuleIdentity{},
		BuildReceiverRoute(a_source, a_receiver),
		a_receiver,
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
	const bool sourceStateCacheAllowed = !compiledRules.HasAnyRules();
	auto* cached = sourceStateCacheAllowed ? FindCachedClassification(signature, a_frame, true) : nullptr;
	if (cached && a_collectDiagnostics) {
		++diagnostics.preOwnerCacheHits;
	}
	if (cached && cached->classification == Classification::kRejected) {
		if (a_collectDiagnostics)
			++diagnostics.cacheHits;
		return Classification::kRejected;
	}

	if (a_collectDiagnostics)
		++diagnostics.ownerResolutionAttempts;
	if (!ResolveStaticOwner(a_source)) {
		if (a_collectDiagnostics) {
			++diagnostics.sourceRejects;
			++diagnostics.cacheMisses;
		}
		if (cached)
			cached->occupied = false;
		if (sourceStateCacheAllowed)
			StoreClassification(a_source, signature, a_frame, Classification::kRejected);
		return Classification::kRejected;
	}
	CompleteOwnershipSignature(a_source, signature);
	if (cached && cached->signature != signature) {
		cached->occupied = false;
		cached = nullptr;
	}
	if (!sourceStateCacheAllowed) {
		cached = FindCachedClassification(signature, a_frame, false);
		if (cached && a_collectDiagnostics)
			++diagnostics.fullSignatureCacheHits;
	}
	if (cached) {
		const bool identityIsCurrent = sourceStateCacheAllowed || CSX::MeshBlendingPolicy::MatchesRuleIdentity(
																	  a_source.modelPath ? a_source.modelPath : "", a_source.NodePath(), cached->ruleIdentity);
		const bool receiverIsCurrentAndSafe = !CSX::MeshBlendingPolicy::RequiresReceiverValidation(cached->classification) ||
		                                      ValidateCachedReceiver(a_source, *cached);
		if (!identityIsCurrent || !CSX::MeshBlendingPolicy::CanReuseCacheHit(
									  cached->classification, a_source.root->HasAnimation(), receiverIsCurrentAndSafe, identityIsCurrent)) {
			cached->occupied = false;
			cached = nullptr;
		}
	}
	Classification classification = Classification::kRejected;
	if (cached) {
		classification = cached->classification;
		if (a_collectDiagnostics) {
			++diagnostics.cacheHits;
		}
	} else {
		if (a_collectDiagnostics) {
			++diagnostics.cacheMisses;
		}
		RE::BSGeometry* receiver = nullptr;
		classification = ClassifyOnCacheMiss(a_source, a_captureDiscovery, receiver);
		StoreClassification(a_source, signature, a_frame, classification, receiver);
	}
	if (a_collectDiagnostics) {
		if (classification == Classification::kAllowedByRule || classification == Classification::kDetectedByRule) {
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
	return CSX::MeshBlendingPolicy::BuildCanonicalNodePath(a_source.NodePath());
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
	if (!a_receiver || a_receiver == a_source.geometry || a_receiver->GetAppCulled() ||
		GetBoundedOwner(a_receiver, kMaximumRootDepth) != a_source.owner) {
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

bool MeshBlending::HasOpaqueSibling(
	const SourceState& a_source,
	bool& a_hitTraversalLimit,
	RE::BSGeometry*& a_receiver)
{
	a_hitTraversalLimit = false;
	a_receiver = nullptr;
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
		if (!object || object->GetAppCulled()) {
			continue;
		}
		if (collectDiagnostics) {
			++diagnostics.nodesVisited;
		}
		if (auto* geometry = object->AsGeometry();
			geometry && IsSafeReceiver(a_source, geometry)) {
			a_receiver = geometry;
			return true;
		}
		if (auto* node = object->AsNode()) {
			for (const auto& child : node->GetChildren()) {
				if (!child || GetLinkedChild(node, child->parentIndex) != child.get()) {
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

MeshBlending::ReceiverRoute MeshBlending::BuildReceiverRoute(const SourceState& a_source, RE::BSGeometry* a_receiver) const
{
	ReceiverRoute route;
	auto* current = static_cast<RE::NiAVObject*>(a_receiver);
	while (current && current != a_source.root && route.length < route.indices.size()) {
		auto* parent = current->parent;
		if (GetLinkedChild(parent, current->parentIndex) != current) {
			return route;
		}
		route.indices[route.length++] = current->parentIndex;
		current = parent;
	}
	route.valid = current && current == a_source.root;
	if (route.valid)
		std::reverse(route.indices.begin(), route.indices.begin() + route.length);
	return route;
}

bool MeshBlending::ValidateCachedReceiver(const SourceState& a_source, const CacheEntry& a_entry) const
{
	if (!a_entry.receiver || !a_entry.receiverRoute.valid)
		return false;
	auto* current = CSX::MeshBlendingPolicy::ResolveLiveRoute(
		a_source.root,
		std::span(a_entry.receiverRoute.indices.data(), a_entry.receiverRoute.length),
		static_cast<RE::NiAVObject*>(a_entry.receiver),
		[](RE::NiAVObject* a_parent, std::uint32_t a_index) -> RE::NiAVObject* {
			return GetLinkedChild(a_parent->AsNode(), a_index);
		},
		[](const RE::NiAVObject* a_object) { return !a_object->GetAppCulled(); });
	return current && IsSafeReceiver(a_source, current->AsGeometry());
}

MeshBlending::Classification MeshBlending::ClassifyOnCacheMiss(
	const SourceState& a_source,
	bool a_captureDiscovery,
	RE::BSGeometry*& a_receiver)
{
	a_receiver = nullptr;
	if (a_source.root->HasAnimation()) {
		return Classification::kRejected;
	}

	const std::string modelPath = BuildModelPath(a_source);
	const bool logClassification = settings.DeveloperLogging != 0u &&
	                               globals::state && globals::state->IsDeveloperMode() &&
	                               classificationLogs < kMaximumClassificationLogs;
	const bool needsNodePath = compiledRules.NeedsNodePath(modelPath);
	std::string nodePath = needsNodePath ? BuildNodePath(a_source) : std::string{};
	if (needsNodePath && nodePath.empty()) {
		return Classification::kRejected;
	}

	const auto ruleMatch = compiledRules.Match(modelPath, nodePath);
	if (ruleMatch == CSX::MeshBlendingPolicy::RuleMatch::kDeny) {
		return Classification::kRejected;
	}
	if (ruleMatch == CSX::MeshBlendingPolicy::RuleMatch::kAuthored) {
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
	const bool detectedMatch = ruleMatch == CSX::MeshBlendingPolicy::RuleMatch::kDetected;
	bool captureHasCapacity = false;
	if (a_captureDiscovery) {
		std::scoped_lock lock(discoveryMutex);
		captureHasCapacity = discoveredRuleKeys.size() < kMaximumRules;
		if (!captureHasCapacity) {
			discoveryCaptureEnabled.store(false, std::memory_order_relaxed);
		}
	}
	if (!detectedMatch && !automaticBlendingEnabled && !captureHasCapacity) {
		return Classification::kRejected;
	}
	if (modelPath.empty()) {
		return Classification::kRejected;
	}
	bool hitTraversalLimit = false;
	if (!HasOpaqueSibling(a_source, hitTraversalLimit, a_receiver)) {
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
	return detectedMatch ? Classification::kDetectedByRule : Classification::kAutomatic;
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
	CSX::MeshBlendingPolicy::ClearDrawDescriptor(descriptor);

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
	const bool nifRuntimeEnabled = runtimeEnabled && settings.NifEnabled != 0u &&
	                               IsFinite(settings.BlendStrength) && settings.BlendStrength > 0.0f && IsOverlayPolicyEnabled();
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
	ImGui::TreePop();
}

void MeshBlending::DrawDiscoverySettings()
{
	static Util::ConfirmationPopup savePopup{
		"Save Detected NIF Rules?##MeshBlending",
		"This atomically merges captured exact model/node pairs into MeshBlendingRules.json. "
		"Generated rules retain current receiver checks and remain separate from authored policy.",
		"Save Detected NIF Rules"
	};
	static Util::ConfirmationPopup clearPopup{
		"Clear Detected NIF Rules?##MeshBlending",
		"This removes generated DetectedAllowList entries and the current session capture. "
		"Authored AllowList, DenyList, and unknown extension fields are preserved.",
		"Clear Detected NIF Rules"
	};
	ImGui::SeparatorText("NIF recording");
	auto* state = globals::state;
	const bool mutationBlocked = state && state->IsPersistentMutationBlocked();
	const bool ruleFileBlocked = !ruleFileMutationAllowed.load(std::memory_order_acquire);
	const bool captureSaturated = IsDiscoveryCaptureSaturated();
	bool captureEnabled = discoveryCaptureEnabled.load(std::memory_order_relaxed);
	if (mutationBlocked || ruleFileBlocked || captureSaturated)
		ImGui::BeginDisabled();
	if (ImGui::Checkbox("Record Visible NIF Identities", &captureEnabled)) {
		SetDiscoveryCaptureEnabled(captureEnabled);
		discoveryStatus = captureEnabled && !discoveryCaptureEnabled.load(std::memory_order_relaxed) ?
		                      "The recording limit is reached. Save or clear the current results." :
		                      "";
	}
	if (mutationBlocked || ruleFileBlocked || captureSaturated)
		ImGui::EndDisabled();
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Records eligible visible NIF overlays inside the view and distance gates. Identities are bounded and deduplicated in memory. "
			"Recording is session-only, pauses feature-cost measurement, and writes a file only when Save Detected NIF Rules is pressed.");
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
	ImGui::Text("Session: %zu / %zu unique; %zu duplicates; %zu dropped", discoveredCount, kMaximumRules, duplicateCount, droppedCount);
	ImGui::Text("Persisted: %zu authored allow, %zu detected allow, %zu deny", allowList.size(), detectedAllowList.size(), denyList.size());
	ImGui::TextDisabled("Rule file: Data/SKSE/Plugins/CommunityShaders/MeshBlendingRules.json");
	if (ruleFileBlocked)
		ImGui::TextWrapped("Mesh Blending and discovery are disabled, and the rule file is protected from replacement: %s Fix or remove it, then restart.", ruleFileLoadError.c_str());
	if (mutationBlocked)
		ImGui::TextDisabled("Rule-file changes are paused while the game is saving or loading.");
	if (droppedCount != 0u)
		ImGui::TextWrapped("Some identities were not retained because recording was full or the identity was unsafe for JSON.");
	if (captureSaturated)
		ImGui::TextWrapped("Recording stopped at the session limit. Save or clear the captured identities.");
	const bool hasSessionCapture = discoveredCount != 0u;
	if (mutationBlocked || ruleFileBlocked || !hasSessionCapture)
		ImGui::BeginDisabled();
	if (ImGui::Button("Save Detected NIF Rules"))
		savePopup.Request();
	if (mutationBlocked || ruleFileBlocked || !hasSessionCapture)
		ImGui::EndDisabled();
	ImGui::SameLine();
	if (mutationBlocked || ruleFileBlocked || (!hasSessionCapture && detectedAllowList.empty()))
		ImGui::BeginDisabled();
	if (ImGui::Button("Clear Detected NIF Rules"))
		clearPopup.Request();
	if (mutationBlocked || ruleFileBlocked || (!hasSessionCapture && detectedAllowList.empty()))
		ImGui::EndDisabled();
	if (!discoveryStatus.empty())
		ImGui::TextWrapped("%s", discoveryStatus.c_str());
	if (savePopup.Draw()) {
		if (globals::state && globals::state->IsPersistentMutationBlocked()) {
			discoveryStatus = "Nothing was changed because the game is saving or loading.";
		} else {
			const auto result = SaveDiscoveredRules();
			discoveryStatus = result.success ?
			                      std::format("Saved {} new NIF identities; {} were already allowed, {} denied, and {} exceeded capacity.",
									  result.added, result.alreadyAllowed, result.denied, result.capacityRejected) :
			                      std::format("Could not save detected NIF rules: {}", result.error);
		}
	}
	if (clearPopup.Draw()) {
		if (globals::state && globals::state->IsPersistentMutationBlocked()) {
			discoveryStatus = "Nothing was changed because the game is saving or loading.";
		} else {
			std::string error;
			discoveryStatus = ClearSavedRules(error) ? "Cleared detected NIF rules and the current capture; authored policy was preserved." :
			                                           std::format("Could not clear detected NIF rules: {}", error);
		}
	}
}

void MeshBlending::DrawSettings()
{
	bool enabled = settings.Enabled != 0u;
	if (ImGui::Checkbox("Enable", &enabled)) {
		settings.Enabled = enabled ? 1u : 0u;
	}
	DrawMechanismToggles();

	bool classifierChanged = false;
	int detectionMode = static_cast<int>(settings.DetectionMode);
	static constexpr const char* detectionModes[] = { "Disabled", "Allow list only", "Automatic" };
	if (ImGui::Combo("Detection mode", &detectionMode, detectionModes, static_cast<int>(std::size(detectionModes)))) {
		settings.DetectionMode = static_cast<std::uint32_t>(detectionMode);
		classifierChanged = true;
	}
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Selects eligible NIF overlays. Automatic and saved detected rules require current static-owner, render-state, bounds, and sibling checks. Deny rules always win.");
	}

	ImGui::SliderFloat("NIF blend strength", &settings.BlendStrength, 0.0f, 1.0f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Controls NIF intersection fades. Zero disables NIF blending work.");
	}
	ImGui::SliderFloat("NIF blend width", &settings.BlendWidth, 0.25f, 128.0f, "%.2f units");
	ImGui::SliderFloat("NIF depth bias", &settings.DepthBias, 0.0f, 16.0f, "%.2f units");
	ImGui::SliderFloat("NIF maximum gap", &settings.MaximumGap, 1.0f, 256.0f, "%.1f units");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Maximum gap is clamped above bias + width so the fade cannot jump discontinuously.");
	}

	if (ImGui::TreeNodeEx("Performance and safety")) {
		ImGui::SliderFloat("Culling distance", &settings.MaximumDistance, 0.0f, 32768.0f, "%.0f units");
		if (auto tooltip = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Static overlays outside this camera/eye-centered bubble are rejected before root lookup. Set to 0 for unlimited distance.");
		}
		classifierChanged |= ImGui::SliderFloat("Bounds expansion", &settings.BoundsExpansion, 0.0f, 256.0f, "%.0f units");
		ImGui::TextDisabled("Automatic and saved detected overlays require a plausible opaque sibling.");
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

	const std::size_t compiledAllowCount = compiledRules.authored.Size() + compiledRules.detected.size();
	const std::size_t compiledDenyCount = compiledRules.denied.Size();
	ImGui::TextDisabled("Policy rules are read from MeshBlendingRules.json.");
	ImGui::Text("Compiled rules: %zu allow, %zu deny", compiledAllowCount, compiledDenyCount);
	if (settings.Enabled != 0u && settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kAllowListOnly) && compiledAllowCount == 0u) {
		ImGui::TextDisabled("Overlay blending is inactive: allow-list-only mode has no compiled allow rules.");
	}
	if (settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic)) {
		ImGui::TextWrapped("Automatic overlay detection is selected. Deny rules always win; explicit allows still pass every render-state and static-owner safety gate.");
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

void MeshBlending::DrawMechanismToggles()
{
	bool nifEnabled = settings.NifEnabled != 0u;
	if (ImGui::Checkbox("NIF intersection blending", &nifEnabled))
		settings.NifEnabled = nifEnabled ? 1u : 0u;
}

void MeshBlending::DrawEssentialSettings()
{
	bool enabled = settings.Enabled != 0u;
	if (ImGui::Checkbox("Enable", &enabled)) {
		settings.Enabled = enabled ? 1u : 0u;
	}
	DrawMechanismToggles();
	const std::size_t compiledAllowCount = compiledRules.authored.Size() + compiledRules.detected.size();
	const std::size_t compiledDenyCount = compiledRules.denied.Size();
	ImGui::TextDisabled("Mode: %s; compiled rules: %zu allow, %zu deny", DetectionModeName(settings.DetectionMode), compiledAllowCount, compiledDenyCount);
	if (settings.Enabled != 0u && settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kAllowListOnly) && compiledAllowCount == 0u) {
		ImGui::TextDisabled("Overlay blending is inactive until an allow rule is configured.");
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
	DrawMechanismToggles();
	ImGui::SliderFloat("Culling distance", &settings.MaximumDistance, 0.0f, 32768.0f, "%.0f units");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Rejects overlays beyond this camera/eye-centered distance before owner lookup.");
	}
	if (IsDiscoveryCaptureActive()) {
		ImGui::TextDisabled("Stop Record Visible Material Identities before running a feature-cost measurement.");
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
		{ "NifEnabled", settings.NifEnabled },
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
