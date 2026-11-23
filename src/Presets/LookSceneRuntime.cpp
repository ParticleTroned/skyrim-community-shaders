#include "LookSceneRuntime.h"

#include "Feature.h"
#include "Globals.h"
#include "LookScenes.h"
#include "SceneSettingsManager.h"
#include "State.h"
#include "Utils/Form.h"
#include "Utils/Game.h"
#include "Utils/LocationLayers.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"
#include <chrono>
#include <map>
#include <set>
#include <utility>

namespace Presets::LookSceneRuntime
{
	namespace
	{
		struct PreparedFeature
		{
			Json values;
			bool flattened = false;
			/** Prepare fields only after the caller's runtime eligibility checks. */
			const Json& Fields()
			{
				if (!flattened) {
					values = values.flatten();
					flattened = true;
				}
				return values;
			}
		};
		using PreparedFields = std::map<std::string, PreparedFeature>;
		std::map<std::string, Json> layers;
		std::map<std::string, bool> periodicContexts;
		std::optional<LookSceneContext> lastContext;
		Json manifest = Json::object(), saved = Json::object(), transitionFrom = Json::object();
		PreparedFields previousFields;
		// Every saved field keeps its parsed address until profile restoration completes.
		std::map<std::string, Json::json_pointer, std::less<>> fieldAddresses;
		std::set<std::string> features;
		Json previousLocations = Json::object();
		uint32_t lastFrame = ~uint32_t{ 0 };
		std::vector<std::string> lastLocations;
		std::chrono::steady_clock::time_point lastTick{};
		float transitionStart = 0;
		Json cachedBase = Json::object(), authoredBase = Json::object();
		unsigned suspended = 0;
		std::string failure;

		const Json::json_pointer& FieldAddress(const std::string& path)
		{
			return fieldAddresses.try_emplace(path, path).first->second;
		}

		PreparedFields PrepareFields(Json resolved)
		{
			PreparedFields fields;
			for (auto& [name, values] : resolved.items()) fields.emplace(name, PreparedFeature{ std::move(values) });
			return fields;
		}

		void Restore()
		{
			const auto interior = Util::IsInterior() ? SceneSettingsManager::GetSingleton()->ExportLookLayer() : Json::object();
			for (const auto& [name, fields] : saved.items()) {
				auto* feature = Feature::FindFeatureByShortName(name);
				if (!feature || !feature->loaded)
					continue;
				Json settings;
				feature->SaveSettings(settings);
				for (const auto& [path, value] : fields.items()) settings[fieldAddresses.at(path)] = value;
				if (interior.contains(name) && !SceneSettingsManager::GetSingleton()->IsFeaturePaused(name))
					OverlayScene(settings, interior[name]);
				feature->LoadSettings(settings);
			}
			saved = Json::object();
			previousFields.clear();
			previousLocations = Json::object();
		}

		LookSceneContext Context()
		{
			LookSceneContext context;
			context.interior = Util::IsInterior();
			auto* sky = globals::game::sky;
			std::array<float, 6> starts{ 5, 7.5f, 10, 16, 18.5f, 21 };
			if (sky && sky->currentClimate) {
				const auto& timing = sky->currentClimate->timing;
				const auto dawn = timing.sunrise.begin / 6.f, day = timing.sunrise.end / 6.f;
				const auto sunset = timing.sunset.begin / 6.f, night = timing.sunset.end / 6.f;
				if (dawn < day && day < sunset && sunset < night)
					starts = { dawn, (dawn + day) * .5f, day, sunset, (sunset + night) * .5f, night };
			}
			auto* calendar = globals::game::calendar;
			const auto hour = calendar && calendar->gameHour ? calendar->gameHour->value : sky ? sky->currentGameHour :
			                                                                                     12.f;
			context.periods = ScenePeriodWeights(starts, hour, manifest.value("periodTransitionHours", 1.f), manifest.value("transitionEase", 1.f));
			const auto weather = WeatherManager::GetSingleton()->GetCurrentWeathers();
			context.weather = weather.currentWeather ? SceneFormKey(Util::GetFormFileKey(weather.currentWeather)) : "";
			context.previousWeather = weather.lastWeather ? SceneFormKey(Util::GetFormFileKey(weather.lastWeather)) : context.weather;
			const auto linear = std::clamp(weather.lerpFactor, 0.f, 1.f);
			const auto smooth = linear * linear * linear * (linear * (linear * 6 - 15) + 10);
			context.weatherWeight = std::lerp(linear, smooth, manifest.value("transitionEase", 1.f));
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				auto* cell = player->GetParentCell();
				context.cell = cell ? SceneFormKey(Util::GetFormFileKey(cell)) : "";
				context.worldspace = player->GetWorldspace() ? SceneFormKey(Util::GetFormFileKey(player->GetWorldspace())) : "";
				Util::VisitLocationLayers(player->GetWorldspace(), player->GetCurrentLocation(), cell,
					[&](const auto* form) { context.locations.push_back(SceneFormKey(Util::GetFormFileKey(form))); });
			}
			return context;
		}

