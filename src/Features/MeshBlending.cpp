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
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
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
	NifEnabled,
	BlendStrength,
	BlendWidth,
	DepthBias,
	MaximumGap,
	ProjectedSnowEnabled,
	ProjectedSnowEdgeWidth,
	LandscapeLayerBlending,
	LandscapeBlendStrength,
	DetectionMode,
	MaximumDistance,
	BoundsExpansion,
	RequireOverlappingBounds,
	DeveloperLogging);

namespace
{
	static_assert(CSX::MeshBlendingPolicy::EnableMask == static_cast<std::uint32_t>(State::ExtraShaderDescriptors::MeshBlending));
	static_assert(CSX::MeshBlendingLandscapePolicy::ClassMask == static_cast<std::uint32_t>(State::ExtraFeatureDescriptors::MeshBlendingLandscapeClasses));
	static_assert(CSX::MeshBlendingLandscapePolicy::ClassShift == State::MeshBlendingLandscapeClassShift);
	static_assert(CSX::MeshBlendingLandscapePolicy::ClassBits == State::MeshBlendingLandscapeClassBits);
	static_assert(CSX::MeshBlendingLandscapePolicy::LayerCount == State::MeshBlendingLandscapeClassCount);
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

	std::string NormalizeIdentity(std::string_view a_value)
	{
		return CSX::MeshBlendingLandscapePolicy::NormalizeIdentity(a_value);
	}

	std::string NormalizeTexturePath(std::string_view a_path)
	{
		return CSX::MeshBlendingLandscapePolicy::NormalizeTexturePath(a_path);
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
		return IsSafeRuleString(a_form, 512u) && !CSX::MeshBlendingLandscapePolicy::CanonicalFormKey(a_form).empty();
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
	settings = Sanitize(settings);
}

MeshBlending::Settings MeshBlending::Sanitize(Settings a_settings)
{
	a_settings.Enabled = a_settings.Enabled ? 1u : 0u;
	a_settings.NifEnabled = a_settings.NifEnabled ? 1u : 0u;
	a_settings.ProjectedSnowEnabled = a_settings.ProjectedSnowEnabled ? 1u : 0u;
	a_settings.RequireOverlappingBounds = a_settings.RequireOverlappingBounds ? 1u : 0u;
	a_settings.DeveloperLogging = a_settings.DeveloperLogging ? 1u : 0u;
	a_settings.LandscapeLayerBlending = a_settings.LandscapeLayerBlending ? 1u : 0u;
	if (a_settings.DetectionMode > static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic)) {
		a_settings.DetectionMode = static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic);
	}
	a_settings.BlendStrength = Util::ClampFinite(a_settings.BlendStrength, 0.0f, 1.0f, 1.0f);
	a_settings.LandscapeBlendStrength = Util::ClampFinite(a_settings.LandscapeBlendStrength, 0.0f, 1.0f, 1.0f);
	a_settings.BlendWidth = Util::ClampFinite(a_settings.BlendWidth, 0.01f, 256.0f, 12.0f);
	a_settings.DepthBias = Util::ClampFinite(a_settings.DepthBias, 0.0f, 128.0f, 0.25f);
	a_settings.MaximumGap = Util::ClampFinite(a_settings.MaximumGap, 0.01f, 4096.0f, 64.0f);
	a_settings.MaximumGap = std::max(a_settings.MaximumGap, a_settings.DepthBias + a_settings.BlendWidth);
	a_settings.ProjectedSnowEdgeWidth = Util::ClampFinite(a_settings.ProjectedSnowEdgeWidth, 0.0f, 16.0f, 2.0f);
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

std::optional<MeshBlending::LandscapeMaterialKind> MeshBlending::ParseLandscapeMaterialKind(std::string_view a_kind)
{
	return CSX::MeshBlendingLandscapePolicy::ParseMaterialKind(a_kind);
}

const char* MeshBlending::CanonicalLandscapeMaterialKind(LandscapeMaterialKind a_kind)
{
	return CSX::MeshBlendingLandscapePolicy::CanonicalMaterialKind(a_kind).data();
}

MeshBlending::LandscapeMaterialClass MeshBlending::GetLandscapeMaterialClass(LandscapeMaterialKind a_kind)
{
	return CSX::MeshBlendingLandscapePolicy::GetMaterialClass(a_kind);
}

