#include "GameSetting.h"

#include <ClibUtil/detail/SimpleIni.h>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>

#include "Utils/FileSystem.h"
#include "Utils/Format.h"
#include "Utils/UI.h"

namespace Util
{
	static constexpr std::string_view CS_SETTINGS_PATH{ "Data/SKSE/Plugins/CommunityShaders/SkyrimOverwrite.ini"sv };

	void DumpSettingsOptions()
	{
		// List of INI setting collections to iterate over
		std::vector<RE::SettingCollectionList<RE::Setting>*> collections = {
			globals::game::iniSettingCollection,
			globals::game::iniPrefSettingCollection,
		};

		// Iterate over each collection and log the settings
		for (const auto& collection : collections) {
			const std::string collectionName = typeid(*collection).name();  // Get the collection name
			for (const auto set : collection->settings) {
				logger::info("Setting [{}] {}", collectionName, set->GetName());
			}
		}

		// Retrieve and log settings from the GameSettingCollection
		auto game = globals::game::gameSettingCollection;
		for (const auto& set : game->settings) {
			logger::info("Game Setting {}", set.second->GetName());
		}
	}

	void SetBooleanSettings(const std::map<std::string, GameSetting>& settingsMap, const std::string& featureName, bool a_value)
	{
		// Extract first letter from each word in featureName
		std::string logTag;
		bool capitalizeNext = true;
		for (char ch : featureName) {
			if (std::isalpha(ch) && capitalizeNext) {
				logTag += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
				capitalizeNext = false;
			}
			if (std::isspace(ch)) {
				capitalizeNext = true;
			}
		}

		// Initialize collections
		std::vector<std::pair<RE::INISettingCollection*, std::string>> iniCollections = {
			{ globals::game::iniSettingCollection, "INISettingCollection" },
			{ globals::game::iniPrefSettingCollection, "INIPrefSettingCollection" }
		};
		auto gameSettingCollection = globals::game::gameSettingCollection;

		// Handle INI settings
		for (const auto& [settingName, settingData] : settingsMap) {
			if (settingData.offset == 0) {  // INI-based settings
				bool processed = false;
				for (const auto& [collection, collectionName] : iniCollections) {
					if (auto setting = collection->GetSetting(settingName); setting) {
						if (setting->data.b != a_value) {
							logger::info("[{}] Changing {}:{} from {} to {} to support {}", logTag, collectionName, settingName, setting->data.b, a_value, featureName);
							setting->data.b = a_value;
						}
						processed = true;
						break;  // Exit once the setting is found and processed
					}
				}

				// Handle game settings if not processed by INI collections
				if (!processed) {
					if (auto setting = gameSettingCollection->GetSetting(settingName.data()); setting) {
						if (setting->data.b != a_value) {
							logger::info("[{}] Changing {} from {} to {} to support {}", logTag, settingName, setting->data.b, a_value, featureName);
							setting->data.b = a_value;
						}
					}
				}
			} else {
				// Handle settings with memory offsets
				auto address = REL::Offset{ settingData.offset }.address();
				bool* setting = reinterpret_cast<bool*>(address);
				if (*setting != a_value) {
					logger::info("[{}] Changing {} from {} to {} to support {}", logTag, settingName, *setting, a_value, featureName);
					*setting = a_value;
				}
			}
		}
	}

	void EnableBooleanSettings(const std::map<std::string, GameSetting>& settingsMap, const std::string& featureName)
	{
		SetBooleanSettings(settingsMap, featureName, true);
	}

	void DisableBooleanSettings(const std::map<std::string, GameSetting>& settingsMap, const std::string& featureName)
	{
		SetBooleanSettings(settingsMap, featureName, false);
	}

