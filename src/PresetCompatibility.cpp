#include "PresetCompatibility.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <charconv>
#include <format>
#include <mutex>
#include <optional>
#include <utility>

namespace
{
	struct Version
	{
		std::uint32_t major{ 0 };
		std::uint32_t minor{ 0 };
		std::string runtime;

		auto operator<=>(const Version&) const = default;
	};

	std::mutex g_evaluationMutex;
	PresetCompatibility::Evaluation g_evaluation;

	std::optional<std::uint32_t> ParseComponent(std::string_view a_value)
	{
		if (a_value.empty())
			return std::nullopt;

		std::uint32_t result = 0;
		const auto parsed = std::from_chars(a_value.data(), a_value.data() + a_value.size(), result);
		if (parsed.ec != std::errc{} || parsed.ptr != a_value.data() + a_value.size())
			return std::nullopt;
		return result;
	}

	std::optional<Version> ParseVersion(std::string_view a_value, bool a_requireRuntime)
	{
		if (a_value.starts_with("CSX "))
			a_value.remove_prefix(4);

		std::string_view runtime;
		if (const auto separator = a_value.find('-'); separator != std::string_view::npos) {
			runtime = a_value.substr(separator + 1);
			a_value = a_value.substr(0, separator);
		}
		if (a_requireRuntime && runtime.empty())
			return std::nullopt;

		const auto dot = a_value.find('.');
		if (dot == std::string_view::npos || a_value.find('.', dot + 1) != std::string_view::npos)
			return std::nullopt;
		const auto major = ParseComponent(a_value.substr(0, dot));
		const auto minor = ParseComponent(a_value.substr(dot + 1));
		if (!major || !minor)
			return std::nullopt;

		return Version{ *major, *minor, std::string(runtime) };
	}

	bool IsSha256(std::string_view a_value)
	{
		if (a_value.size() != 64)
			return false;
		for (const auto character : a_value) {
			if (!std::isxdigit(static_cast<unsigned char>(character)))
				return false;
		}
		return true;
	}

	PresetCompatibility::Evaluation Reject(
		PresetCompatibility::Evaluation a_evaluation,
		std::string a_message)
	{
		a_evaluation.disposition = PresetCompatibility::Disposition::kRejected;
		a_evaluation.message = std::move(a_message);
		return a_evaluation;
	}
}

