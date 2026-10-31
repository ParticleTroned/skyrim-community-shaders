#include "Features/MeshBlendingPolicy.h"
#include "Utils/Finite.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using json = nlohmann::json;

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct FileBoundary
	{
		std::optional<std::string> contents;
		std::optional<std::string> concurrentContents;
		std::function<void()> beforeWrite;
		bool failWrite = false;
		std::size_t writes = 0u;
	} file;

	std::filesystem::path GetRuleFilePath() { return "MeshBlendingRules.json"; }
}

namespace logger
{
	template <class... Args>
	void info(std::string_view, Args&&...)
	{}
	template <class... Args>
	void warn(std::string_view, Args&&...)
	{}
}

namespace Util
{
	namespace FileHelpers
	{
		bool ReadTextFileBounded(const std::filesystem::path&, std::uintmax_t limit,
			std::optional<std::string>& contents, std::string& error)
		{
			if (file.contents && file.contents->size() > limit) {
				error = "File limit exceeded";
				return false;
			}
			contents = file.contents;
			return true;
		}

		bool WriteTextFileAtomicIfUnchanged(const std::filesystem::path&, std::string_view contents,
			const std::optional<std::string>& expected, std::uintmax_t limit, std::string& error)
		{
			++file.writes;
			if (file.beforeWrite)
				file.beforeWrite();
			if (file.concurrentContents)
				file.contents = file.concurrentContents;
			if (file.failWrite || file.contents != expected || contents.size() > limit) {
				error = "Injected conditional-write failure";
				return false;
			}
			file.contents = std::string(contents);
			return true;
		}
	}
}

// The engine and file operations are boundaries; all parsing, merging,
// serialization and policy publication below execute production methods.
namespace globals
{
	struct ShaderCache
	{
		bool enabled = true;
		bool IsEnabled() const { return enabled; }
	} shaderCacheValue;
	auto* shaderCache = &shaderCacheValue;
}

#define STATIC_ASSERT_ALIGNAS_16(Type) static_assert(alignof(Type) == 16)
struct MeshBlending
{
	using CompiledRules = CSX::MeshBlendingPolicy::CompiledRules;
#include "mesh_blending_rule_members.h"
	Settings settings;
	bool loaded = true;
	std::vector<OverrideRule> allowList;
	std::vector<OverrideRule> detectedAllowList;
	std::vector<OverrideRule> denyList;
	CompiledRules compiledRules;
	json ruleFileDocument = json::object();
	std::atomic_bool discoveryCaptureEnabled = false;
	mutable std::mutex discoveryMutex;
	std::set<std::pair<std::string, std::string>> discoveredRuleKeys;
	std::size_t discoveryDuplicateObservations = 0u;
	std::size_t discoveryDropped = 0u;
	std::atomic_bool ruleFileMutationAllowed = true;
	std::string ruleFileLoadError;
	std::uint32_t cacheGeneration = 0u;

	static Settings Sanitize(Settings settings);
	PerFrame GetCommonBufferData() const;
	bool IsOverlayPolicyEnabled() const;
	bool IsRuntimeEnabled() const;
	void InvalidateClassificationCache() { ++cacheGeneration; }
	bool IsDiscoveryCaptureSaturated() const;
	void SetDiscoveryCaptureEnabled(bool enabled);
	static CompiledRules CompileRules(const RuleFileState& state);
	bool LoadRuleFile();
	bool ReadRuleFileState(RuleFileState& state, bool& notFound, std::string& error) const;
	void ApplyRuleFileState(RuleFileState&& state, CompiledRules&& compiled);
	bool WriteRuleFile(const RuleFileState& state, std::string& error) const;
	DiscoverySaveResult SaveDiscoveredRules();
	bool ClearSavedRules(std::string& error);
	void ClearDiscoveredRules();
};

#undef STATIC_ASSERT_ALIGNAS_16

#include "mesh_blending_rules_under_test.h"