	void ResetGameSettingsToDefaults(std::map<std::string, GameSetting>& settingsMap)
	{
		std::vector<std::pair<RE::INISettingCollection*, std::string>> iniCollections = {
			{ globals::game::iniSettingCollection, "INISettingCollection" },
			{ globals::game::iniPrefSettingCollection, "INIPrefSettingCollection" }
		};
		auto gameSettingCollection = globals::game::gameSettingCollection;

		for (auto& [settingName, settingData] : settingsMap) {
			char inputTypeChar = settingName[0];

			if (settingData.offset == 0) {  // INI-based settings
				bool processed = false;
				for (const auto& [collection, collectionName] : iniCollections) {
					if (auto setting = collection->GetSetting(settingName); setting) {
						switch (inputTypeChar) {
						case 'b':
							{
								bool currentValue = setting->data.b;
								bool defaultValue = std::get<bool>(settingData.defaultValue);
								if (currentValue != defaultValue) {
									setting->data.b = defaultValue;
									logger::debug("{} Setting {}: changed from {} to default boolean value {}.", collectionName, settingName, currentValue, defaultValue);
								}
							}
							break;
						case 'f':
							{
								float currentValue = setting->data.f;
								float defaultValue = std::get<float>(settingData.defaultValue);
								if (currentValue != defaultValue) {
									setting->data.f = defaultValue;
									logger::debug("{} Setting {}: changed from {} to default float value {}.", collectionName, settingName, currentValue, defaultValue);
								}
							}
							break;
						case 'i':
						case 'u':
							{
								int32_t currentValue = setting->data.i;
								int32_t defaultValue = std::get<int32_t>(settingData.defaultValue);
								if (currentValue != defaultValue) {
									setting->data.i = defaultValue;
									logger::debug("{} Setting {}: changed from {} to default integer value {}.", collectionName, settingName, currentValue, defaultValue);
								}
							}
							break;
						default:
							logger::debug("Unknown type for {} setting {}.", collectionName, settingName);
							break;
						}
						processed = true;
						break;  // Exit once the setting is found and processed
					}
				}

				// Handle game settings if not processed by INI collections
				if (!processed) {
					if (auto setting = gameSettingCollection->GetSetting(settingName.data()); setting) {
						bool currentValue = setting->data.b;
						bool defaultValue = std::get<bool>(settingData.defaultValue);
						if (currentValue != defaultValue) {
							setting->data.b = defaultValue;
							logger::debug("GameSetting {}: changed from {} to default boolean value {}.", settingName, currentValue, defaultValue);
						}
					}
				}
			} else {
				// Handle settings with memory offsets
				auto address = REL::Offset{ settingData.offset }.address();
				switch (inputTypeChar) {
				case 'b':
					{
						bool* ptr = reinterpret_cast<bool*>(address);
						bool currentValue = *ptr;
						bool defaultValue = std::get<bool>(settingData.defaultValue);
						if (currentValue != defaultValue) {
							*ptr = defaultValue;
							logger::debug("Setting {}: changed from {} to default boolean value {}.", settingName, currentValue, defaultValue);
						}
					}
					break;
				case 'f':
					{
						float* ptr = reinterpret_cast<float*>(address);
						float currentValue = *ptr;
						float defaultValue = std::get<float>(settingData.defaultValue);
						if (currentValue != defaultValue) {
							*ptr = defaultValue;
							logger::debug("Setting {}: changed from {} to default float value {}.", settingName, currentValue, defaultValue);
						}
					}
					break;
				case 'i':
				case 'u':
					{
						int32_t* ptr = reinterpret_cast<int32_t*>(address);
						int32_t currentValue = *ptr;
						int32_t defaultValue = std::get<int32_t>(settingData.defaultValue);
						if (currentValue != defaultValue) {
							*ptr = defaultValue;
							logger::debug("Setting {}: changed from {} to default integer value {}.", settingName, currentValue, defaultValue);
						}
					}
					break;
				default:
					logger::debug("Unknown type for setting {}.", settingName);
					break;
				}
			}
		}
	}

	void RenderImGuiSettingsTree(const std::map<std::string, GameSetting>& settingsMap, const std::string& treeName)
	{
		if (ImGui::TreeNode(treeName.c_str())) {  // Create a tree node
			std::vector<std::pair<RE::INISettingCollection*, std::string>> iniCollections = {
				{ globals::game::iniSettingCollection, "INISettingCollection" },
				{ globals::game::iniPrefSettingCollection, "INIPrefSettingCollection" }
			};
			auto gameSettingCollection = globals::game::gameSettingCollection;

			for (const auto& [settingName, settingData] : settingsMap) {
				// Handle INI and INIPref settings
				if (settingData.offset == 0) {
					bool found = false;
					for (const auto& [collection, collectionName] : iniCollections) {
						if (auto setting = collection->GetSetting(settingName); setting) {
							RenderImGuiElement(settingName, settingData, setting, collectionName);
							found = true;
							break;  // Exit once the setting is found and processed
						}
					}
					// Handle game settings if not found in INI collections
					if (!found) {
						if (auto gameSetting = gameSettingCollection->GetSetting(settingName.data()); gameSetting) {
							RenderImGuiElement(settingName, settingData, gameSetting, "GameSetting");
							continue;
						}
					}
				} else {
					// Handle settings with offsets (raw memory)
					std::visit([&](auto&& defaultValue) {
						using ValueType = std::decay_t<decltype(defaultValue)>;
						if constexpr (std::is_same_v<ValueType, bool>) {
							bool* ptr = reinterpret_cast<bool*>(REL::Offset{ settingData.offset }.address());
							RenderImGuiElement(settingName, settingData, ptr);
						} else if constexpr (std::is_same_v<ValueType, float>) {
							float* ptr = reinterpret_cast<float*>(REL::Offset{ settingData.offset }.address());
							RenderImGuiElement(settingName, settingData, ptr);
						} else if constexpr (std::is_same_v<ValueType, int32_t>) {
							int32_t* ptr = reinterpret_cast<int32_t*>(REL::Offset{ settingData.offset }.address());
							RenderImGuiElement(settingName, settingData, ptr);
						} else if constexpr (std::is_same_v<ValueType, uint32_t>) {
							uint32_t* ptr = reinterpret_cast<uint32_t*>(REL::Offset{ settingData.offset }.address());
							RenderImGuiElement(settingName, settingData, ptr);
						} else {
							logger::warn("Unsupported type for setting {}", settingName);
						}
					},
						settingData.defaultValue);
				}
			}

			ImGui::TreePop();  // Close the tree node
		}
	}