std::shared_ptr<const MeshBlending::CompiledLandscapeRules> MeshBlending::CompileLandscapeRules(const RuleFileState& a_state)
{
	return CSX::MeshBlendingLandscapePolicy::CompilePolicy({ a_state.landscapeAssignments,
		a_state.surfaceMaterialRules, a_state.blendPairRules, a_state.blendPairsExplicit });
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
		sanitized.Enabled != 0u && sanitized.LandscapeLayerBlending != 0u && sanitized.LandscapeBlendStrength > 0.0f ? 1u : 0u,
		sanitized.ProjectedSnowEnabled != 0u ? sanitized.ProjectedSnowEdgeWidth : 0.0f,
		sanitized.LandscapeBlendStrength,
		0.0f
	};
}

bool MeshBlending::IsRuntimeEnabled() const
{
	if (!loaded || !ruleFileMutationAllowed.load(std::memory_order_acquire) ||
		!globals::shaderCache || !globals::shaderCache->IsEnabled() || settings.Enabled == 0u) {
		return false;
	}
	if (settings.LandscapeLayerBlending != 0u && IsFinite(settings.LandscapeBlendStrength) && settings.LandscapeBlendStrength > 0.0f) {
		return true;
	}
	return IsOverlayPolicyEnabled() &&
	       ((settings.NifEnabled != 0u && IsFinite(settings.BlendStrength) && settings.BlendStrength > 0.0f) ||
			   (settings.ProjectedSnowEnabled != 0u && IsFinite(settings.ProjectedSnowEdgeWidth) && settings.ProjectedSnowEdgeWidth > 0.0f));
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

void MeshBlending::CaptureDiscoveredLandscape(const MeshBlendingLandscape::Texture& a_texture, LandscapeMaterialKind a_kind)
{
	if (!a_texture.present || !discoveryCaptureEnabled.load(std::memory_order_relaxed) ||
		!ruleFileMutationAllowed.load(std::memory_order_acquire)) {
		return;
	}

	LandscapeDiagnostic diagnostic;
	diagnostic.Kind = CanonicalLandscapeMaterialKind(a_kind);
	diagnostic.Form = a_texture.form;
	diagnostic.EditorID = a_texture.editorID;
	diagnostic.Diffuse = a_texture.diffuse;
	if (a_texture.materialCount != 0u) {
		const auto& material = a_texture.materials[0];
		diagnostic.Material = !material.form.empty()     ? material.form :
		                      !material.editorID.empty() ? material.editorID :
		                                                   material.name;
	}
	for (auto* identity : { &diagnostic.Form, &diagnostic.EditorID, &diagnostic.Diffuse, &diagnostic.Material }) {
		if (!IsSafeRuleString(*identity, kMaximumRuleLength))
			identity->clear();
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

void MeshBlending::CaptureLandscapeMaterials(RE::TESObjectLAND* a_land, bool a_materialSetupSucceeded)
{
	MeshBlendingLandscape::DiscoverySink discovery;
	if (IsDiscoveryCaptureActive()) {
		discovery.context = this;
		discovery.capture = [](void* context, const MeshBlendingLandscape::Texture& texture, LandscapeMaterialKind kind) {
			static_cast<MeshBlending*>(context)->CaptureDiscoveredLandscape(texture, kind);
		};
	}
	landscape.Capture(a_land, a_materialSetupSucceeded, discovery);
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
	auto compiledLandscape = CompileLandscapeRules(state);
	ApplyRuleFileState(std::move(state), std::move(compiled), std::move(compiledLandscape));
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
				auto selectors = CSX::MeshBlendingLandscapePolicy::CanonicalizeLandscapeSelectors(
					assignment.Form, assignment.EditorID, assignment.Diffuse);
				if (selectors.DiffuseCollapsed()) {
					a_error = std::format("{}.Diffuse has no effective texture path after canonicalization.", context);
					return false;
				}
				const bool hasSelector = selectors.HasSelector();
				assignment.Form = std::move(selectors.form);
				assignment.EditorID = std::move(selectors.editorID);
				assignment.Diffuse = std::move(selectors.diffuse);
				if (!hasSelector) {
					a_error = std::format("{} has no effective Form, EditorID, or Diffuse selector.", context);
					return false;
				}
				if (!assignment.Form.empty()) {
					if (!IsPortableLandscapeFormKey(assignment.Form)) {
						a_error = std::format("{}.Form is not a portable 0xLOCALID~Plugin selector.", context);
						return false;
					} else {
						assignment.Form = CSX::MeshBlendingLandscapePolicy::CanonicalFormKey(assignment.Form);
					}
				}
				a_state.landscapeAssignments.push_back(std::move(assignment));
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
					} else {
						diagnostic.Form = CSX::MeshBlendingLandscapePolicy::CanonicalFormKey(diagnostic.Form);
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

void MeshBlending::ApplyRuleFileState(RuleFileState&& a_state, CompiledRules&& a_compiledRules,
	std::shared_ptr<const CompiledLandscapeRules> a_landscapeRules)
{
	allowList = std::move(a_state.allowList);
	detectedAllowList = std::move(a_state.detectedAllowList);
	denyList = std::move(a_state.denyList);
	landscapeAssignments = std::move(a_state.landscapeAssignments);
	surfaceMaterialRules = std::move(a_state.surfaceMaterialRules);
	blendPairRules = std::move(a_state.blendPairRules);
	savedDiscoveryDiagnostics = std::move(a_state.discoveryDiagnostics);
	blendPairsExplicit = a_state.blendPairsExplicit;
	ruleFileDocument = std::move(a_state.document);
	compiledRules = std::move(a_compiledRules);
	InvalidateClassificationCache();
	landscape.PublishPolicy(std::move(a_landscapeRules));
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
				array.push_back({ { "Kind", entry.Kind },
					{ "Form", entry.Form },
					{ "EditorID", entry.EditorID },
					{ "Diffuse", entry.Diffuse } });
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
				array.push_back({ { "Kind", entry.Kind },
					{ "Form", entry.Form },
					{ "EditorID", entry.EditorID },
					{ "Diffuse", entry.Diffuse },
					{ "Material", entry.Material } });
			}
			ruleFile["DiscoveryDiagnostics"] = std::move(array);
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

	std::map<std::string, LandscapeDiagnostic, std::less<>> mergedLandscape;
	for (const auto& diagnostic : diskState.discoveryDiagnostics) {
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

	RuleFileState proposed = std::move(diskState);
	proposed.detectedAllowList = std::move(proposedDetectedAllowList);
	proposed.discoveryDiagnostics.clear();
	proposed.discoveryDiagnostics.reserve(mergedLandscape.size());
	for (auto& [key, diagnostic] : mergedLandscape) {
		(void)key;
		proposed.discoveryDiagnostics.push_back(std::move(diagnostic));
	}
	auto compiled = CompileRules(proposed);
	auto compiledLandscape = CompileLandscapeRules(proposed);
	if (!WriteRuleFile(proposed, result.error)) {
		ruleFileMutationAllowed.store(true, std::memory_order_release);
		return result;
	}

	ApplyRuleFileState(std::move(proposed), std::move(compiled), std::move(compiledLandscape));
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
	auto compiled = CompileRules(diskState);
	auto compiledLandscape = CompileLandscapeRules(diskState);
	if (!WriteRuleFile(diskState, a_error)) {
		ruleFileMutationAllowed.store(true, std::memory_order_release);
		return false;
	}
	ApplyRuleFileState(std::move(diskState), std::move(compiled), std::move(compiledLandscape));
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
		if (!captureHasCapacity && discoveredLandscapeIdentities.size() >= kMaximumLandscapeIdentities) {
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

bool MeshBlending::TryPrepareProjectedSnow(RE::BSRenderPass* a_pass)
{
	auto* state = globals::state;
	if (!state || !a_pass || !a_pass->geometry || !a_pass->shaderProperty) {
		return false;
	}

	const bool discoveryCaptureActive = IsDiscoveryCaptureActive();
	const bool automaticBlendingEnabled =
		settings.DetectionMode == static_cast<std::uint32_t>(DetectionMode::kStrictAutomatic);
	const bool runtimePolicyEnabled =
		IsRuntimeEnabled() && settings.ProjectedSnowEnabled != 0u &&
		IsFinite(settings.ProjectedSnowEdgeWidth) && settings.ProjectedSnowEdgeWidth > 0.0f && IsOverlayPolicyEnabled();
	if (!runtimePolicyEnabled && !discoveryCaptureActive) {
		return false;
	}

	SourceState source;
	if (!BuildOverlaySourceState(a_pass, source)) {
		return false;
	}
	constexpr auto requiredFlags = ToMask(ShaderFlag::kProjectedUV) |
	                               ToMask(ShaderFlag::kMultiIndexSnow);
	constexpr auto projectedSnowExceptions = ToMask(ShaderFlag::kProjectedUV) | ToMask(ShaderFlag::kMultiIndexSnow);
	if ((source.shaderFlags & requiredFlags) != requiredFlags ||
		(source.shaderFlags & (kExcludedShaderFlags & ~projectedSnowExceptions)) != 0u) {
		return false;
	}
	if (source.material->GetFeature() != MaterialFeature::kMultiIndexTriShapeSnow) {
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
	CSX::MeshBlendingPolicy::ClearDrawDescriptors(descriptor, featureDescriptor, CSX::MeshBlendingLandscapePolicy::ClassMask);

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
		// Borrowed seam layers have no six-slot material policy; keep their native weights.
		const bool seamPass = (state->modifiedPixelDescriptor & static_cast<std::uint32_t>(SIE::ShaderCache::LightingShaderFlags::LandscapeSeams)) != 0u;
		if (landscapePass && !seamPass && runtimeEnabled && settings.LandscapeLayerBlending != 0u &&
			IsFinite(settings.LandscapeBlendStrength) && settings.LandscapeBlendStrength > 0.0f) {
			std::uint32_t packedClasses = 0u;
			if (landscape.TryGetClasses(a_pass, packedClasses)) {
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
	ImGui::Text("LAND snapshots: %llu hits, %llu misses",
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
	const auto compiledLandscapePairCount = landscape.GetBlendPairCount();
	{
		std::scoped_lock lock(discoveryMutex);
		discoveredNifCount = discoveredRuleKeys.size();
		nifDuplicateCount = discoveryDuplicateObservations;
		nifDroppedCount = discoveryDropped;
		discoveredLandscapeCount = discoveredLandscapeIdentities.size();
		landscapeDuplicateCount = landscapeDiscoveryDuplicateObservations;
		landscapeDroppedCount = landscapeDiscoveryDropped;
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
				discoveryStatus = std::format("Could not save detected materials: {}", result.error);
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
	DrawMechanismToggles();

	bool classifierChanged = false;
	int detectionMode = static_cast<int>(settings.DetectionMode);
	static constexpr const char* detectionModes[] = { "Disabled", "Allow list only", "Automatic" };
	if (ImGui::Combo("Detection mode", &detectionMode, detectionModes, static_cast<int>(std::size(detectionModes)))) {
		settings.DetectionMode = static_cast<std::uint32_t>(detectionMode);
		classifierChanged = true;
	}
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Selects NIF and projected-snow overlays. Automatic and saved detected rules require current static-owner, render-state, bounds, and sibling checks. Deny rules always win.");
	}

	ImGui::SliderFloat("NIF blend strength", &settings.BlendStrength, 0.0f, 1.0f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Controls NIF intersection fades. Zero disables NIF blending work.");
	}
	ImGui::SliderFloat("LAND blend strength", &settings.LandscapeBlendStrength, 0.0f, 1.0f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Rebalances already-active LAND texture weights using bounded material rules: soft deposits over hard, "
			"authored soft-layer mixtures are preserved, and hard/hard stays crisp. Unknown identities preserve authored weights.");
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

void MeshBlending::DrawMechanismToggles()
{
	bool nifEnabled = settings.NifEnabled != 0u;
	if (ImGui::Checkbox("NIF intersection blending", &nifEnabled))
		settings.NifEnabled = nifEnabled ? 1u : 0u;
	bool snowEnabled = settings.ProjectedSnowEnabled != 0u;
	if (ImGui::Checkbox("Projected snow edges", &snowEnabled))
		settings.ProjectedSnowEnabled = snowEnabled ? 1u : 0u;
	bool landscapeEnabled = settings.LandscapeLayerBlending != 0u;
	if (ImGui::Checkbox("LAND/LTEX material blending", &landscapeEnabled))
		settings.LandscapeLayerBlending = landscapeEnabled ? 1u : 0u;
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
	ImGui::SliderFloat("NIF culling distance", &settings.MaximumDistance, 0.0f, 32768.0f, "%.0f units");
	if (auto tooltip = Util::HoverTooltipWrapper()) {
		ImGui::Text("Applies only to NIF classification. LAND work is already limited to submitted nearby terrain draws.");
	}
	if (IsDiscoveryCaptureActive()) {
		ImGui::TextDisabled("Stop Record Visible Material Identities before running a feature-cost measurement.");
	}
	if (a_advanced) {
		ImGui::Text("Mode: %s; active draws: %u last frame, %u this frame", DetectionModeName(settings.DetectionMode), diagnostics.lastFrameActive, diagnostics.currentFrameActive);
		ImGui::Text("LAND snapshots: %llu hits, %llu misses",
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
		{ "NifEnabled", settings.NifEnabled },
		{ "ProjectedSnowEnabled", settings.ProjectedSnowEnabled },
		{ "LandscapeLayerBlending", settings.LandscapeLayerBlending },
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