namespace
{
	using CSX::MeshBlendingPolicy::RuleMatch;
	const std::string model = "meshes/example.nif";
	const std::string source = "root/source[1]";

	json EmptyDocument()
	{
		return json{ { "SchemaVersion", 4 }, { "AllowList", json::array() }, { "DenyList", json::array() } };
	}

	json Rule(std::string_view ruleModel, std::string_view ruleNode)
	{
		return json{ { "Model", ruleModel }, { "NodePath", ruleNode } };
	}

	void Load(MeshBlending& feature, const json& document)
	{
		file = {};
		file.contents = document.dump();
		Require(feature.LoadRuleFile(), "valid initial policy failed to load");
	}

	void TestFailedSavePreservesActivePolicy()
	{
		MeshBlending feature;
		auto initial = EmptyDocument();
		initial["AllowList"].push_back(Rule(model, source));
		Load(feature, initial);
		const auto generationAfterStoppingCapture = feature.cacheGeneration + 1u;
		feature.discoveredRuleKeys.emplace("meshes/new.nif", source);
		feature.discoveryCaptureEnabled.store(true);
		const auto captured = feature.discoveredRuleKeys;
		auto edited = EmptyDocument();
		edited["DenyList"].push_back(Rule(model, source));
		edited["Extension"] = { { "Keep", 17 } };
		file.contents = edited.dump();
		file.failWrite = true;
		file.beforeWrite = [&] {
			Require(feature.compiledRules.Match(model, source) == RuleMatch::kAuthored,
				"staged disk rules changed active policy before write");
			Require(feature.cacheGeneration == generationAfterStoppingCapture, "staging invalidated active cache");
		};
		const auto result = feature.SaveDiscoveredRules();
		Require(!result.success && !result.error.empty() && file.writes == 1u, "write failure was not returned");
		Require(feature.compiledRules.Match(model, source) == RuleMatch::kAuthored && feature.denyList.empty(),
			"failed save replaced active policy");
		Require(feature.cacheGeneration == generationAfterStoppingCapture,
			"failed save changed live policy generations");
		Require(feature.ruleFileDocument == initial && feature.discoveredRuleKeys == captured,
			"failed save discarded the active document or pending capture");
		Require(feature.ruleFileMutationAllowed.load() && !feature.discoveryCaptureEnabled.load(),
			"failed write did not reopen mutation or stop recording");
		Require(file.contents == edited.dump(), "failed save replaced the disk document");
	}

	void TestSuccessfulSaveMergesDiskPolicy()
	{
		MeshBlending feature;
		Load(feature, EmptyDocument());
		auto edited = EmptyDocument();
		edited["SchemaVersion"] = 1;
		edited["AllowList"].push_back(Rule("meshes/manual.nif", {}));
		edited["DenyList"].push_back(Rule("meshes/denied.nif", {}));
		edited["DetectedAllowList"] = json::array({ Rule("meshes/existing.nif", source) });
		edited["Extension"] = { { "Keep", "author data" } };
		file.contents = edited.dump();
		feature.discoveredRuleKeys = {
			{ "meshes/manual.nif", source }, { "meshes/denied.nif", source },
			{ "meshes/existing.nif", source }, { model, source }
		};
		const auto result = feature.SaveDiscoveredRules();
		Require(result.success && result.added == 1u && result.alreadyAllowed == 2u && result.denied == 1u,
			"save did not filter discoveries against current disk policy");
		Require(feature.compiledRules.Match(model, source) == RuleMatch::kDetected,
			"saved discovery acquired authored trust");
		Require(feature.compiledRules.Match("meshes/manual.nif", source) == RuleMatch::kAuthored &&
					feature.compiledRules.Match("meshes/denied.nif", source) == RuleMatch::kDeny,
			"successful save did not publish disk author edits");
		const auto saved = json::parse(*file.contents);
		Require(saved.at("SchemaVersion") == 4 && saved.at("Extension") == edited.at("Extension") &&
					saved.at("AllowList") == edited.at("AllowList") && saved.at("DenyList") == edited.at("DenyList"),
			"save lost authored rules or extension fields");
		Require(saved.at("DetectedAllowList").size() == 2u && feature.discoveredRuleKeys.empty(),
			"successful save did not persist and clear bounded capture");
		Require(feature.LoadRuleFile() && feature.compiledRules.Match(model, source) == RuleMatch::kDetected,
			"reload promoted a generated rule");
	}