	template <typename T>
	void RenderImGuiElement(const std::string& settingName, const GameSetting& settingData, T* valuePtr, const std::string& collectionName)
	{
		// Unique ID for each element
		ImGui::PushID(settingName.c_str());  // Use settingName as unique ID
		bool success = false;                // Flag to track if element creation is successful

		if constexpr (std::is_same_v<T, bool>) {
			ImGui::Checkbox(settingData.friendlyName.c_str(), valuePtr);
			success = true;  // Mark as successful
		} else if constexpr (std::is_same_v<T, float>) {
			try {
				auto minFloat = std::get<float>(settingData.minValue);
				auto maxFloat = std::get<float>(settingData.maxValue);
				ImGui::SliderFloat(settingData.friendlyName.c_str(), valuePtr, minFloat, maxFloat);
				success = true;  // Mark as successful
			} catch (const std::bad_variant_access&) {
				logger::warn("Type mismatch for {} {}: expected float for minValue or maxValue but received other type", collectionName, settingName);
			}
		} else if constexpr (std::is_same_v<T, int>) {
			try {
				auto minInt = std::get<int32_t>(settingData.minValue);
				auto maxInt = std::get<int32_t>(settingData.maxValue);
				ImGui::SliderInt(settingData.friendlyName.c_str(), reinterpret_cast<int*>(valuePtr), minInt, maxInt);
				success = true;  // Mark as successful
			} catch (const std::bad_variant_access&) {
				logger::warn("Type mismatch for {} {}: expected int for minValue or maxValue but received other type", collectionName, settingName);
			}
		} else if constexpr (std::is_same_v<T, unsigned int>) {
			try {
				auto minUInt = std::get<uint32_t>(settingData.minValue);  // Use uint32_t for unsigned
				auto maxUInt = std::get<uint32_t>(settingData.maxValue);
				ImGui::SliderScalar(settingData.friendlyName.c_str(), ImGuiDataType_U32, reinterpret_cast<unsigned int*>(valuePtr), &minUInt, &maxUInt);
				success = true;  // Mark as successful
			} catch (const std::bad_variant_access&) {
				logger::warn("Type mismatch for {} {}: expected unsigned int for minValue or maxValue but received other type", collectionName, settingName);
			}
		} else {
			logger::warn("Unsupported type for {} {}: {}", collectionName, settingName, typeid(T).name());
		}

		// Log if element creation failed
		if (!success) {
			logger::debug("Failed to create element for {} {} of type {}", collectionName, settingName, typeid(T).name());
		}

		// Tooltip handling
		if (auto _tt = HoverTooltipWrapper()) {
			std::string hover = "";
			if (settingData.offset != 0)
				hover = std::format("{}\n\nNOTE: CSX cannot save this game setting directly. Setting {} '{}' might be able to be saved manually in the ini. Use the Copy button to export to clipboard.", settingData.description, collectionName, settingName);
			else
				hover = settingData.description;
			ImGui::Text(hover.c_str());
		}
		if (settingData.offset != 0) {
			ImGui::SameLine();
			if (ImGui::Button("Copy")) {
				ImGui::SetClipboardText(settingName.c_str());
			}
			if (auto _tt = HoverTooltipWrapper()) {
				ImGui::Text(std::format("Copy {} '{}' to clipboard.", collectionName, settingName).c_str());
			}
		}
		ImGui::PopID();  // End unique ID scope
	}

