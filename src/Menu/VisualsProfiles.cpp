#include "VisualsProfiles.h"

#include "Presets/LookProfiles.h"
#include "Presets/LookSceneRuntime.h"
#include "ProfileExchange.h"
#include "SettingsPage.h"
#include "Utils/UI.h"

namespace MenuUI
{
	ProfileExchangeState& VisualsProfileExchange()
	{
		static ProfileExchangeState exchange;
		return exchange;
	}

	void DrawVisualsProfiles()
	{
		auto& exchange = VisualsProfileExchange();
		static ActionFeedback feedback;
		static auto catalog = nlohmann::json::object();
		auto& profiles = Presets::LookProfiles::Get();
		const auto refresh = [&] { catalog = profiles.Inspect(); };
		if (catalog.empty())
			refresh();
		SectionHeading("Visuals profile", "One profile controls appearance across features and saved places. Performance choices remain separate. Save settings keeps your selection and personal edits without changing the shared pack. Export includes base appearance, saved places and this pack's scene rules. Separate editor weather and form attachments are not included.");
		if (ImGui::Button("Refresh profiles"))
			refresh();
		ImGui::SameLine();
		const auto* blocked = profiles.MutationBlockReason();
		{
			const auto disabled = Util::DisableGuard(blocked != nullptr);
			if (ImGui::Button("Restore profile defaults")) {
				feedback.error = !profiles.Restore(feedback.message);
				if (!feedback.error)
					feedback.message = "Profile defaults restored. Save settings to keep them.";
			}
		}
		Util::AddTooltip(blocked ? blocked : "Discard personal appearance edits for this profile. Other profiles and performance choices stay unchanged.");
		struct Card
		{
			std::string id, name, detail, help;
			bool enabled;
		};
		std::vector<Card> text{ { "", "Installed settings", "Your original appearance settings.", "Restore the installed appearance and its personal edits.", !blocked } };
		int selected = profiles.ActiveId().empty() ? 0 : -1;
		for (const auto& pack : catalog.value("packs", nlohmann::json::array())) {
			const auto description = pack["manifest"].find("description");
			std::string help = description != pack["manifest"].end() && description->is_string() ? description->get<std::string>() : "";
			for (const auto* key : { "errors", "unavailable", "notices" })
				for (const auto& message : pack[key]) help += "\n" + message.get<std::string>();
			const bool compatible = pack["compatible"].get<bool>();
			text.push_back({ pack["id"], pack["name"], pack["summary"], help, !blocked && compatible });
			if (pack["id"] == profiles.ActiveId())
				selected = static_cast<int>(text.size()) - 1;
		}
		std::vector<Choice> cards;
		for (const auto& item : text) cards.push_back({ item.id.c_str(), item.name.c_str(), item.detail.c_str(), item.help.c_str(), item.enabled });
		if (selected == -1)
			PageNotice("Active profile is not listed. Refresh profiles.", NoticeKind::Unavailable);
		const int clicked = ChoiceCards("Visuals profiles", selected, cards);
		if (clicked >= 0 && clicked != selected) {
			feedback.error = !(clicked == 0 ? profiles.Clear(feedback.message) : profiles.Select(text[clicked].id, feedback.message));
			if (!feedback.error)
				feedback.message = "Profile applied. Save settings to keep it.";
		}
		DrawProfileExchange(exchange, "Share Visuals", Presets::LookProfiles::RootPath(), true, blocked, [&](const auto& source, const auto& name) {
				feedback.error = !profiles.Import(source, name, feedback.message);
				if (!feedback.error) { feedback.message = "Profile imported and applied. Save settings to keep it."; refresh(); } }, [&](const auto& folder, const auto& name) {
				feedback.error = !profiles.Export(name, feedback.message, folder);
				if (!feedback.error) { feedback.message = "Visuals profile exported."; refresh(); } });
		if (!profiles.GetLoadError().empty())
			PageNotice("Saved Visuals profile unavailable.", NoticeKind::Unavailable, profiles.GetLoadError());
		if (catalog.contains("discoveryError"))
			PageNotice("Could not read the profile library.", NoticeKind::Error, catalog["discoveryError"].get<std::string>());
		if (!Presets::LookSceneRuntime::Error().empty())
			PageNotice("Visuals scene rules stopped.", NoticeKind::Error, Presets::LookSceneRuntime::Error());
		const auto summary = std::string_view(feedback.message).substr(0, feedback.message.find('\n'));
		PageNotice(summary, feedback.error ? NoticeKind::Error : NoticeKind::Information, feedback.message);
	}
}