	void TestConcurrentFileChangeAndClearFailure()
	{
		MeshBlending feature;
		auto initial = EmptyDocument();
		initial["DetectedAllowList"] = json::array({ Rule(model, source) });
		initial["Extension"] = true;
		Load(feature, initial);
		feature.discoveredRuleKeys.emplace("meshes/new.nif", source);
		const auto generation = feature.cacheGeneration;
		file.concurrentContents = "author replaced file during save";
		const auto result = feature.SaveDiscoveredRules();
		Require(!result.success && file.contents == file.concurrentContents &&
					feature.cacheGeneration == generation && feature.detectedAllowList.size() == 1u &&
					feature.discoveredRuleKeys.size() == 1u,
			"concurrent edit changed active policy or lost capture");

		file.concurrentContents.reset();
		file.contents = initial.dump();
		file.failWrite = true;
		std::string error;
		Require(!feature.ClearSavedRules(error) && feature.cacheGeneration == generation &&
					feature.detectedAllowList.size() == 1u && feature.discoveredRuleKeys.size() == 1u,
			"failed clear published or discarded capture");
		file.failWrite = false;
		Require(feature.ClearSavedRules(error), "valid clear failed");
		const auto saved = json::parse(*file.contents);
		Require(!saved.contains("DetectedAllowList") && saved.at("Extension") == true &&
					feature.detectedAllowList.empty() && feature.discoveredRuleKeys.empty(),
			"clear lost extension fields or retained generated policy");
	}

	void TestParserLimitsAndFailClosedState()
	{
		MeshBlending feature;
		Load(feature, EmptyDocument());
		const auto accepts = [&](const json& document) {
			file.contents = document.dump();
			MeshBlending::RuleFileState state;
			bool notFound = false;
			std::string error;
			return feature.ReadRuleFileState(state, notFound, error);
		};
		for (const auto& rule : { Rule("/", source), Rule(model, "/"), Rule({}, {}) }) {
			auto document = EmptyDocument();
			document["AllowList"].push_back(rule);
			Require(!accepts(document), "parser accepted a collapsed selector");
		}
		auto document = EmptyDocument();
		document["DetectedAllowList"] = json::array({ Rule("meshes/*.nif", source) });
		Require(!accepts(document), "parser accepted a generated wildcard");
		document = EmptyDocument();
		document["AllowList"].push_back(Rule(std::string(513u, 'x'), source));
		Require(!accepts(document), "parser accepted an oversized selector");
		document = EmptyDocument();
		for (std::size_t index = 0u; index < 1024u; ++index)
			document["AllowList"].push_back(Rule(model, source));
		Require(accepts(document), "parser rejected the exact rule limit");
		document["DetectedAllowList"] = json::array({ Rule("meshes/other.nif", source) });
		Require(!accepts(document), "parser accepted combined allow capacity overflow");
		document = EmptyDocument();
		document["DetectedAllowList"] = json::array({ Rule("Data\\Meshes\\Example.nif", source), Rule(model, source) });
		file.contents = document.dump();
		Require(feature.LoadRuleFile() && feature.detectedAllowList.size() == 1u,
			"canonical duplicate generated identities were not deduplicated");
		feature.discoveredRuleKeys.emplace("meshes/new.nif", source);
		file.contents = "{ invalid";
		const auto writes = file.writes;
		const auto generation = feature.cacheGeneration;
		Require(!feature.SaveDiscoveredRules().success && !feature.ruleFileMutationAllowed.load() &&
					file.writes == writes && feature.cacheGeneration == generation && feature.discoveredRuleKeys.size() == 1u,
			"malformed external policy did not fail closed while preserving captures");
	}
}