	void RenderImGuiElement(const std::string& settingName, const GameSetting& settingData, RE::Setting* setting, const std::string& collectionName)
	{
		// Determine the type of the setting
		switch (setting->GetType()) {
		case RE::Setting::Type::kBool:
			RenderImGuiElement(settingName, settingData, &setting->data.b, collectionName);
			break;
		case RE::Setting::Type::kFloat:
			RenderImGuiElement(settingName, settingData, &setting->data.f, collectionName);
			break;
		case RE::Setting::Type::kInteger:
			RenderImGuiElement(settingName, settingData, &setting->data.i, collectionName);
			break;
		case RE::Setting::Type::kUnsignedInteger:
			RenderImGuiElement(settingName, settingData, &setting->data.u, collectionName);
			break;
		case RE::Setting::Type::kColorRGB:
		case RE::Setting::Type::kColorRGBA:
		case RE::Setting::Type::kString:
		case RE::Setting::Type::kUnknown:
		default:
			logger::warn("Unsupported type for {} setting '{}'", collectionName, settingName);
			break;
		}
	}

	namespace
	{
		bool ReadGameSettingsIni(CSimpleIniA& a_ini, bool& a_utf16)
		{
			a_ini.SetUnicode();
			a_ini.SetMultiKey();
			a_utf16 = false;
			std::ifstream input(std::filesystem::path{ CS_SETTINGS_PATH }, std::ios::binary | std::ios::ate);
			if (!input.is_open()) {
				std::error_code error;
				if (!std::filesystem::exists(CS_SETTINGS_PATH, error) && !error)
					return false;
				throw std::runtime_error(std::format("Could not open {} for reading", CS_SETTINGS_PATH));
			}
			const std::streamsize size = input.tellg();
			if (size < 0)
				throw std::runtime_error(std::format("Could not determine the size of {}", CS_SETTINGS_PATH));
			std::string contents(static_cast<size_t>(size), '\0');
			input.seekg(0);
			if (!input.read(contents.data(), size))
				throw std::runtime_error(std::format("Could not finish reading {}", CS_SETTINGS_PATH));
			a_utf16 = contents.starts_with("\xff\xfe");
			if (a_utf16 && contents.size() > 2) {
				if (contents.size() % sizeof(wchar_t) != 0)
					throw std::runtime_error(std::format("Incomplete UTF-16 text in {}", CS_SETTINGS_PATH));
				std::wstring wide((contents.size() - 2) / sizeof(wchar_t), L'\0');
				std::memcpy(wide.data(), contents.data() + 2, contents.size() - 2);
				const auto converted = SKSE::stl::utf16_to_utf8(wide);
				if (!converted || SKSE::stl::utf8_to_utf16(*converted) != wide)
					throw std::runtime_error(std::format("Invalid UTF-16 text in {}", CS_SETTINGS_PATH));
				contents = *converted;
			} else if (a_utf16) {
				contents.clear();
			}
			// Embedded NULs would make SimpleIni silently discard the rest of the file.
			if (contents.find('\0') != std::string::npos)
				throw std::runtime_error(std::format("Unexpected NUL in {}", CS_SETTINGS_PATH));
			const auto result = a_ini.LoadData(contents);
			if (result >= 0)
				return true;
			throw std::runtime_error(std::format("Could not read {} (INI error {})", CS_SETTINGS_PATH, result));
		}

		std::pair<std::string, std::string> SplitGameSettingName(const std::string& a_name)
		{
			const auto separator = a_name.find(':');
			if (separator == std::string::npos || separator == 0 || separator + 1 == a_name.size())
				throw std::runtime_error(std::format("Invalid INI setting name '{}'", a_name));
			return { a_name.substr(0, separator), a_name.substr(separator + 1) };
		}

		template <class F>
		void VisitGameSettingValue(const std::string& a_name, F&& a_visit)
		{
			RE::Setting* setting = nullptr;
			for (auto* collection : { globals::game::iniSettingCollection,
					 static_cast<RE::INISettingCollection*>(globals::game::iniPrefSettingCollection) }) {
				if (collection && (setting = collection->GetSetting(a_name)))
					break;
			}
			if (!setting && globals::game::gameSettingCollection)
				setting = globals::game::gameSettingCollection->GetSetting(a_name.c_str());
			if (!setting)
				throw std::runtime_error(std::format("Game setting '{}' not found", a_name));

			switch (setting->GetType()) {
			case RE::Setting::Type::kBool:
				a_visit(setting->data.b);
				break;
			case RE::Setting::Type::kInteger:
				a_visit(setting->data.i);
				break;
			case RE::Setting::Type::kUnsignedInteger:
				a_visit(setting->data.u);
				break;
			case RE::Setting::Type::kFloat:
				a_visit(setting->data.f);
				break;
			default:
				throw std::runtime_error(std::format("Unsupported INI setting type for '{}'", a_name));
			}
		}

