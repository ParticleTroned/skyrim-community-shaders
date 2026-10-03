#include "StabilizerIntegration.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "StabilizerSettings.h"
#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

namespace VRFpsStabilizer
{
	namespace
	{
		using json = nlohmann::json;

		json StatusJson()
		{
			const auto current = Status();
			return { { "available", current.available }, { "pending", current.pending },
				{ "restartRequired", current.restartRequired }, { "build", current.build },
				{ "revision", current.revision }, { "message", current.message } };
		}

		bool EditableSection(ConfigFile file, std::string_view section)
		{
			if (file == ConfigFile::Locations) {
				return std::ranges::any_of(kLocationSections, [&](const char* name) { return Equal(section, name); });
			}
			return std::ranges::any_of(kEventSections, [&](const char* name) { return Equal(section, name); }) ||
			       (section.size() == 6 && Equal(section.substr(0, 5), "Level") && section[5] >= '0' && section[5] <= '9');
		}

		json Execute(const json& args)
		{
			const auto action = args.value("action", std::string("status"));
			if (action == "status")
				return { { "status", StatusJson() } };
			if (!REL::Module::IsVR())
				return { { "error", "Skyrim VR is required" } };
			const auto fileName = args.value("file", std::string("main"));
			if (fileName != "main" && fileName != "locations")
				return { { "error", "file must be main or locations" } };
			const auto file = fileName == "main" ? ConfigFile::Main : ConfigFile::Locations;
			std::string error;
			if (action == "reload") {
				const bool queued = RequestReload(file, error);
				return { { "queued", queued }, { "error", error }, { "status", StatusJson() } };
			}
			IniDocument document;
			if (!Load(file, document, error))
				return { { "error", error } };
			if (action == "read") {
				json controls = json::array();
				if (file == ConfigFile::Main) {
					for (const auto& setting : kSettings)
						controls.push_back({ { "key", setting.key }, { "label", setting.label }, { "group", setting.group },
							{ "kind", static_cast<int>(setting.kind) }, { "minimum", setting.minimum }, { "maximum", setting.maximum }, { "help", setting.help } });
				}
				return { { "contents", document.text }, { "path", ConfigPath(file).string() }, { "controls", controls }, { "status", StatusJson() } };
			}
			if (action != "save")
				return { { "error", "Unknown Stabilizer action" } };
			if (!args.contains("expectedContents") || !args["expectedContents"].is_string() ||
				args["expectedContents"].get_ref<const std::string&>() != document.original)
				return { { "error", "expectedContents must match the most recently read INI bytes" } };
			const auto settings = args.value("settings", json::object());
			const auto sections = args.value("sections", json::object());
			if (!settings.is_object() || !sections.is_object() || (settings.empty() && sections.empty()))
				return { { "error", "Provide nonempty settings and/or sections objects with string values" } };
			for (const auto& [key, value] : settings.items()) {
				if (!value.is_string())
					return { { "error", "Setting values must be strings" } };
				const auto text = value.get<std::string>();
				if (file == ConfigFile::Main) {
					const auto* setting = FindSetting(key);
					if (!setting || !ValidateSetting(*setting, text, error))
						return { { "error", setting ? error : "Unknown main setting" } };
				} else if (!(Equal(key, "Enabled") || EditableSection(file, key)) || Equal(key, "Interior") ||
						   (Equal(key, "Enabled") && text != "0" && text != "1")) {
					return { { "error", "Unknown or invalid location setting" } };
				}
				if (!document.Set("Settings", key, text, error))
					return { { "error", error } };
			}
			for (const auto& [section, body] : sections.items()) {
				if (!body.is_string() || !EditableSection(file, section))
					return { { "error", "Unknown section or non-string section body" } };
				if (!document.SetBody(section, body.get<std::string>(), error))
					return { { "error", error } };
			}
			const bool saved = Save(file, document, error);
			return { { "saved", saved }, { "error", error }, { "status", StatusJson() } };
		}

		void Handler(void*, const char* text, void* sink, DevBenchAPI::WriteFn write) noexcept
		{
			json output;
			try {
				const auto args = text && *text ? json::parse(text) : json::object();
				if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
					output = std::move(*mismatch);
				else
					output = CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), [args] { return Execute(args); });
			} catch (const std::exception& e) {
				output = { { "error", e.what() } };
			} catch (...) {
				output = { { "error", "Unknown Stabilizer request failure" } };
			}
			BuildProvenance::AttachProducer(output);
			try {
				const auto serialized = output.dump();
				write(sink, serialized.c_str());
			} catch (...) {
				write(sink, R"({"error":"Stabilizer response serialization failed"})");
			}
		}
	}

	void InstallDevBench()
	{
		auto* host = DevBenchAPI::GetDevBenchInterface001();
		if (!host)
			return;
		static constexpr const char* descriptor = R"({"description":"Inspect, edit and reload the installed VR FPS Stabilizer main or location INI. Skyrim VR only. read returns exact contents and typed setting descriptions. save requires expectedContents from read, rejects stale edits, validates main performance limits, preserves unedited INI bytes and queues the revision 1 reload on the game thread. settings patches [Settings] keys; sections replaces complete bodies of Level0-Level9, event/Conditional sections or location tiers. Advanced bodies contain Stabilizer command syntax, never section headers. status distinguishes interface availability, pending reload, revision and restartRequired. Completion means the author's void reload API returned, not visual verification. Startup/event commands still require their events. No ResetIniSettings call is made. Old plugins can save with restartRequired. expectedBuildId fails closed on a different CSX binary.","inputSchema":{"type":"object","additionalProperties":false,"required":["action"],"properties":{"action":{"type":"string","enum":["status","read","save","reload"]},"file":{"type":"string","enum":["main","locations"],"default":"main"},"expectedContents":{"type":"string","maxLength":4194304},"settings":{"type":"object","additionalProperties":{"type":"string"}},"sections":{"type":"object","additionalProperties":{"type":"string"}},"expectedBuildId":{"type":"string"}}}})";
		host->RegisterTool("communityshaders.stabilizer", descriptor, &Handler, nullptr);
	}
}
#else
namespace VRFpsStabilizer
{
	void InstallDevBench() {}
}
#endif