void TestSettingsAndPayload()
{
	static_assert(sizeof(MeshBlending::PerFrame) == 32u);
	static_assert(offsetof(MeshBlending::PerFrame, BlendStrength) == 0u);
	static_assert(offsetof(MeshBlending::PerFrame, BlendWidth) == 4u);
	static_assert(offsetof(MeshBlending::PerFrame, DepthBias) == 8u);
	static_assert(offsetof(MeshBlending::PerFrame, MaximumGap) == 12u);
	static_assert(offsetof(MeshBlending::PerFrame, Padding0) == 16u);
	static_assert(offsetof(MeshBlending::PerFrame, ProjectedSnowEdgeWidth) == 20u);
	static_assert(offsetof(MeshBlending::PerFrame, Padding2) == 24u);
	static_assert(offsetof(MeshBlending::PerFrame, Padding3) == 28u);
	MeshBlending feature;
	feature.settings = json::object().get<MeshBlending::Settings>();
	Require(feature.IsRuntimeEnabled(), "default enabled NIF controls were inactive");
	feature.settings.ProjectedSnowEnabled = 0u;

	feature.settings.NifEnabled = 0u;
	Require(!feature.IsRuntimeEnabled(), "disabled NIF mechanism remained active");
	feature.settings.NifEnabled = 1u;
	feature.settings.BlendStrength = 0.0f;
	Require(!feature.IsRuntimeEnabled(), "zero NIF strength remained active");
	feature.settings.NifEnabled = 0u;
	feature.settings.ProjectedSnowEnabled = 1u;
	Require(feature.IsRuntimeEnabled(), "snow-only operation depended on NIF strength");
	feature.settings.ProjectedSnowEdgeWidth = 0.0f;
	Require(!feature.IsRuntimeEnabled(), "zero snow width remained active");
	feature.settings.ProjectedSnowEdgeWidth = 2.0f;
	feature.settings.ProjectedSnowEnabled = 0u;
	feature.settings.NifEnabled = 1u;

	feature.settings.BlendStrength = 1.0f;
	feature.settings.DetectionMode = static_cast<std::uint32_t>(MeshBlending::DetectionMode::kAllowListOnly);
	Require(!feature.IsRuntimeEnabled(), "empty allow-list policy was active");
	feature.compiledRules.AddDetected("meshes/example.nif", "root/source[1]");
	Require(feature.IsRuntimeEnabled(), "generated exact rule did not enable allow-list selection");
	feature.settings.Enabled = 0u;
	Require(!feature.IsRuntimeEnabled(), "master toggle did not disable mechanisms");
	feature.settings.Enabled = 1u;
	globals::shaderCacheValue.enabled = false;
	Require(!feature.IsRuntimeEnabled(), "disabled shader cache admitted the mechanism");
	globals::shaderCacheValue.enabled = true;
	feature.ruleFileMutationAllowed.store(false);
	Require(!feature.IsRuntimeEnabled(), "invalid rule-file gate admitted the mechanism");
	feature.ruleFileMutationAllowed.store(true);
	feature.settings.BlendStrength = std::numeric_limits<float>::quiet_NaN();
	feature.settings.BlendWidth = 256.0f;
	feature.settings.DepthBias = 128.0f;
	feature.settings.MaximumGap = 0.0f;
	const auto payload = feature.GetCommonBufferData();
	Require(payload.BlendStrength == 1.0f && payload.MaximumGap == 384.0f,
		"GPU payload did not apply the shared finite-value sanitizer");
	Require(payload.Padding0 == 0u && payload.Padding2 == 0.0f && payload.Padding3 == 0.0f, "GPU padding was not zero");
}

int main()
{
	try {
		TestSettingsAndPayload();
		TestFailedSavePreservesActivePolicy();
		TestSuccessfulSaveMergesDiskPolicy();
		TestConcurrentFileChangeAndClearFailure();
		TestParserLimitsAndFailClosedState();
		std::cout << "Mesh rule transactions and parser limits passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