		template <class T>
		bool ParseGameSettingValue(std::string_view a_text, T& a_value)
		{
			if (a_text.size() >= 2 && (a_text.front() == '"' || a_text.front() == '\'') && a_text.back() == a_text.front())
				a_text = a_text.substr(1, a_text.size() - 2);
			if constexpr (std::is_same_v<T, bool>) {
				if (a_text == "1" || IEquals(a_text, "true")) {
					a_value = true;
					return true;
				}
				if (a_text == "0" || IEquals(a_text, "false")) {
					a_value = false;
					return true;
				}
				return false;
			} else {
				T parsed{};
				const auto [end, error] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), parsed);
				if (error != std::errc{} || end != a_text.data() + a_text.size())
					return false;
				if constexpr (std::is_floating_point_v<T>) {
					if (!std::isfinite(parsed))
						return false;
				}
				a_value = parsed;
				return true;
			}
		}
	}

	void SaveGameSettings(const std::map<std::string, GameSetting>& settingsMap)
	{
		CSimpleIniA ini;
		bool utf16 = false;
		ReadGameSettingsIni(ini, utf16);
		bool changed = false;
		for (const auto& [name, metadata] : settingsMap) {
			if (metadata.offset != 0)
				continue;
			const auto [key, section] = SplitGameSettingName(name);
			VisitGameSettingValue(name, [&](auto& value) {
				using T = std::remove_cvref_t<decltype(value)>;
				if constexpr (std::is_floating_point_v<T>) {
					if (!std::isfinite(value))
						throw std::runtime_error(std::format("Non-finite value for INI setting '{}'", name));
				}
				std::string text;
				if constexpr (std::is_same_v<T, bool>)
					text = value ? "1" : "0";
				else
					text = std::format("{}", value);
				if (ini.SetValue(section.c_str(), key.c_str(), text.c_str(), nullptr, true) < 0)
					throw std::runtime_error(std::format("Could not serialize INI setting '{}'", name));
			});
			changed = true;
		}
		if (!changed)
			return;

		std::string contents;
		if (ini.Save(contents) < 0)
			throw std::runtime_error(std::format("Could not serialize {}", CS_SETTINGS_PATH));
		if (utf16) {
			const auto wide = SKSE::stl::utf8_to_utf16(contents);
			if (!wide)
				throw std::runtime_error(std::format("Could not encode {} as UTF-16", CS_SETTINGS_PATH));
			contents.assign("\xff\xfe", 2);
			contents.append(reinterpret_cast<const char*>(wide->data()), wide->size() * sizeof(wchar_t));
		}
		std::string error;
		if (!FileHelpers::WriteTextFileAtomic(CS_SETTINGS_PATH, contents, error))
			throw std::runtime_error(std::format("Could not save {}: {}", CS_SETTINGS_PATH, error));
		logger::debug("Saved game settings to {}", CS_SETTINGS_PATH);
	}

	void LoadGameSettings(const std::map<std::string, GameSetting>& settingsMap)
	{
		CSimpleIniA ini;
		bool utf16 = false;
		try {
			if (!ReadGameSettingsIni(ini, utf16))
				return;
		} catch (const std::exception& e) {
			logger::warn("{}; retaining current game settings", e.what());
			return;
		}

		for (const auto& [name, metadata] : settingsMap) {
			if (metadata.offset != 0)
				continue;
			try {
				const auto [key, section] = SplitGameSettingName(name);
				const auto* text = ini.GetValue(section.c_str(), key.c_str());
				if (!text)
					continue;
				VisitGameSettingValue(name, [&](auto& value) {
					if (!ParseGameSettingValue(text, value))
						throw std::runtime_error(std::format("Invalid value for INI setting '{}'", name));
				});
				logger::debug("Loaded game setting {} from {}", name, CS_SETTINGS_PATH);
			} catch (const std::exception& e) {
				logger::warn("{} in {}; retaining its current value", e.what(), CS_SETTINGS_PATH);
			}
		}
	}
}  // namespace Util