PresetCompatibility::Evaluation PresetCompatibility::Evaluate(
	const nlohmann::json& a_settings,
	std::string_view a_currentVersionLabel)
{
	Evaluation evaluation;
	evaluation.currentVersion = std::string(a_currentVersionLabel);

	const auto contractIt = a_settings.find(kSettingsKey);
	if (contractIt == a_settings.end()) {
		evaluation.message = "Settings file has no preset compatibility marker; accepted as a legacy or user-authored configuration.";
		return evaluation;
	}
	if (!contractIt->is_object())
		return Reject(std::move(evaluation), "Preset compatibility marker must be a JSON object.");

	const auto& contract = *contractIt;
	const auto contractVersionIt = contract.find("contractVersion");
	if (contractVersionIt == contract.end() || !contractVersionIt->is_number_unsigned())
		return Reject(std::move(evaluation), "Preset compatibility contractVersion is missing or invalid.");
	evaluation.contractVersion = contractVersionIt->get<std::uint32_t>();
	if (evaluation.contractVersion != kContractVersion) {
		return Reject(
			std::move(evaluation),
			std::format(
				"Preset compatibility contract {} is not supported by this build (supported: {}).",
				evaluation.contractVersion,
				kContractVersion));
	}

	const auto readRequiredString = [&](std::string_view a_key, std::string& a_output) {
		const auto valueIt = contract.find(a_key);
		if (valueIt == contract.end() || !valueIt->is_string() || valueIt->get_ref<const std::string&>().empty())
			return false;
		a_output = valueIt->get<std::string>();
		return true;
	};
	if (!readRequiredString("presetId", evaluation.presetId) ||
		!readRequiredString("presetVersion", evaluation.presetVersion)) {
		return Reject(std::move(evaluation), "Preset compatibility identity is missing or invalid.");
	}

	const auto targetIt = contract.find("target");
	if (targetIt == contract.end() || !targetIt->is_object())
		return Reject(std::move(evaluation), "Preset compatibility target is missing or invalid.");
	const auto& target = *targetIt;
	const auto readTargetString = [&](std::string_view a_key, std::string& a_output) {
		const auto valueIt = target.find(a_key);
		if (valueIt == target.end() || !valueIt->is_string() || valueIt->get_ref<const std::string&>().empty())
			return false;
		a_output = valueIt->get<std::string>();
		return true;
	};
	if (!readTargetString("runtime", evaluation.targetRuntime) ||
		!readTargetString("minimumVersion", evaluation.minimumVersion) ||
		!readTargetString("maximumVersionExclusive", evaluation.maximumVersionExclusive)) {
		return Reject(std::move(evaluation), "Preset compatibility target range is missing or invalid.");
	}

	const auto settingsContractIt = contract.find("settingsContract");
	if (settingsContractIt == contract.end() || !settingsContractIt->is_object())
		return Reject(std::move(evaluation), "Preset settings contract is missing or invalid.");
	const auto revisionIt = settingsContractIt->find("revision");
	const auto sourceHashIt = settingsContractIt->find("sourceTreeSha256");
	if (revisionIt == settingsContractIt->end() || !revisionIt->is_number_unsigned() ||
		sourceHashIt == settingsContractIt->end() || !sourceHashIt->is_string()) {
		return Reject(std::move(evaluation), "Preset settings contract identity is missing or invalid.");
	}
	evaluation.settingsContractRevision = revisionIt->get<std::uint32_t>();
	evaluation.settingsContractSourceTreeSha256 = sourceHashIt->get<std::string>();
	if (evaluation.settingsContractRevision != kSettingsContractRevision) {
		return Reject(
			std::move(evaluation),
			std::format(
				"Preset settings contract revision {} is not supported by this build (supported: {}).",
				evaluation.settingsContractRevision,
				kSettingsContractRevision));
	}
	if (!IsSha256(evaluation.settingsContractSourceTreeSha256))
		return Reject(std::move(evaluation), "Preset settings contract sourceTreeSha256 is invalid.");

	const auto current = ParseVersion(a_currentVersionLabel, true);
	const auto minimum = ParseVersion(evaluation.minimumVersion, false);
	const auto maximum = ParseVersion(evaluation.maximumVersionExclusive, false);
	if (!current)
		return Reject(std::move(evaluation), "This CSX build has an unrecognized version label; marked preset was not applied.");
	if (!minimum || !maximum || *minimum >= *maximum)
		return Reject(std::move(evaluation), "Preset compatibility version range is invalid.");
	if (current->runtime != evaluation.targetRuntime) {
		return Reject(
			std::move(evaluation),
			std::format(
				"Preset targets the {} runtime, but the loaded CSX build is {}.",
				evaluation.targetRuntime,
				current->runtime));
	}

	const Version currentWithoutRuntime{ current->major, current->minor, {} };
	if (currentWithoutRuntime < *minimum || currentWithoutRuntime >= *maximum) {
		return Reject(
			std::move(evaluation),
			std::format(
				"Preset {} {} requires CSX {} >= {} and < {}; loaded build is {}.",
				evaluation.presetId,
				evaluation.presetVersion,
				evaluation.targetRuntime,
				evaluation.minimumVersion,
				evaluation.maximumVersionExclusive,
				evaluation.currentVersion));
	}

	evaluation.disposition = Disposition::kCompatible;
	evaluation.message = std::format(
		"Preset {} {} is compatible with {}.",
		evaluation.presetId,
		evaluation.presetVersion,
		evaluation.currentVersion);
	return evaluation;
}

void PresetCompatibility::Publish(Evaluation a_evaluation)
{
	std::scoped_lock lock(g_evaluationMutex);
	g_evaluation = std::move(a_evaluation);
}

PresetCompatibility::Evaluation PresetCompatibility::GetPublished()
{
	std::scoped_lock lock(g_evaluationMutex);
	return g_evaluation;
}

std::string_view PresetCompatibility::GetDispositionName(Disposition a_disposition) noexcept
{
	switch (a_disposition) {
	case Disposition::kUnmarked:
		return "unmarked";
	case Disposition::kCompatible:
		return "compatible";
	case Disposition::kRejected:
		return "rejected";
	default:
		return "unknown";
	}
}

nlohmann::json PresetCompatibility::ToJson(const Evaluation& a_evaluation)
{
	return {
		{ "disposition", GetDispositionName(a_evaluation.disposition) },
		{ "shouldApply", a_evaluation.ShouldApply() },
		{ "contractVersion", a_evaluation.contractVersion },
		{ "presetId", a_evaluation.presetId },
		{ "presetVersion", a_evaluation.presetVersion },
		{ "targetRuntime", a_evaluation.targetRuntime },
		{ "minimumVersion", a_evaluation.minimumVersion },
		{ "maximumVersionExclusive", a_evaluation.maximumVersionExclusive },
		{ "settingsContractRevision", a_evaluation.settingsContractRevision },
		{ "settingsContractSourceTreeSha256", a_evaluation.settingsContractSourceTreeSha256 },
		{ "currentVersion", a_evaluation.currentVersion },
		{ "message", a_evaluation.message },
	};
}
