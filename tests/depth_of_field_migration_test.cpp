
namespace
{
	void Check(bool a_value, const char* a_message)
	{
		if (!a_value) {
			std::cerr << a_message << '\n';
			std::exit(1);
		}
	}

	json Read(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path);
		json value;
		input >> value;
		return value;
	}

	void Write(const std::filesystem::path& a_path, const json& a_value)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream output(a_path);
		output << a_value.dump();
		Check(output.good(), "Fixture write failed");
	}

	json Legacy()
	{
		json saved = AdaptiveBalanceDepthOfField::Settings{};
		saved["enabled"] = false;
		saved["fixUnderwaterFogDofBlur"] = true;
		saved["sceneDof"]["locked"] = true;
		saved["sceneDof"]["values"]["autoFocus"] = true;
		saved["sceneDof"]["values"]["autoFocusSettings"]["farBlur"] = 0.8f;
		saved["sceneDof"]["values"]["strength"] = 0.4f;
		saved["sceneDof"]["baseline"]["distance"] = 144.0f;
		saved["underwaterDof"]["values"]["strength"] = 0.2f;
		saved["underwaterDof"]["baseline"]["blurRadius"] = 7;
		return saved;
	}
}

int main(int argc, char** argv)
{
	Check(argc == 3, "Expected test name and scratch directory");
	const std::string test = argv[1];
	Fixture::root = argv[2];
	const auto providers = Util::PathHelpers::GetOverridesPath();
	const auto users = Util::PathHelpers::GetUserOverridesPath();
	std::filesystem::create_directories(users);
	auto& manager = *SettingsOverrideManager::GetSingleton();
	const std::string adaptive = "AdaptiveBrightness";
	if (test == "saved-values") {
		const auto legacy = Legacy();
		json layer = { { "CS Utility", legacy } };
		Check(SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer), "DOF-only settings were not migrated");
		Check(!layer.contains("CS Utility"), "Recognized legacy settings retained duplicate ownership");
		Check(layer.at("Adaptive Balance").at("depthOfField") == legacy, "Saved DOF values or independent boolean flags changed");
		Check(!layer.at("Adaptive Balance").contains("globalLightingEnabled"), "DOF-only migration changed the lighting gate");
		Check(!SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer), "Canonical migration is not idempotent");
		layer = { { "CS Utility", legacy } };
		layer["CS Utility"]["skyBrightness"] = 2.0;
		layer["CS Utility"]["bloomEnhancement"]["Enabled"] = 1;
		Check(SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer), "Renderer and DOF migration failed");
		Check(layer["Adaptive Balance"]["globalProfile"]["advanced"] == false, "Legacy renderer enable value changed");
		Check(layer["Adaptive Balance"]["globalProfile"]["skyBrightnessMult"] == 2.0, "Legacy lighting migration regressed");
		Check(layer["CS Utility"]["bloomEnhancement"]["Enabled"] == 1, "Legacy Bloom migration regressed");
		Check(layer["Adaptive Balance"]["depthOfField"] == legacy, "Mixed renderer migration lost DOF");
	} else if (test == "precedence") {
		json layer = { { "CS Utility", Legacy() } };
		layer["Adaptive Balance"]["depthOfField"]["enabled"] = true;
		layer["Adaptive Balance"]["depthOfField"]["sceneDof"]["values"]["strength"] = 0.9f;
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
		Check(layer["Adaptive Balance"]["depthOfField"]["enabled"] == true, "Explicit destination enabled did not win");
		Check(layer["Adaptive Balance"]["depthOfField"]["sceneDof"]["values"]["strength"] == 0.9f, "Explicit nested destination did not win");
		Check(layer["Adaptive Balance"]["depthOfField"]["sceneDof"]["baseline"]["distance"] == 144.0f, "Missing destination baseline was not migrated");
		json higher = { { "OS Utility", { { "enabled", false }, { "sceneDof", { { "values", { { "strength", 0.7f } } } } } } } };
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(higher);
		layer.merge_patch(higher);
		Check(layer["Adaptive Balance"]["depthOfField"]["enabled"] == false, "Higher priority legacy flag did not override lower canonical source");
		Check(layer["Adaptive Balance"]["depthOfField"]["sceneDof"]["values"]["strength"] == 0.7f, "Higher priority legacy nested value did not win");
		layer = { { "CS Utility", { { "enabled", true }, { "skyBrightness", 2.0 } } } };
		layer["Adaptive Balance"]["lighting"]["skyBrightness"] = 3.0;
		layer["Adaptive Balance"]["globalLightingEnabled"] = false;
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
		Check(layer["Adaptive Balance"]["globalProfile"]["skyBrightnessMult"] == 3.0, "Historical canonical lighting lost same-source precedence");
		Check(layer["Adaptive Balance"]["globalProfile"]["advanced"] == false, "Historical canonical lighting gate lost same-source precedence");
	} else if (test == "malformed") {
		json layer = { { "CS Utility", Legacy() } };
		layer["Adaptive Balance"]["depthOfField"]["enabled"] = "bad";
		layer["Adaptive Balance"]["depthOfField"]["sceneDof"]["values"] = { { "strength", false }, { "distance", 100.0 } };
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
		const auto& depth = layer["Adaptive Balance"]["depthOfField"];
		Check(depth["enabled"] == false && depth["sceneDof"]["values"]["strength"] == 0.4f, "Malformed destination did not recover usable legacy values");
		Check(depth["sceneDof"]["values"]["distance"] == 100.0, "Recovering malformed sibling discarded valid destination");
		layer = { { "CS Utility", Legacy() }, { "Adaptive Balance", false } };
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
		Check(layer["Adaptive Balance"]["depthOfField"] == Legacy(), "Malformed destination root prevented recovery");
	} else if (test == "disabled-at-boot") {
		for (const auto* alias : { "CSUtility", "OSUtility" }) {
			json lower = { { "CS Utility", Legacy() } };
			lower["CS Utility"]["enabled"] = true;
			SettingsMigrations::MigrateAdaptiveBalanceRootLayer(lower);
			json higher = { { "Disable at Boot", { { alias, true } } } };
			Check(SettingsMigrations::MigrateAdaptiveBalanceRootLayer(higher), "Legacy boot-disable choice was not migrated");
			lower.merge_patch(higher);
			const auto& depth = lower["Adaptive Balance"]["depthOfField"];
			Check(depth["enabled"] == false && depth["fixUnderwaterFogDofBlur"] == false, "Removed boot feature unexpectedly became active");
			Check(depth["sceneDof"] == Legacy()["sceneDof"] && !lower["Adaptive Balance"].contains("enabled"), "Boot migration changed saved values or the master gate");
			Check(!SettingsMigrations::MigrateAdaptiveBalanceRootLayer(higher), "Boot migration is not idempotent");
			json explicitChoice = { { "Disable at Boot", { { alias, true } } }, { "CS Utility", Legacy() }, { "Adaptive Balance", { { "depthOfField", { { "enabled", true }, { "fixUnderwaterFogDofBlur", true } } } } } };
			SettingsMigrations::MigrateAdaptiveBalanceRootLayer(explicitChoice);
			Check(explicitChoice["Adaptive Balance"]["depthOfField"]["enabled"] == true && explicitChoice["Adaptive Balance"]["depthOfField"]["fixUnderwaterFogDofBlur"] == true, "Explicit canonical choices did not win over obsolete boot data");
		}
	} else if (test == "dof-retained-invalid") {
		json legacy = Legacy();
		legacy["sceneDof"]["values"]["strength"] = "broken";
		legacy["sceneDof"]["values"]["future"] = { { "value", 7 } };
		legacy["underwaterDof"]["values"]["mode"] = -1;
		legacy["underwaterDof"]["values"]["blurRadius"] = 2.5;
		legacy["underwaterDof"]["baseline"]["distance"] = 1e100;
		json layer = { { "CS Utility", legacy }, { "Adaptive Balance", { { "depthOfField", { { "sceneDof", { { "values", { { "strength", 0.9 } } } } } } } } } };
		Check(SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer), "Mixed usable DOF settings did not migrate");
		const auto remaining = layer.at("CS Utility");
		Check(remaining["sceneDof"]["values"]["strength"] == "broken" && remaining["sceneDof"]["values"]["future"] == legacy["sceneDof"]["values"]["future"], "Malformed or unknown scene values were discarded");
		Check(remaining["underwaterDof"]["values"]["mode"] == -1 && remaining["underwaterDof"]["values"]["blurRadius"] == 2.5 && remaining["underwaterDof"]["baseline"]["distance"] == 1e100, "Invalid numeric DOF values were consumed");
		Check(layer["Adaptive Balance"]["depthOfField"]["sceneDof"]["values"]["strength"] == 0.9, "Explicit valid canonical value lost precedence");
		Check(!SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer) && layer.at("CS Utility") == remaining, "Retained DOF values are not idempotent");
		for (const double invalid : { 1e100, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
			json recovery = { { "CS Utility", { { "sceneDof", { { "values", { { "strength", 0.4 } } } } } } },
				{ "Adaptive Balance", { { "depthOfField", { { "sceneDof", { { "values", { { "strength", invalid } } } } } } } } } };
			Check(SettingsMigrations::MigrateAdaptiveBalanceRootLayer(recovery) && recovery["Adaptive Balance"]["depthOfField"]["sceneDof"]["values"]["strength"] == 0.4, "Invalid canonical numeric value blocked usable legacy recovery");
		}
	} else if (test == "appearance-invalid-numeric") {
		json legacy = { { "skyBrightness", 1e100 }, { "cloudBrightness", 0.7 }, { "water", { { "parallaxQuality", -1 }, { "reflectionAmount", 0.5 } } } };
		json layer = { { "CS Utility", legacy } };
		Check(SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer), "Usable appearance fields did not migrate");
		Check(layer["CS Utility"]["skyBrightness"] == 1e100 && layer["CS Utility"]["water"]["parallaxQuality"] == -1, "Unrepresentable appearance values were consumed");
		Check(layer["Adaptive Balance"]["globalProfile"]["cloudBrightnessMult"] == 0.7, "Valid sibling appearance value did not migrate");
		Check(!SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer), "Invalid retained appearance values keep replaying");
	} else if (test == "upstream-appearance") {
		json legacy = json::object();
		for (const auto& [sourceName, destinationName] : SettingsMigrations::kLegacyUtilityAppearanceFields) {
			legacy[json::json_pointer{ std::string(sourceName) }] = sourceName == "/useAmbientEffectLighting" ? json(true) : sourceName == "/water/parallaxQuality" ? json(16) :
			                                                                                                                                                          json(0.75);
		}
		legacy["vlIntensity"] = 4.0;
		legacy["sceneDof"]["locked"] = true;
		json layer = { { "CS Utility", legacy } };
		Check(SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer), "Upstream settings were not migrated");
		for (const auto& [sourceName, destinationName] : SettingsMigrations::kLegacyUtilityAppearanceFields) {
			Check(layer.at("Adaptive Balance").at(json::json_pointer{ std::string(destinationName) }) == legacy.at(json::json_pointer{ std::string(sourceName) }), "Upstream appearance field was lost or redirected incorrectly");
		}
		Check(layer["Adaptive Balance"]["globalProfile"]["advanced"] == true, "Upstream appearance values remain gated off");
		Check(layer["Adaptive Balance"]["depthOfField"]["enabled"] == true, "Upstream locked DOF was not activated");
		Check(!layer.contains("CS Utility") && layer["Adaptive Balance"]["godrayFinalBrightness"] == 4.0, "Upstream final godray brightness did not move into its global setting");
		legacy["enabled"] = false;
		layer = { { "CS Utility", legacy } };
		layer["Adaptive Balance"]["globalProfile"]["ambientMult"] = 2.0;
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
		Check(layer["Adaptive Balance"]["globalProfile"]["advanced"] == false, "Explicit legacy renderer disable was lost");
		Check(layer["Adaptive Balance"]["globalProfile"]["ambientMult"] == 2.0, "Canonical appearance did not override upstream value");
		Check(layer["Adaptive Balance"]["depthOfField"]["enabled"] == false, "Lock inference overrode explicit disabled DOF");
		layer = { { "CS Utility", { { "underwaterDof", { { "locked", true } } } } } };
		layer["Adaptive Balance"]["depthOfField"]["enabled"] = false;
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
		Check(layer["Adaptive Balance"]["depthOfField"]["enabled"] == false, "Lock inference overrode canonical disabled DOF");
	} else if (test == "godray-migration") {
		for (const auto* alias : { "CS Utility", "OS Utility" }) {
			for (const auto& value : { json(0), json(1.0), json(5.0) }) {
				json layer = { { alias, { { "vlIntensity", value } } } };
				Check(SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer), "Retained legacy godray brightness did not migrate");
				Check(layer == json({ { "Adaptive Balance", { { "godrayFinalBrightness", value } } } }), "Godray brightness affected a profile or another global control");
				Check(!SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer), "Godray brightness migration is not idempotent");
			}
		}
		json layer = { { "CS Utility", { { "vlIntensity", 5.0 } } } };
		layer["Adaptive Balance"] = { { "enabled", false }, { "godrayFinalBrightness", 0.0 } };
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
		Check(layer["Adaptive Balance"]["godrayFinalBrightness"] == 0.0 && layer["Adaptive Balance"]["enabled"] == false,
			"Legacy godray brightness overwrote explicit canonical values or enabled the feature");
		Check(!layer.contains("CS Utility"), "Canonical godray override retained an obsolete duplicate");
		json higher = { { "OS Utility", { { "vlIntensity", 5.0 } } } };
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(higher);
		layer.merge_patch(higher);
		Check(layer["Adaptive Balance"]["godrayFinalBrightness"] == 5.0, "Higher-priority legacy godray override did not win");
		for (const auto& malformed : { json(nullptr), json(false), json("5"), json::array({ 1 }), json({ { "value", 5 } }) }) {
			layer = { { "CS Utility", { { "vlIntensity", malformed }, { "enabled", false } } } };
			SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
			Check(layer["CS Utility"]["vlIntensity"] == malformed, "Malformed legacy godray value was destroyed");
			Check(!layer["Adaptive Balance"].contains("godrayFinalBrightness"), "Malformed legacy godray value was applied");
			layer = { { "CS Utility", { { "vlIntensity", 1.0 } } } };
			layer["Adaptive Balance"]["godrayFinalBrightness"] = malformed;
			SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
			Check(layer["Adaptive Balance"]["godrayFinalBrightness"] == 1.0, "Valid legacy godray value did not repair malformed destination");
		}
	} else if (test == "godray-invalid-numeric") {
		for (const double invalid : { -1.0, 5.01, 1e100, std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
			json layer = { { "CS Utility", { { "vlIntensity", invalid }, { "enabled", false } } } };
			SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
			const auto& retained = layer["CS Utility"]["vlIntensity"];
			Check(retained.is_number() && (std::isnan(invalid) ? std::isnan(retained.get<double>()) : retained == invalid), "Unrepresentable legacy godray number was destroyed");
			Check(!layer["Adaptive Balance"].contains("godrayFinalBrightness"), "Unrepresentable legacy godray number was applied");
			layer = { { "CS Utility", { { "vlIntensity", 1.0 } } } };
			layer["Adaptive Balance"]["godrayFinalBrightness"] = invalid;
			SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
			Check(layer["Adaptive Balance"]["godrayFinalBrightness"] == 1.0 && !layer.contains("CS Utility"), "Valid legacy godray number did not repair unrepresentable destination");
		}
		for (const double invalid : { -1.0, 5.01, 1e100 }) {
			Write(users / "CSUtility.user.json", { { "vlIntensity", invalid }, { "enabled", false } });
			manager.DiscoverOverrides();
			json current = { { "godrayFinalBrightness", 5.0 } };
			Check(manager.LoadUserOverride(adaptive, current), "Valid sibling beside unrepresentable godray number did not load");
			Check(current["godrayFinalBrightness"] == 5.0, "Unrepresentable retained godray number overrode canonical value");
			Check(manager.SaveUserOverride(adaptive, current, json::object()), "Valid sibling beside unrepresentable godray number did not save");
			Check(Read(users / "CSUtility.user.json") == json({ { "vlIntensity", invalid } }), "Durable migration discarded unrepresentable legacy godray number");
		}
	} else if (test == "godray-retained-user") {
		const json retained = { { "vlIntensity", 0.0 } };
		Write(users / "OSUtility.user.json", retained);
		manager.DiscoverOverrides();
		manager.CleanupStaleUserOverrides();
		json current = { { "godrayFinalBrightness", 1.0 } };
		Check(manager.LoadUserOverride(adaptive, current) && current["godrayFinalBrightness"] == 0.0, "Previously retained user godray value was not recovered without a provider");
		Fixture::failedWrite = "AdaptiveBrightness.user.json";
		Check(!manager.SaveUserOverride(adaptive, current, json::object()), "Failed godray destination write was reported as successful");
		Check(Read(users / "OSUtility.user.json") == retained, "Failed destination write removed retained godray source");
		Fixture::failedWrite.clear();
		Check(manager.SaveUserOverride(adaptive, current, json::object()), "Retained godray source did not migrate durably");
		Check(!std::filesystem::exists(users / "OSUtility.user.json"), "Durable godray migration left a replayable source");
		Check(Read(users / "AdaptiveBrightness.user.json")["godrayFinalBrightness"] == 0.0, "Zero godray choice was lost during migration");
		current["godrayFinalBrightness"] = 5.0;
		Check(manager.SaveUserOverride(adaptive, current, json::object()), "Second godray user edit failed to save");
		manager.DiscoverOverrides();
		manager.CleanupStaleUserOverrides();
		json reloaded = json::object();
		Check(manager.LoadUserOverride(adaptive, reloaded) && reloaded["godrayFinalBrightness"] == 5.0, "Saved godray choice was lost without its retired provider");
		Write(users / "CSUtility.user.json", { { "vlIntensity", "malformed" }, { "enabled", false } });
		manager.DiscoverOverrides();
		manager.LoadUserOverride(adaptive, reloaded);
		Check(manager.SaveUserOverride(adaptive, reloaded, json::object()), "Valid sibling migration failed beside malformed godray value");
		Check(Read(users / "CSUtility.user.json") == json({ { "vlIntensity", "malformed" } }), "Malformed retained godray data was silently removed");
		Check(Read(users / "AdaptiveBrightness.user.json")["godrayFinalBrightness"] == 5.0, "Malformed source overrode saved canonical godray value");
	} else if (test == "bloom-preservation") {
		const json blob = {
			{ "Enabled", 1 }, { "SelectedPreset", 1 },
			{ "Default", { { "EnhancementIntensity", 0.25 }, { "HaloRadius", 1.0 } } },
			{ "Fantasy", { { "EnhancementIntensity", 3.5 }, { "HaloRadius", 6.0 } } },
			{ "Dreamy", { { "EnhancementIntensity", 2.0 }, { "HaloRadius", 2.5 } } }
		};
		json layer = { { "CS Utility", { { "bloomEnhancement", blob } } } };
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer);
		Check(layer["Adaptive Balance"]["globalProfile"]["bloom"]["EnhancementIntensity"] == 3.5, "Selected Bloom did not migrate into global controls");
		Check(layer["Adaptive Balance"]["globalProfile"]["bloom"]["HaloRadius"] == 6.0, "Selected Bloom parameters changed");
		Check(layer["CS Utility"]["bloomEnhancement"] == blob, "Inactive/custom Bloom presets were discarded");
		layer["Adaptive Balance"]["globalProfile"]["bloom"]["EnhancementIntensity"] = 1.5;
		Check(!SettingsMigrations::MigrateAdaptiveBalanceRootLayer(layer), "Retained Bloom source keeps rewriting migrated settings");
		Check(layer["Adaptive Balance"]["globalProfile"]["bloom"]["EnhancementIntensity"] == 1.5, "Retained Bloom source overrode newer canonical choices");
		json historical = { { "CS Utility", { { "bloomEnhancement", blob } } } };
		historical["Adaptive Balance"]["bloomEnhancement"] = { { "Enabled", 0 }, { "SelectedPreset", 0 } };
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(historical);
		Check(!historical["Adaptive Balance"].contains("globalProfile"), "Legacy canonical Bloom was composed twice with utility fallback");
		Write(providers / "Bloom_CSUtility.json", { { "bloomEnhancement", blob } });
		Write(users / "CSUtility.user.json", { { "bloomEnhancement", blob }, { "vlIntensity", 4.0 } });
		manager.DiscoverOverrides();
		json current = manager.GetMergedOverrideSettings(adaptive, json::object());
		manager.LoadUserOverride(adaptive, current);
		current["globalProfile"]["bloom"]["EnhancementIntensity"] = 1.75;
		Check(manager.SaveUserOverride(adaptive, current, manager.GetMergedOverrideSettings(adaptive, json::object())), "Bloom user migration failed");
		auto retainedBloom = blob;
		retainedBloom["_adaptiveBalanceMigrated"] = true;
		Check(Read(users / "CSUtility.user.json") == json({ { "bloomEnhancement", retainedBloom } }), "User source lost inactive Bloom data or retained migrated godray brightness");
		Check(Read(users / "AdaptiveBrightness.user.json")["godrayFinalBrightness"] == 4.0, "Migrated godray brightness was not persisted beside Bloom");
		manager.DiscoverOverrides();
		json reloaded = json::object();
		manager.LoadUserOverride(adaptive, reloaded);
		Check(reloaded["globalProfile"]["bloom"]["EnhancementIntensity"] == 1.75, "Retained user source overrode newer active Bloom values");
	} else if (test == "providers") {
		Write(providers / "Scene_CSUtility.json", { { "enabled", false }, { "sceneDof", { { "values", { { "strength", 0.4 } } } } } });
		Write(providers / "Underwater_OSUtility.json", { { "fixUnderwaterFogDofBlur", true }, { "underwaterDof", { { "locked", false } } }, { "vlIntensity", 5.0 } });
		Write(providers / "Global_Global.json", { { "OS Utility", { { "enabled", true } } }, { "Adaptive Balance", { { "depthOfField", { { "enabled", false } } } } } });
		Check(manager.DiscoverOverrides() == 3, "Legacy provider discovery failed");
		Check(manager.HasFeatureOverrides(adaptive), "Legacy utility provider was not mapped to Adaptive Balance");
		json target = json::object();
		Check(manager.ApplyOverrides(adaptive, target) == 2, "Both aliases were not routed");
		Check(target["depthOfField"]["sceneDof"]["values"]["strength"] == 0.4 && target["depthOfField"]["underwaterDof"]["locked"] == false, "Routed data missing");
		Check(target["depthOfField"]["enabled"] == false && target["depthOfField"]["fixUnderwaterFogDofBlur"] == true, "Routed independent booleans changed");
		Check(target["godrayFinalBrightness"] == 5.0 && !target.contains("globalProfile"), "Legacy provider godray value did not remain a global control");
		Check(!manager.GetCombinedOverrideHash(adaptive).empty(), "Routed overrides lack source hash identity");
		json global = json::object();
		Check(manager.ApplyGlobalOverrides(global) == 1 && global["Adaptive Balance"]["depthOfField"]["enabled"] == false, "Global alias/canonical precedence failed");
	} else if (test == "user-migration" || test == "user-failure") {
		Write(providers / "Scene_CSUtility.json", { { "enabled", true } });
		const auto legacy = Legacy();
		Write(users / "CSUtility.user.json", legacy);
		Write(users / "OSUtility.user.json", { { "sceneDof", { { "values", { { "strength", 0.1 } } } } } });
		Write(users / "AdaptiveBrightness.user.json", { { "depthOfField", { { "sceneDof", { { "values", { { "strength", 0.9 } } } } } } } });
		manager.DiscoverOverrides();
		json current = manager.GetMergedOverrideSettings(adaptive, json::object());
		Check(manager.LoadUserOverride(adaptive, current), "Legacy user layer did not load");
		Check(current["depthOfField"]["sceneDof"]["values"]["strength"] == 0.9, "Native user values did not win over aliases");
		Check(Read(users / "CSUtility.user.json") == legacy, "Loading consumed the only durable legacy source");
		const auto base = manager.GetMergedOverrideSettings(adaptive, json::object());
		if (test == "user-failure") {
			Fixture::failedWrite = "AdaptiveBrightness.user.json";
			Check(!manager.SaveUserOverride(adaptive, current, base), "Injected destination failure was reported as success");
			Check(Read(users / "CSUtility.user.json") == legacy, "Failed destination write destroyed legacy values");
			Fixture::failedWrite.clear();
			auto changed = legacy;
			changed["sceneDof"]["values"]["strength"] = 0.6;
			Write(users / "CSUtility.user.json", changed);
			Check(!manager.SaveUserOverride(adaptive, current, base), "Concurrent source change was silently consumed");
			Check(Read(users / "CSUtility.user.json") == changed, "Concurrent source edit was overwritten");
			Write(users / "CSUtility.user.json", legacy);
		}
		Check(manager.SaveUserOverride(adaptive, current, base), "Migration retry did not finish");
		Check(!std::filesystem::exists(users / "CSUtility.user.json") && !std::filesystem::exists(users / "OSUtility.user.json"), "Successful durable migration left replayable aliases");
		const auto saved = Read(users / "AdaptiveBrightness.user.json");
		Check(saved["depthOfField"] == current["depthOfField"], "User fields absent from provider mask were lost");
		manager.DiscoverOverrides();
		json reloaded = base;
		manager.LoadUserOverride(adaptive, reloaded);
		Check(reloaded["depthOfField"] == current["depthOfField"], "Saved canonical user layer did not roundtrip");
		reloaded["depthOfField"]["sceneDof"]["values"]["strength"] = 0.6;
		Check(manager.SaveUserOverride(adaptive, reloaded, base), "Second canonical save failed");
		Check(Read(users / "AdaptiveBrightness.user.json")["depthOfField"] == reloaded["depthOfField"], "Second save discarded migrated fields outside provider mask");
		std::filesystem::remove(providers / "Scene_CSUtility.json");
		manager.DiscoverOverrides();
		manager.CleanupStaleUserOverrides();
		json withoutProvider = json::object();
		Check(manager.LoadUserOverride(adaptive, withoutProvider), "Removing provider lost the durable user layer");
		Check(withoutProvider["depthOfField"] == reloaded["depthOfField"], "Provider removal lost saved migrated settings");
	} else if (test == "provider-rename") {
		Write(providers / "Scene_AdaptiveBrightness.json", { { "depthOfField", { { "enabled", true } } } });
		Write(users / "OSUtility.user.json", Legacy());
		manager.DiscoverOverrides();
		manager.CleanupStaleUserOverrides();
		Check(std::filesystem::exists(users / "OSUtility.user.json"), "Provider rename prematurely deleted recoverable legacy user values");
		Check(manager.HasUserOverride(adaptive), "Legacy user state is missing from destination UI");
		json current = json::object();
		Check(manager.LoadUserOverride(adaptive, current) && current["depthOfField"] == Legacy(), "Renamed provider did not retain user values");
		Check(manager.DeleteUserOverride(adaptive), "Apply Override did not clear user state");
		Check(!std::filesystem::exists(users / "OSUtility.user.json"), "Cleared alias would replay on next load");
	} else if (test == "reset-preservation") {
		Write(providers / "Scene_CSUtility.json", { { "enabled", true } });
		auto legacy = Legacy();
		legacy["opaque"] = { { "future", 42 } };
		legacy["sceneDof"]["values"]["future"] = "preserved";
		legacy["underwaterDof"]["values"]["strength"] = "invalid";
		legacy["bloomEnhancement"] = { { "Enabled", 1 }, { "SelectedPreset", 1 }, { "Fantasy", { { "EnhancementIntensity", 0.7 } } }, { "Default", { { "HaloRadius", 9.0 } } } };
		Write(users / "CSUtility.user.json", legacy);
		Write(users / "AdaptiveBrightness.user.json", { { "depthOfField", { { "enabled", false } } } });
		manager.DiscoverOverrides();
		Fixture::failedWrite = "CSUtility.user.json";
		Check(!manager.DeleteUserOverride(adaptive), "Failed selective reset was reported as success");
		Check(Read(users / "CSUtility.user.json") == legacy && std::filesystem::exists(users / "AdaptiveBrightness.user.json"), "Failed reset discarded a source or canonical user layer");
		Fixture::failedWrite.clear();
		Check(manager.DeleteUserOverride(adaptive), "Selective reset failed");
		const auto retained = Read(users / "CSUtility.user.json");
		Check(retained["opaque"] == legacy["opaque"] && retained["sceneDof"]["values"]["future"] == "preserved" && retained["underwaterDof"]["values"]["strength"] == "invalid", "Reset discarded unsupported user data");
		Check(retained["bloomEnhancement"]["Default"] == legacy["bloomEnhancement"]["Default"] && retained["bloomEnhancement"]["Fantasy"] == legacy["bloomEnhancement"]["Fantasy"], "Reset lost retained Bloom presets");
		Check(!manager.HasUserOverride(adaptive), "Opaque retained data masquerades as an Adaptive Balance user override");
		manager.DiscoverOverrides();
		json current = json::object();
		Check(!manager.LoadUserOverride(adaptive, current) && current.empty(), "Retained Bloom or unsupported fields replayed after reset");
	} else if (test == "disabled-provider") {
		Write(providers / "Scene_CSUtility.json", { { "_metadata", { { "enabled", false } } }, { "enabled", true } });
		Write(users / "CSUtility.user.json", Legacy());
		manager.DiscoverOverrides();
		json current = json::object();
		Check(manager.ApplyOverrides(adaptive, current) == 0, "Disabled legacy provider applied");
		Check(!manager.LoadUserOverride(adaptive, current), "Disabled legacy provider applied its user layer");
		Check(manager.SaveUserOverride(adaptive, current, json::object()), "Empty destination delta did not save");
		Check(Read(users / "CSUtility.user.json") == Legacy(), "Automatic destination cleanup discarded unapplied legacy user layer");
	} else {
		Check(false, "Unknown case");
	}
	std::cout << test << " passed\n";
}
