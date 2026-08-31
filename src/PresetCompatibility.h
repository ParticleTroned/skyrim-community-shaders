#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace PresetCompatibility
{
	inline constexpr std::string_view kSettingsKey = "Preset Compatibility";
	inline constexpr std::uint32_t kContractVersion = 1;
	inline constexpr std::uint32_t kSettingsContractRevision = 3;

	enum class Disposition
	{
		kUnmarked,
		kCompatible,
		kRejected
	};

	struct Evaluation
	{
		Disposition disposition{ Disposition::kUnmarked };
		std::uint32_t contractVersion{ 0 };
		std::string presetId;
		std::string presetVersion;
		std::string targetRuntime;
		std::string minimumVersion;
		std::string maximumVersionExclusive;
		std::uint32_t settingsContractRevision{ 0 };
		std::string settingsContractSourceTreeSha256;
		std::string currentVersion;
		std::string message;

		bool ShouldApply() const noexcept { return disposition != Disposition::kRejected; }
	};

	/** Evaluates optional preset metadata without modifying the settings document. */
	Evaluation Evaluate(const nlohmann::json& a_settings, std::string_view a_currentVersionLabel);

	/** Publishes the last SettingsUser compatibility decision for diagnostics. */
	void Publish(Evaluation a_evaluation);

	/** Returns the last published SettingsUser compatibility decision. */
	Evaluation GetPublished();

	/** Returns a stable diagnostic name for a compatibility disposition. */
	std::string_view GetDispositionName(Disposition a_disposition) noexcept;

	/** Serializes a compatibility decision for diagnostic APIs. */
	nlohmann::json ToJson(const Evaluation& a_evaluation);
}