		void Apply(const Json& base, PreparedFields& fields, bool weatherOnly = false)
		{
			if (weatherOnly && std::ranges::none_of(features, [](const auto& name) { return WeatherManager::GetSingleton()->HasActiveVariablesForFeature(name); }))
				return;
			const auto beforeSaved = saved;
			std::map<std::string, Json> snapshots;
			try {
				for (const auto& name : features) {
					if (weatherOnly && !WeatherManager::GetSingleton()->HasActiveVariablesForFeature(name))
						continue;
					auto* feature = Feature::FindFeatureByShortName(name);
					if (!feature || !feature->loaded || !base.contains(name))
						continue;
					Json current;
					feature->SaveSettings(current);
					auto candidate = current;
					if (saved.contains(name))
						for (const auto& [path, value] : saved[name].items()) {
							const auto& address = fieldAddresses.at(path);
							candidate[address] = base[name].contains(address) ? base[name][address] : value;
						}
					Json nextSaved = Json::object();
					if (const auto found = fields.find(name); found != fields.end() && !SceneSettingsManager::GetSingleton()->IsFeaturePaused(name)) {
						for (const auto& [path, value] : found->second.Fields().items()) {
							if (path.empty() || value.is_null())
								continue;
							const auto& address = FieldAddress(path);
							if (!base[name].contains(address))
								continue;
							nextSaved[path] = authoredBase[name][address];
							candidate[address] = value;
						}
					}
					if (candidate != current) {
						snapshots[name] = current;
						feature->LoadSettings(candidate);
					}
					if (nextSaved.empty())
						saved.erase(name);
					else
						saved[name] = std::move(nextSaved);
				}
			} catch (const std::exception& failure) {
				std::string message = failure.what();
				for (auto& [name, settings] : snapshots) {
					try {
						Feature::FindFeatureByShortName(name)->LoadSettings(settings);
					} catch (const std::exception& rollback) {
						message += std::string("; rollback failed: ") + rollback.what();
					}
				}
				saved = beforeSaved;
				throw std::runtime_error(message);
			}
		}
	}

	void Suspend()
	{
		if (!suspended)
			Restore();
		++suspended;
	}
	void Resume()
	{
		if (suspended)
			--suspended;
		lastTick = {};
		lastFrame = ~uint32_t{ 0 };
		lastContext.reset();
		cachedBase = Json::object();
	}
	void Use(const LookPack* pack)
	{
		Restore();
		fieldAddresses.clear();
		layers.clear();
		const auto contextKey = [](const std::string& key) {
			if (!key.starts_with("Weather/") && !key.starts_with("Locations/"))
				return key;
			const auto first = key.find('/') + 1;
			const auto last = key.find('/', first);
			return key.substr(0, first) + SceneFormKey(std::string_view(key).substr(first, last - first)) +
			       (last == key.npos ? std::string{} : key.substr(last));
		};
		if (pack)
			for (const auto& [key, values] : pack->scenes)
				if (!layers.emplace(contextKey(key), values).second)
					throw std::runtime_error("Duplicate scene context: " + key);
		manifest = pack ? pack->manifest : Json::object();
		periodicContexts.clear();
		if (pack)
			for (const auto& [key, enabled] : pack->periodicContexts) periodicContexts.emplace(contextKey(key), enabled);
		lastContext.reset();
		cachedBase = Json::object();
		features.clear();
		for (const auto& [context, values] : layers)
			for (const auto& [name, settings] : values.items()) features.insert(name);
		lastLocations.clear();
		transitionFrom = Json::object();
		lastTick = {};
		failure.clear();
		lastFrame = ~uint32_t{ 0 };
	}
	void OverlayBase(const std::string& feature, Json& settings)
	{
		if (const auto found = saved.find(feature); found != saved.end())
			for (const auto& [path, value] : found->items()) settings[fieldAddresses.at(path)] = value;
	}
	bool Controls(const std::string& feature, const std::string& root)
	{
		const auto found = saved.find(feature);
		if (found == saved.end())
			return false;
		if (SceneSettingsManager::GetSingleton()->IsFeaturePaused(feature))
			return false;
		if (root.empty())
			return !found->empty();
		const auto prefix = "/" + root;
		return std::ranges::any_of(found->items(), [&](const auto& field) { return field.key() == prefix || field.key().starts_with(prefix + "/"); });
	}
	const std::string& Error() { return failure; }
	void Update()
	{
		if (suspended || layers.empty() || !failure.empty() || globals::state->isMapMenuOpen)
			return;
		if (lastFrame == globals::state->frameCount)
			return;
		lastFrame = globals::state->frameCount;
		const auto now = std::chrono::steady_clock::now();
		const bool resolveDue = now - lastTick >= std::chrono::milliseconds(33);
		try {
			if (!RE::PlayerCharacter::GetSingleton() || globals::state->isLoadingMenuOpen || globals::state->isMainMenuOpen) {
				Restore();
				lastContext.reset();
				return;
			}
			if (!resolveDue && !previousFields.empty()) {
				Apply(cachedBase, previousFields, true);
				return;
			}
			lastTick = now;
			const auto context = Context();
			const auto duration = manifest.value("locationTransitionSeconds", 5.f);
			const auto sinceTransition = globals::state->timer - transitionStart;
			if (lastContext && *lastContext == context && (sinceTransition < 0 || sinceTransition >= duration)) {
				Apply(cachedBase, previousFields, true);
				return;
			}
			Json base = Json::object();
			for (const auto& name : features) {
				auto* feature = Feature::FindFeatureByShortName(name);
				if (feature && feature->loaded)
					feature->SaveBaseSettings(base[name]);
			}
			authoredBase = base;
			if (context.interior)
				OverlayScene(base, SceneSettingsManager::GetSingleton()->ExportLookLayer());
			auto globalContext = context;
			globalContext.locations.clear();
			auto resolved = ResolveLookScenes(layers, globalContext, base, periodicContexts);
			auto locationBase = base;
			OverlayScene(locationBase, resolved);
			auto locations = ResolveLookLocations(layers, context, locationBase, periodicContexts);
			cachedBase = base;
			if (lastLocations != context.locations) {
				transitionFrom = previousLocations;
				transitionStart = lastContext && !lastContext->interior && !context.interior && lastContext->worldspace == context.worldspace && lastContext->cell != context.cell ? globals::state->timer : -1000.f;
				lastLocations = context.locations;
			}
			const auto elapsed = globals::state->timer - transitionStart;
			if (duration > 0 && elapsed >= 0 && elapsed < duration) {
				const auto progress = std::clamp(elapsed / duration, 0.f, 1.f);
				const auto smooth = progress * progress * progress * (progress * (progress * 6 - 15) + 10);
				locations = BlendSceneLayers(locationBase, transitionFrom, locations, std::lerp(progress, smooth, manifest.value("transitionEase", 1.f)));
			}
			OverlayScene(resolved, locations);
			auto fields = PrepareFields(std::move(resolved));
			Apply(base, fields);
			previousLocations = std::move(locations);
			// Weather reapplication consumes prepared fields until the next context resolution.
			previousFields = std::move(fields);
			lastContext = context;
		} catch (const std::exception& error) {
			failure = error.what();
			try {
				Restore();
			} catch (const std::exception& rollback) {
				failure += std::string("; restore failed: ") + rollback.what();
			}
			logger::error("Visuals scene profile stopped: {}", failure);
		}
	}
}
