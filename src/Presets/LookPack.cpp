#include "LookPack.h"
#include "LookScenes.h"

#include "BaselineCompatibility.h"
#include "SettingsSerialization.h"
#include "Utils/PathValidation.h"
#include "Utils/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace Presets
{
	namespace
	{
		constexpr std::uintmax_t kMaxDocumentBytes = 1024 * 1024;
		constexpr size_t kMaxPackFiles = 256;
		constexpr size_t kMaxPackEntries = 1024;
		constexpr std::uintmax_t kMaxPackDocumentBytes = 16 * 1024 * 1024;
		constexpr std::uintmax_t kMaxPackAssetBytes = 256 * 1024 * 1024;

		void RequireFeature(LookPack& pack, const std::string& feature)
		{
			pack.requiresPostProcessing |= feature == "PostProcessing";
			const auto message = feature == "PostProcessing" ?
			                         "Post Processing is not available in this CSX build." :
			                         "Required feature is unavailable: " + feature;
			if (std::ranges::find(pack.unavailable, message) == pack.unavailable.end())
				pack.unavailable.push_back(message);
		}

		std::filesystem::path Utf8Path(const std::string& value)
		{
			return std::filesystem::path(std::u8string(value.begin(), value.end()));
		}

		std::string DocumentKey(const std::filesystem::path& path)
		{
			const auto value = path.generic_u8string();
			return { value.begin(), value.end() };
		}

		std::string DocumentFeature(const Json& document, const std::filesystem::path& path)
		{
			if (document.contains("_feature")) {
				const auto feature = document.at("_feature").get<std::string>();
				if (document.contains("feature") && document.at("feature") != feature)
					throw std::runtime_error("Conflicting feature identifiers: " + DocumentKey(path));
				return feature;
			}
			const auto stem = Util::PathToUtf8(path.stem());
			return document.value("feature", stem.substr(stem.rfind('_') == std::string::npos ? 0 : stem.rfind('_') + 1));
		}

		Json ReadObject(const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				throw std::runtime_error("Cannot read preset document: " + Util::PathToUtf8(path.filename()));
			std::string bytes(kMaxDocumentBytes + 1, '\0');
			input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			if (input.bad())
				throw std::runtime_error("Failed to read preset document: " + Util::PathToUtf8(path.filename()));
			bytes.resize(static_cast<size_t>(input.gcount()));
			if (bytes.size() > kMaxDocumentBytes)
				throw std::runtime_error("Document exceeds 1 MiB: " + Util::PathToUtf8(path.filename()));
			std::map<int, std::set<std::string>> objectKeys;
			const auto document = Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json& value) {
				if (depth > 64)
					throw std::runtime_error("Preset document is nested too deeply");
				if (event == Json::parse_event_t::object_start)
					objectKeys[depth].clear();
				if (event == Json::parse_event_t::key && !objectKeys[depth - 1].insert(value.get<std::string>()).second)
					throw std::runtime_error("Duplicate preset key: " + value.get<std::string>());
				if (event == Json::parse_event_t::object_end)
					objectKeys.erase(depth);
				return true;
			});
			if (!document.is_object())
				throw std::runtime_error("Expected an object: " + Util::PathToUtf8(path.filename()));
			return document;
		}

		void ReadLayer(const std::filesystem::path& folder, const Json& schemas, Json& layer, LookPack& pack)
		{
			if (!std::filesystem::exists(folder))
				return;
			if (!Util::IsPathWithinDirectory(folder.parent_path(), folder))
				throw std::runtime_error("Preset layer resolves outside its pack");
			std::vector<std::filesystem::path> files;
			for (const auto& file : std::filesystem::directory_iterator(folder)) {
				if (file.is_regular_file() && Util::IEndsWithAsciiInsensitive(Util::PathToUtf8(file.path().extension()), ".json"))
					files.push_back(file.path());
				if (files.size() > kMaxPackFiles)
					throw std::runtime_error("Too many preset files");
			}
			std::sort(files.begin(), files.end());
			for (const auto& file : files) {
				if (!Util::IsPathWithinDirectory(folder, file))
					throw std::runtime_error("Preset file resolves outside its pack");
				const auto original = ReadObject(file);
				const auto feature = DocumentFeature(original, file);
				if (feature == "Advanced") {
					pack.errors.push_back("Core appearance belongs in the manifest's csx.core section.");
					continue;
				}
				auto values = original;
				pack.documents[DocumentKey(file.lexically_relative(pack.sourceRoot))] = values;
				const auto metadata = values.value("_metadata", Json::object());
				if (!metadata.is_object())
					throw std::runtime_error("Invalid preset metadata");
				if (metadata.contains("enabled") && !metadata.at("enabled").is_boolean())
					throw std::runtime_error("Invalid preset enabled flag");
				if (!metadata.value("enabled", true))
					continue;
				for (const auto* unsupported : { "entryTransitions", "entryDefaults", "entryDeletions" })
					if (metadata.contains(unsupported) && !metadata[unsupported].empty())
						pack.unavailable.push_back(std::string("This profile requires unsupported scene metadata: ") + unsupported);
				if (metadata.contains("timeOfDayEnabled") && !metadata["timeOfDayEnabled"].is_boolean())
					throw std::runtime_error("Invalid time-of-day enabled flag");
				if (metadata.contains("timeOfDayEnabled")) {
					auto context = folder.lexically_relative(pack.sourceRoot);
					if (context.begin()->string() == "TimeOfDay" || std::distance(context.begin(), context.end()) > 2)
						context = context.parent_path();
					const auto key = DocumentKey(context);
					const bool enabled = metadata["timeOfDayEnabled"];
					if (pack.periodicContexts.contains(key) && pack.periodicContexts[key] != enabled)
						throw std::runtime_error("Conflicting time-of-day mode in " + key);
					pack.periodicContexts[key] = enabled;
				}
				if (metadata.contains("targetType")) {
					const auto type = metadata.at("targetType").get<std::string>();
					if (type != "Worldspace" && type != "Location" && type != "Cell")
						pack.unavailable.push_back("Unsupported location target: " + type);
				}
				values.erase("_metadata");
				values.erase("_feature");
				values.erase("feature");
				if (!IsLookFeatureAvailable(feature, schemas)) {
					RequireFeature(pack, feature);
					continue;
				}
				if (!HasProfileSettings(feature)) {
					pack.notices.push_back(feature + ": local settings are kept from your configuration.");
					continue;
				}
				const auto relativeFolder = folder.lexically_relative(pack.sourceRoot);
				const auto context = DocumentKey(relativeFolder);
				if (context == "Baseline")
					BaselineCompatibility::EraseUnsupportedSettings(feature, schemas.at(feature), values);
				std::string error;
				if (!ValidatePatch(values, schemas.at(feature), error, "/" + feature)) {
					pack.errors.push_back(feature + ": " + error);
					continue;
				}
				const auto normalize = [&](auto&& self, Json& value, const Json& prototype) -> void {
					if (prototype.is_number_float() && value.is_number_integer())
						value = value.get<double>();
					else if (value.is_object())
						for (auto& [key, child] : value.items()) self(self, child, prototype.at(key));
					else if (value.is_array())
						for (size_t i = 0; i < value.size(); ++i) self(self, value[i], prototype[std::min(i, prototype.size() - 1)]);
				};
				normalize(normalize, values, schemas.at(feature));

				auto appearance = SelectDomain(feature, values, SettingsDomain::Appearance);
				const bool scene = context != "Baseline";
				const bool floatsOnly = context.starts_with("TimeOfDay/") || context.starts_with("Weather/") ||
				                        (context.starts_with("Locations/") && std::distance(relativeFolder.begin(), relativeFolder.end()) == 3);
				if (scene) {
					const auto fields = appearance.flatten();
					for (const auto& [path, value] : fields.items()) {
						if (path.empty() || value.is_null())
							continue;
						const bool rebuild = feature == "VolumetricLighting" || feature == "TerrainVariation" ||
						                     (feature == "ScreenSpaceGI" && (path == "/EnableGI" || path == "/EnableExperimentalSpecularGI")) ||
						                     (feature == "ImageBasedLighting" && (path == "/DisableInWorldMap" || path == "/DisableInLoadingScreen")) ||
						                     (feature == "AdaptiveBrightness" && path.starts_with("/locationOverrides"));
						if (rebuild || (floatsOnly && !value.is_number_float()))
							pack.unavailable.push_back("Setting is not supported in this scene layer: " + feature + path);
					}
				}
				if (appearance != values)
					pack.notices.push_back(feature + ": performance and local settings are kept from your configuration.");
				if (!appearance.empty()) {
					if (!layer.contains(feature))
						layer[feature] = Json::object();
					const auto addresses = appearance.flatten();
					for (const auto& [path, value] : addresses.items()) {
						if (!path.empty() && !value.is_null() && layer[feature].contains(Json::json_pointer(path)))
							throw std::runtime_error("Duplicate scene setting: " + feature + path);
					}
					Overlay(layer[feature], appearance);
				}
			}
		}
	}

	Json ReadProfileDocument(const std::filesystem::path& path) { return ReadObject(path); }

	bool IsLookFeatureAvailable(std::string_view feature, const Json& schemas)
	{
		// A PP serializer alone must not enable imports before its field ownership is reviewed.
		return schemas.contains(feature) && (feature != "PostProcessing" || HasProfileSettings(feature));
	}

	std::string_view LookPack::CompatibilityStatus() const
	{
		return !errors.empty() ? "invalid" : !unavailable.empty() ? "unavailable" :
		                                                            "compatible";
	}

	std::string LookPack::CompatibilitySummary() const
	{
		if (!errors.empty())
			return "Invalid profile.";
		if (requiresPostProcessing)
			return "Requires Post Processing.";
		return unavailable.empty() ? "Select this appearance." : "Unavailable in this build.";
	}

	std::string LookPack::CompatibilityDetails() const
	{
		if (IsCompatible())
			return {};
		auto result = CompatibilitySummary();
		for (const auto* messages : { &errors, &unavailable })
			for (const auto& message : *messages) result += "\n" + message;
		return result;
	}

	bool IsPackId(std::string_view id)
	{
		if (id.empty() || id.size() > 100 || (id.front() == '.' || id.front() == '_') || id.back() == '.' || id.back() == ' ')
			return false;
		for (const unsigned char c : id)
			if (c < 32 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos)
				return false;
		std::string name(id.substr(0, id.find('.')));
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		if (name == "CON" || name == "PRN" || name == "AUX" || name == "NUL")
			return false;
		if (name.size() == 4 && (name.starts_with("COM") || name.starts_with("LPT")) && name[3] >= '0' && name[3] <= '9')
			return false;
		return true;
	}

	static LookPack ReadPackFolder(const std::filesystem::path& folder, const std::string& id, const Json& schemas)
	{
		LookPack pack;
		pack.id = id;
		try {
			pack.sourceRoot = folder;
			if (!std::filesystem::is_directory(folder))
				throw std::runtime_error("Preset folder is missing");
			std::vector<std::filesystem::path> entries;
			std::uintmax_t documentBytes = 0;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
				if (entries.size() >= kMaxPackEntries || !Util::IsPathWithinDirectory(folder, entry.path()))
					throw std::runtime_error("Profile contains too many files or a path outside its folder");
				if (entry.is_regular_file() && Util::IEndsWithAsciiInsensitive(Util::PathToUtf8(entry.path().extension()), ".json")) {
					const auto bytes = entry.file_size();
					if (bytes > kMaxDocumentBytes || bytes > kMaxPackDocumentBytes - documentBytes)
						throw std::runtime_error("Profile JSON exceeds the 16 MiB pack or 1 MiB document limit");
					documentBytes += bytes;
				}
				entries.push_back(entry.path());
			}
			std::sort(entries.begin(), entries.end());
			auto manifest = folder / Utf8Path(id + ".json");
			if (!std::filesystem::exists(manifest))
				manifest = folder / "preset.json";
			if (std::filesystem::exists(manifest)) {
				if (!Util::IsPathWithinDirectory(folder, manifest))
					throw std::runtime_error("Manifest resolves outside its pack");
				pack.manifest = ReadObject(manifest);
			}
			for (const auto* key : { "name", "version", "author", "description", "csVersion", "type" })
				if (pack.manifest.contains(key) && !pack.manifest[key].is_string())
					throw std::runtime_error(std::string("Invalid manifest field: ") + key);
			if (const auto found = pack.manifest.find("requiredFeatures"); found != pack.manifest.end()) {
				if (!found->is_array())
					throw std::runtime_error("requiredFeatures must be an array");
				for (const auto& feature : *found)
					if (!feature.is_string())
						throw std::runtime_error("requiredFeatures must contain names");
					else if (!IsLookFeatureAvailable(feature.get<std::string>(), schemas))
						RequireFeature(pack, feature.get<std::string>());
			}
			if (Util::ToLowerAscii(pack.manifest.value("type", "")) == "e11" || pack.manifest.contains("effects11") || std::filesystem::exists(folder / "enbseries.ini"))
				pack.unavailable.push_back("Effects11 profiles are not supported by CSX.");
			for (const char* unsupported : { "Forms", "effects11" }) {
				const auto path = folder / unsupported;
				if (std::filesystem::exists(path) && !std::filesystem::is_empty(path))
					pack.unavailable.push_back(std::string("This build cannot apply the pack's ") + unsupported + " layer.");
			}
			for (const char* unsupported : { "sceneControl", "disableAtBoot" })
				if (pack.manifest.contains(unsupported) && !pack.manifest[unsupported].empty())
					pack.unavailable.push_back(std::string("This Look requires unsupported manifest control: ") + unsupported);
			if (pack.manifest.contains("csVersion"))
				pack.notices.push_back("Authored for Community Shaders " + pack.manifest["csVersion"].get<std::string>() + "; compatibility is checked by supported settings, not the CSX version number.");
			if (const auto plugins = pack.manifest.find("requiredPlugins"); plugins != pack.manifest.end()) {
				if (!plugins->is_array())
					throw std::runtime_error("requiredPlugins must be an array");
				for (const auto& plugin : *plugins) {
					if (!plugin.is_string())
						throw std::runtime_error("requiredPlugins must contain names");
					pack.notices.push_back("Required plugin: " + plugin.get<std::string>());
				}
			}
			ReadLayer(folder / "Baseline", schemas, pack.baseline, pack);
			ReadLayer(folder / "InteriorOnly", schemas, pack.interior, pack);
			std::uintmax_t assetBytes = 0;
			std::set<std::string> sceneContexts;
			for (const auto& path : entries) {
				const std::filesystem::directory_entry entry(path);
				const auto relative = entry.path().lexically_relative(folder);
				const auto first = Util::PathToUtf8(*relative.begin());
				if (entry.is_directory() && (first == "TimeOfDay" || first == "Weather" || first == "Locations")) {
					std::vector<std::string> components;
					for (const auto& item : relative) components.push_back(Util::PathToUtf8(item));
					const auto count = components.size();
					if (count == 1)
						continue;
					const bool periodic = first == "TimeOfDay" || count == 3;
					if (count > 3 || (first == "TimeOfDay" && count != 2) ||
						(periodic && std::ranges::find(ScenePeriods, components.back()) == ScenePeriods.end()))
						throw std::runtime_error("Unrecognized scene folder: " + DocumentKey(relative));
					if (first != "TimeOfDay") {
						const auto context = first + "/" + SceneFormKey(components[1]) + (count == 3 ? "/" + components[2] : "");
						if (!sceneContexts.insert(context).second)
							throw std::runtime_error("Duplicate scene context: " + context);
					}
					ReadLayer(entry.path(), schemas, pack.scenes[DocumentKey(relative)], pack);
				}
				if (entry.is_regular_file() && Util::IEndsWithAsciiInsensitive(Util::PathToUtf8(entry.path().extension()), ".json") && entry.path() != manifest && !pack.documents.contains(DocumentKey(relative))) {
					pack.documents[DocumentKey(relative)] = ReadObject(entry.path());
					pack.errors.push_back("Unrecognized profile document: " + DocumentKey(relative));
				}
				if (entry.is_regular_file() && !Util::IEndsWithAsciiInsensitive(Util::PathToUtf8(entry.path().extension()), ".json")) {
					const auto extension = Util::ToLowerAscii(Util::PathToUtf8(entry.path().extension()));
					if (extension != ".dds" && extension != ".png" && extension != ".bmp" && extension != ".jpg" && extension != ".jpeg" && extension != ".cube" && extension != ".txt" && extension != ".md")
						throw std::runtime_error("Unsupported profile asset: " + DocumentKey(relative));
					const auto bytes = entry.file_size();
					if (bytes > kMaxPackAssetBytes - assetBytes)
						throw std::runtime_error("Profile assets exceed 256 MiB");
					assetBytes += bytes;
					pack.assets.push_back(relative);
				}
			}
			for (const auto& [name, maximum] : std::map<std::string, double>{ { "periodTransitionHours", 2 }, { "locationTransitionSeconds", 300 }, { "weatherTransitionScale", 10 }, { "transitionEase", 1 } }) {
				if (const auto found = pack.manifest.find(name); found != pack.manifest.end() &&
																 (!found->is_number() || !std::isfinite(found->get<double>()) || found->get<double>() < (name == "weatherTransitionScale" ? .1 : 0) || found->get<double>() > maximum))
					throw std::runtime_error("Invalid transition setting: " + name);
			}

			if (pack.manifest.value("weatherTransitionScale", 1.f) != 1.f)
				pack.unavailable.push_back("This profile changes the native weather-transition speed, which this build cannot apply.");

			if (const auto extension = pack.manifest.find("csx"); extension != pack.manifest.end()) {
				if (!extension->is_object() || extension->value("lookSchema", 1) != 1)
					throw std::runtime_error("Unsupported CSX Look schema");
				if (extension->contains("core")) {
					std::string error;
					if (!ValidatePatch(extension->at("core"), schemas.at("Advanced"), error))
						throw std::runtime_error(error);
					pack.baseline["Advanced"] = SelectDomain("Advanced", extension->at("core"), SettingsDomain::Appearance);
				}
			}
			std::erase_if(pack.scenes, [](const auto& item) { return item.second.empty(); });
			if (pack.IsCompatible() && pack.baseline.empty() && pack.interior.empty() && pack.scenes.empty())
				pack.errors.push_back("This folder has no supported Visuals settings.");
		} catch (const std::exception& error) {
			pack.errors.push_back(error.what());
		}
		return pack;
	}

	LookPack ReadLookPack(const std::filesystem::path& root, const std::string& id, const Json& schemas)
	{
		LookPack pack;
		pack.id = id;
		try {
			if (!IsPackId(id) || !Util::IsPathWithinDirectory(root, root / Utf8Path(id)))
				throw std::runtime_error("Invalid preset folder");
			return ReadPackFolder(root / Utf8Path(id), id, schemas);
		} catch (const std::exception& error) {
			pack.errors.push_back(error.what());
		}
		return pack;
	}

	bool WriteLookPack(const std::filesystem::path& root, const LookPack& pack, const Json& schemas, std::string& error)
	{
		std::filesystem::path staging;
		try {
			if (!pack.IsCompatible())
				throw std::runtime_error(pack.CompatibilityDetails());
			if (!IsPackId(pack.id))
				throw std::runtime_error("Use a valid folder name for the Look");
			if (!root.is_absolute())
				throw std::runtime_error("Choose an absolute export folder");
			std::filesystem::create_directories(root);
			const auto destination = root / Utf8Path(pack.id);
			if (!Util::IsPathWithinDirectory(root, destination))
				throw std::runtime_error("Profile destination could not be verified inside the selected folder.\nDestination: " + Util::PathToUtf8(destination) + "\nFolder: " + Util::PathToUtf8(root));
			if (std::filesystem::exists(destination))
				throw std::runtime_error("That Look already exists; choose a new name");
			staging = root / Utf8Path("." + pack.id + ".exporting");
			if (!std::filesystem::create_directory(staging))
				throw std::runtime_error("An unfinished export exists; choose a different name");
			auto manifest = pack.manifest;
			manifest["name"] = pack.id;
			manifest["type"] = pack.scenes.empty() && pack.interior.empty() ? "Baseline" : "CS";
			manifest["csx"]["lookSchema"] = 1;
			std::set<std::string> required;
			for (const auto& feature : manifest.value("requiredFeatures", Json::array())) required.insert(feature.get<std::string>());
			manifest["requiredFeatures"] = Json::array();
			const auto writeLayer = [&](const std::string& name, const Json& layer) {
				const auto layerPath = staging / Utf8Path(name);
				if (!Util::IsPathWithinDirectory(staging, layerPath))
					throw std::runtime_error("Invalid profile layer path");
				for (const auto& [feature, values] : layer.items()) {
					if (feature == "Advanced") {
						manifest["csx"]["core"] = values;
						continue;
					}
					if (!IsPackId(feature))
						throw std::runtime_error("Invalid feature name");
					std::filesystem::create_directories(layerPath);
					Json output = values;
					for (const auto& [relative, original] : pack.documents) {
						const auto path = Utf8Path(relative);
						const auto originalFeature = DocumentFeature(original, path);
						if (DocumentKey(path.parent_path()) != name || originalFeature != feature)
							continue;
						if (original.contains("_metadata") && original["_metadata"].value("enabled", true))
							Overlay(output["_metadata"], original["_metadata"]);
					}
					if (name != "Baseline")
						output["_feature"] = feature;
					if (!SettingsSerialization::WriteFileAtomic(layerPath / Utf8Path(feature + ".json"), output, error))
						throw std::runtime_error(error);
					required.insert(feature);
				}
			};
			writeLayer("Baseline", pack.baseline);
			writeLayer("InteriorOnly", pack.interior);
			for (const auto& [context, layer] : pack.scenes) writeLayer(context, layer);
			std::uintmax_t copiedAssetBytes = 0;
			for (const auto& relative : pack.assets) {
				if (!Util::IsPathWithinDirectory(pack.sourceRoot, pack.sourceRoot / relative) || !Util::IsPathWithinDirectory(staging, staging / relative))
					throw std::runtime_error("Asset resolves outside the profile");
				const auto bytes = std::filesystem::file_size(pack.sourceRoot / relative);
				if (bytes > kMaxPackAssetBytes - copiedAssetBytes)
					throw std::runtime_error("Profile assets exceed 256 MiB");
				copiedAssetBytes += bytes;
				std::filesystem::create_directories((staging / relative).parent_path());
				std::filesystem::copy_file(pack.sourceRoot / relative, staging / relative);
			}

			for (const auto& [relative, original] : pack.documents) {
				auto path = Utf8Path(relative);
				const auto parent = DocumentKey(path.parent_path());
				const auto feature = DocumentFeature(original, path);
				const bool emitted = (parent == "Baseline" && pack.baseline.contains(feature)) ||
				                     (parent == "InteriorOnly" && pack.interior.contains(feature)) ||
				                     (pack.scenes.contains(parent) && pack.scenes.at(parent).contains(feature));
				if (emitted && original.value("_metadata", Json::object()).value("enabled", true))
					continue;
				if (!Util::IsPathWithinDirectory(staging, staging / path))
					throw std::runtime_error("Invalid profile document path");
				auto output = original;
				if (std::filesystem::exists(staging / path)) {
					if (original.value("_metadata", Json::object()).value("enabled", true))
						throw std::runtime_error("Profile documents have conflicting names");
					output["_feature"] = feature;
					const auto stem = Util::PathToUtf8(path.stem());
					unsigned suffix = 0;
					do {
						path = path.parent_path() / Utf8Path("disabled-" + std::to_string(++suffix) + "_" + stem + ".json");
					} while (std::filesystem::exists(staging / path));
				}
				std::filesystem::create_directories((staging / path).parent_path());
				if (!SettingsSerialization::WriteFileAtomic(staging / path, output, error))
					throw std::runtime_error(error);
			}

			for (const auto& feature : required) manifest["requiredFeatures"].push_back(feature);
			if (!SettingsSerialization::WriteFileAtomic(staging / Utf8Path(pack.id + ".json"), manifest, error))
				throw std::runtime_error(error);
			const auto verified = ReadPackFolder(staging, pack.id, schemas);
			if (!verified.IsCompatible())
				throw std::runtime_error("Export is not importable: " + verified.CompatibilityDetails());
			std::filesystem::rename(staging, destination);
			return true;
		} catch (const std::exception& failure) {
			error = failure.what();
			// Preserve incomplete output for recovery; discovery ignores dot-prefixed folders.
			std::error_code ec;
			if (!staging.empty() && std::filesystem::exists(staging, ec))
				error += " (incomplete export preserved in " + Util::PathToUtf8(staging.filename()) + ")";
			return false;
		}
	}
}
