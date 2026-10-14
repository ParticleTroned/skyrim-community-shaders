#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	void Require(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "Particle maintenance assertion failed at " << a_location.file_name() << ':' << a_location.line() << '\n';
			std::abort();
		}
	}

	bool failNextKeyAppend = false;
	bool failNextMapInsertion = false;
	namespace eastl
	{
		template <class T>
		class vector : public std::vector<T>
		{
			using Base = std::vector<T>;

		public:
			using Base::Base;
			void push_back(const T& a_value)
			{
				if (std::exchange(failNextKeyAppend, false))
					throw std::bad_alloc();
				Base::push_back(a_value);
			}
		};
		template <class Key, class Value, class Hash = std::hash<Key>>
		class hash_map : public std::unordered_map<Key, Value, Hash>
		{
			using Base = std::unordered_map<Key, Value, Hash>;

		public:
			using Base::Base;
			template <class... Args>
			auto emplace(Args&&... a_args)
			{
				if (std::exchange(failNextMapInsertion, false))
					throw std::bad_alloc();
				return Base::emplace(std::forward<Args>(a_args)...);
			}
		};
	}
	namespace SKSE::stl
	{
		template <class Fn>
		class scope_exit
		{
			Fn callback;
			bool active = true;

		public:
			explicit scope_exit(Fn a_callback) : callback(std::move(a_callback)) {}
			scope_exit(const scope_exit&) = delete;
			~scope_exit()
			{
				if (active)
					callback();
			}
			void release() noexcept { active = false; }
		};
	}

	std::function<void(std::string_view)> nameRetirement;
	std::function<void(std::string_view)> engineRetirement;
	namespace RE
	{
		struct NiColorA
		{
			float red = 1, green = 1, blue = 1, alpha = 1;
			bool operator==(const NiColorA&) const = default;
		};
		struct NiPoint3
		{
			float x = 0, y = 0, z = 0;
		};
		class BSFixedString
		{
			struct Name
			{
				std::string value;
				explicit Name(std::string a_value) : value(std::move(a_value)) {}
				~Name()
				{
					if (nameRetirement)
						nameRetirement(value);
				}
			};
			std::shared_ptr<Name> value;
			static auto& Pool()
			{
				static std::unordered_map<std::string, std::weak_ptr<Name>> pool;
				return pool;
			}

		public:
			BSFixedString() = default;
			explicit BSFixedString(std::string a_value)
			{
				if (a_value.empty())
					return;
				auto& weak = Pool()[a_value];
				value = weak.lock();
				if (!value) {
					value = std::make_shared<Name>(std::move(a_value));
					weak = value;
				}
			}
			bool empty() const { return !value; }
			const char* c_str() const { return value ? value->value.c_str() : ""; }
			bool operator==(const BSFixedString& a_other) const { return value == a_other.value; }
		};
		struct BSShaderMaterial
		{};
		struct BSEffectShaderMaterial : BSShaderMaterial
		{
			BSFixedString sourceTexturePath;
			BSFixedString greyscaleTexturePath;
			NiColorA tint;
		};
		namespace BSGraphics
		{
			struct TriShape
			{
				const std::uint8_t* rawVertexData = nullptr;
			};
		}
		inline const int particleRtti = 1, billboardRtti = 2, geometryRtti = 3;
		struct BSGeometry
		{
			BSGeometry* parent = nullptr;
			const int* rtti = &particleRtti;
			struct RuntimeData
			{
				BSGraphics::TriShape* rendererData = nullptr;
			} runtime;
			const int* GetRTTI() const { return rtti; }
			RuntimeData& GetGeometryRuntimeData() { return runtime; }
		};
		struct NiParticleSystem : BSGeometry
		{
			std::string name;
			~NiParticleSystem()
			{
				if (!name.empty() && engineRetirement)
					engineRetirement(name);
			}
		};
		struct NiPSysData
		{
			std::string name;
			~NiPSysData()
			{
				if (!name.empty() && engineRetirement)
					engineRetirement(name);
			}
		};
		template <class T>
		using NiPointer = std::shared_ptr<T>;
		struct BSShaderProperty
		{};
		struct BSEffectShaderProperty : BSShaderProperty
		{
			BSEffectShaderMaterial* material = nullptr;
			BSEffectShaderMaterial* GetMaterial() { return material; }
		};
		struct BSRenderPass
		{
			BSGeometry* geometry;
			BSShaderProperty* shaderProperty;
		};
	}
	using float3 = RE::NiPoint3;
	struct ParticleLights
	{
		struct Config
		{
			bool cull = false;
			RE::NiColorA colorMult;
			float radiusMult = 1, saturationMult = 1;
			bool flicker = false;
			float flickerSpeed = 1, flickerIntensity = 0, flickerMovement = 0;
		};
		struct GradientConfig
		{
			RE::NiColorA color;
		};
		std::unordered_map<std::string, Config> particleLightConfigs;
		std::unordered_map<std::string, GradientConfig> particleLightGradientConfigs;
		std::uint64_t configVersion = 1;
	};
	namespace globals
	{
		struct State
		{
			std::uint32_t frameCount = 0;
			bool IsFeatureDisabled(std::string_view) const { return false; }
		} stateStorage;
		State* state = &stateStorage;
		namespace features::llf
		{
			ParticleLights particleLights;
		}
		namespace rtti
		{
			struct Rtti
			{
				const int* value;
				const int* get() const { return value; }
			};
			Rtti NiParticleSystemRTTI{ &RE::particleRtti };
			Rtti NiBillboardNodeRTTI{ &RE::billboardRtti };
		}
	}
	namespace LightLimitFixDetail::ShadowCulling
	{
		void Update(bool, bool) {}
	}
	namespace Util
	{
		unsigned stemQueries = 0;
		std::optional<std::string> GetLowercaseStem(const char* a_path, std::string_view a_extension)
		{
			++stemQueries;
			std::string path(a_path);
			if (path.size() <= a_extension.size() || !path.ends_with(a_extension))
				return std::nullopt;
			path.resize(path.size() - a_extension.size());
			auto separator = path.find_last_of("/\\");
			if (separator != std::string::npos)
				path.erase(0, separator + 1);
			for (auto& character : path)
				character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			return path;
		}
	}
	static constexpr unsigned MAX_LIGHTS = 1024;

#include "particle_light_maintenance_constants_under_test.h"

	struct LightLimitFix
	{
#include "particle_light_maintenance_declarations_under_test.h"

		struct Settings
		{
			bool ShadowCulling = false;
		} settings;
		bool loaded = true;
		std::vector<int> effectLightValidationCache, sceneLightSnapshots, jsonPlacedLightCache;
		bool sceneLightSnapshotFailed = false;
		std::string_view GetShortName() const { return "LLF"; }
		void Reset();
		void PruneParticleLightCache(std::uint32_t a_frame);
		void RetireParticleLightCacheEntry(ParticleLightCacheMap::iterator a_entry, ParticleLightCacheEntry& a_retired);
		ParticleLightReference GetParticleLightConfigs(RE::BSRenderPass* a_pass, RE::BSEffectShaderProperty* a_property);
	};

	unsigned tintQueries = 0;
	std::function<void()> duringTintResolve;
	void ResolveBillboardTint(LightLimitFix::ParticleLightReference& a_reference, RE::BSGeometry*, RE::BSEffectShaderMaterial* a_material, RE::BSEffectShaderProperty*)
	{
		++tintQueries;
		if (duringTintResolve) {
			auto callback = std::exchange(duringTintResolve, {});
			callback();
		}
		a_reference.baseColor = a_material->tint;
	}

#include "particle_light_maintenance_methods_under_test.h"

#include "particle_light_maintenance_lookup_under_test.h"

	using Key = LightLimitFix::ParticleLightKey;
	using Entry = LightLimitFix::ParticleLightCacheEntry;

	void RequireUnlocked(LightLimitFix& a_lights)
	{
		bool cache = false, current = false, queue = false;
		std::thread probe([&] {
			cache = a_lights.particleLightsCacheMutex.try_lock();
			if (cache)
				a_lights.particleLightsCacheMutex.unlock();
			current = a_lights.currentParticleLightsMutex.try_lock();
			if (current)
				a_lights.currentParticleLightsMutex.unlock();
			queue = a_lights.particleLightsQueueMutex.try_lock();
			if (queue)
				a_lights.particleLightsQueueMutex.unlock();
		});
		probe.join();
		Require(cache && current && queue);
	}

	void RequireConsistent(const LightLimitFix& a_lights)
	{
		Require(a_lights.particleLightsReferences.size() == a_lights.particleLightMaintenanceKeys.size());
		Require(a_lights.particleLightDiagnostics.cacheEntries.load() == a_lights.particleLightsReferences.size());
		Require(a_lights.particleLightMaintenanceKeys.empty() || a_lights.particleLightMaintenanceCursor <= a_lights.particleLightMaintenanceKeys.size());
		for (std::size_t index = 0; index < a_lights.particleLightMaintenanceKeys.size(); ++index) {
			const auto key = a_lights.particleLightMaintenanceKeys[index];
			const auto found = a_lights.particleLightsReferences.find(key);
			Require(found != a_lights.particleLightsReferences.end());
			Require(found->second.maintenanceIndex == index);
		}
	}

	struct Fixture
	{
		LightLimitFix lights;
		RE::NiParticleSystem geometry;
		RE::BSGeometry billboardParent;
		RE::BSEffectShaderMaterial material;
		std::vector<RE::BSEffectShaderProperty> properties;
		explicit Fixture(std::size_t a_properties = 16) : properties(a_properties)
		{
			globals::state = &globals::stateStorage;
			globals::stateStorage.frameCount = 0;
			globals::features::llf::particleLights = {};
			globals::features::llf::particleLights.particleLightConfigs["spell"].radiusMult = 2;
			material.sourceTexturePath = RE::BSFixedString("spell.dds");
			for (auto& property : properties)
				property.material = &material;
			lights.particleLightMaintenanceKeys.reserve(kParticleLightCacheMaxEntries);
			billboardParent.rtti = &RE::billboardRtti;
			nameRetirement = {};
			engineRetirement = {};
			failNextKeyAppend = failNextMapInsertion = false;
			tintQueries = Util::stemQueries = 0;
			duringTintResolve = {};
		}
		~Fixture()
		{
			nameRetirement = {};
			engineRetirement = {};
			duringTintResolve = {};
		}
		Key GetKey(std::size_t a_index) { return { &geometry, &properties[a_index] }; }
		auto Lookup(std::size_t a_index, std::uint32_t a_frame = 0)
		{
			globals::stateStorage.frameCount = a_frame;
			RE::BSRenderPass pass{ &geometry, &properties[a_index] };
			return lights.GetParticleLightConfigs(&pass, &properties[a_index]);
		}
		void Seed(std::size_t a_count, std::uint32_t a_frame = 0)
		{
			for (std::size_t index = 0; index < a_count; ++index)
				Require(Lookup(index, a_frame).valid);
			RequireConsistent(lights);
		}
		void Retire(std::size_t a_index)
		{
			Entry retired;
			{
				std::lock_guard lock(lights.particleLightsCacheMutex);
				lights.RetireParticleLightCacheEntry(lights.particleLightsReferences.find(GetKey(a_index)), retired);
			}
			RequireConsistent(lights);
		}
	};

	void TestCursorRepairAndRehash()
	{
		Fixture f;
		f.Seed(8, 10);
		f.lights.particleLightMaintenanceCursor = 2;
		f.Retire(2);
		Require(f.lights.particleLightMaintenanceKeys[2] == f.GetKey(7));
		Require(f.lights.particleLightMaintenanceCursor == 2);
		f.Retire(6);
		Require(f.lights.particleLightMaintenanceCursor == 2);
		f.Retire(0);
		RequireConsistent(f.lights);
		f.lights.particleLightsReferences.rehash(257);
		RequireConsistent(f.lights);
		f.lights.particleLightMaintenanceCursor = f.lights.particleLightMaintenanceKeys.size() - 1;
		const auto tail = f.lights.particleLightMaintenanceKeys.back();
		Entry retired;
		{
			std::lock_guard lock(f.lights.particleLightsCacheMutex);
			f.lights.RetireParticleLightCacheEntry(f.lights.particleLightsReferences.find(tail), retired);
		}
		Require(f.lights.particleLightMaintenanceCursor == 0);
		f.Seed(8, 11);
		RequireConsistent(f.lights);
		++globals::features::llf::particleLights.configVersion;
		f.lights.PruneParticleLightCache(12);
		Require(f.lights.particleLightsReferences.empty());
		Require(f.lights.particleLightMaintenanceCursor == 0);
		RequireConsistent(f.lights);
	}

	void TestBoundedServiceAndFrameWrap()
	{
		Fixture f(1000);
		f.Seed(1000);
		f.lights.PruneParticleLightCache(600);
		Require(f.lights.particleLightsReferences.size() == 1000);
		const auto cursor = f.lights.particleLightMaintenanceCursor;
		Require(cursor == kParticleLightCachePruneBudget);
		f.lights.PruneParticleLightCache(600);
		Require(f.lights.particleLightMaintenanceCursor == cursor);
		f.lights.PruneParticleLightCache(601);
		Require(f.lights.particleLightsReferences.size() == 1000 - kParticleLightCachePruneBudget);
		f.lights.PruneParticleLightCache(601);
		Require(f.lights.particleLightsReferences.size() == 1000 - kParticleLightCachePruneBudget);
		++globals::features::llf::particleLights.configVersion;
		for (std::uint32_t frame = 602; !f.lights.particleLightsReferences.empty(); ++frame) {
			const auto previous = f.lights.particleLightsReferences.size();
			f.lights.PruneParticleLightCache(frame);
			Require(previous - f.lights.particleLightsReferences.size() == std::min(previous, kParticleLightCachePruneBudget));
			RequireConsistent(f.lights);
		}
		Fixture initial;
		initial.Seed(1);
		++globals::features::llf::particleLights.configVersion;
		initial.lights.PruneParticleLightCache(0);
		Require(initial.lights.particleLightsReferences.empty());
		Fixture wrap;
		wrap.Seed(1, std::numeric_limits<std::uint32_t>::max() - 500);
		wrap.lights.PruneParticleLightCache(99);
		Require(wrap.lights.particleLightsReferences.size() == 1);
		wrap.lights.PruneParticleLightCache(100);
		Require(wrap.lights.particleLightsReferences.empty());
		RequireConsistent(wrap.lights);
	}

	void TestExpiryBeforeTouchAndInvalidation()
	{
		Fixture exact;
		const auto reference = exact.Lookup(0);
		const auto probes = Util::stemQueries;
		Require(exact.Lookup(0, 600).config.radiusMult == reference.config.radiusMult);
		Require(Util::stemQueries == probes);
		Require(exact.lights.particleLightsReferences.at(exact.GetKey(0)).lastSeenFrame == 600);
		Fixture expired;
		const auto original = expired.Lookup(0);
		const auto oldProbes = Util::stemQueries;
		const auto refreshed = expired.Lookup(0, 601);
		Require(refreshed.valid == original.valid && refreshed.config.radiusMult == original.config.radiusMult);
		Require(Util::stemQueries == oldProbes + 1);
		Require(expired.lights.particleLightsReferences.at(expired.GetKey(0)).lastSeenFrame == 601);
		RequireConsistent(expired.lights);
		expired.material.sourceTexturePath = RE::BSFixedString("unknown.dds");
		Require(!expired.Lookup(0, 602).valid);
		const auto invalidProbes = Util::stemQueries;
		Require(!expired.Lookup(0, 1203).valid);
		Require(Util::stemQueries == invalidProbes + 1);
		expired.material.sourceTexturePath = RE::BSFixedString("spell.dds");
		expired.material.greyscaleTexturePath = RE::BSFixedString("gradient.dds");
		globals::features::llf::particleLights.particleLightGradientConfigs["gradient"].color = { 0, 1, 0, 1 };
		auto gradient = expired.Lookup(0, 1204);
		Require(gradient.hasGradientConfig && gradient.gradientConfig.color.green == 1);
		++globals::features::llf::particleLights.configVersion;
		globals::features::llf::particleLights.particleLightConfigs["spell"].radiusMult = 3;
		Require(expired.Lookup(0, 1205).config.radiusMult == 3);
		RequireConsistent(expired.lights);
	}

	void TestBillboardReplacementAndFinalNameRelease()
	{
		Fixture f;
		f.geometry.rtti = &RE::geometryRtti;
		f.geometry.parent = &f.billboardParent;
		Require(f.Lookup(0).valid);
		const auto index = f.lights.particleLightsReferences.at(f.GetKey(0)).maintenanceIndex;
		f.material.tint = { 0, 1, 0, 1 };
		Require(f.Lookup(0, 1).baseColor == f.material.tint);
		Require(tintQueries == 2);
		Require(f.lights.particleLightsReferences.at(f.GetKey(0)).maintenanceIndex == index);
		Require(f.lights.particleLightMaintenanceKeys.size() == 1);
		f.Lookup(0, 1);
		Require(tintQueries == 2);
		unsigned releases = 0;
		const auto caller = std::this_thread::get_id();
		nameRetirement = [&](std::string_view a_name) {
			if (a_name == "retired.dds") {
				++releases;
				Require(std::this_thread::get_id() == caller);
				RequireUnlocked(f.lights);
			}
		};
		duringTintResolve = [&] {
			std::lock_guard lock(f.lights.particleLightsCacheMutex);
			f.lights.particleLightsReferences.at(f.GetKey(0)).sourceTexturePath = RE::BSFixedString("retired.dds");
		};
		f.Lookup(0, 2);
		Require(releases == 1);
		f.lights.particleLightsReferences.at(f.GetKey(0)).sourceTexturePath = RE::BSFixedString("retired.dds");
		f.Lookup(0, 3);
		Require(releases == 2);
		f.lights.particleLightsReferences.at(f.GetKey(0)).sourceTexturePath = RE::BSFixedString("retired.dds");
		++globals::features::llf::particleLights.configVersion;
		f.lights.PruneParticleLightCache(4);
		Require(releases == 3);
		RequireConsistent(f.lights);
	}

	void TestRetirementCallbackMutationBetweenBatches()
	{
		Fixture f(601);
		f.Seed(600);
		f.lights.particleLightsReferences.at(f.GetKey(0)).sourceTexturePath = RE::BSFixedString("callback.dds");
		unsigned callbacks = 0;
		nameRetirement = [&](std::string_view a_name) {
			if (a_name == "callback.dds") {
				++callbacks;
				RequireUnlocked(f.lights);
				Require(f.Lookup(600, 601).valid);
				RequireConsistent(f.lights);
			}
		};
		f.lights.PruneParticleLightCache(601);
		Require(callbacks == 1);
		Require(f.lights.particleLightsReferences.contains(f.GetKey(600)));
		Require(f.lights.particleLightsReferences.size() >= 601 - kParticleLightCachePruneBudget);
		RequireConsistent(f.lights);
		for (std::uint32_t frame = 602; frame < 610; ++frame)
			f.lights.PruneParticleLightCache(frame);
		Require(f.lights.particleLightsReferences.size() == 1);
		RequireConsistent(f.lights);
	}

	void TestInsertionFailureRollback()
	{
		Fixture f;
		failNextKeyAppend = true;
		bool appendFailed = false;
		try {
			f.Lookup(0);
		} catch (const std::bad_alloc&) {
			appendFailed = true;
		}
		Require(appendFailed);
		Require(f.lights.particleLightsReferences.empty());
		RequireConsistent(f.lights);
		failNextMapInsertion = true;
		bool insertionFailed = false;
		try {
			f.Lookup(0);
		} catch (const std::bad_alloc&) {
			insertionFailed = true;
		}
		Require(insertionFailed);
		Require(f.lights.particleLightsReferences.empty());
		RequireConsistent(f.lights);
		f.Seed(4);
		failNextMapInsertion = true;
		try {
			f.Lookup(4);
		} catch (const std::bad_alloc&) {
			insertionFailed = true;
		}
		Require(f.lights.particleLightsReferences.size() == 4);
		RequireConsistent(f.lights);
		Require(f.Lookup(4).valid);
		Require(f.lights.particleLightsReferences.size() == 5);
		RequireConsistent(f.lights);
	}

	void TestCapacityReclamation()
	{
		Fixture f(kParticleLightCacheMaxEntries + 1);
		f.Seed(kParticleLightCacheMaxEntries);
		Require(f.Lookup(kParticleLightCacheMaxEntries).valid);
		Require(f.lights.particleLightsReferences.size() == kParticleLightCacheMaxEntries);
		Require(!f.lights.particleLightsReferences.contains(f.GetKey(kParticleLightCacheMaxEntries)));
		f.lights.PruneParticleLightCache(601);
		Require(f.lights.particleLightsReferences.size() == kParticleLightCacheMaxEntries - kParticleLightCachePruneBudget);
		Require(f.Lookup(kParticleLightCacheMaxEntries, 601).valid);
		Require(f.lights.particleLightsReferences.contains(f.GetKey(kParticleLightCacheMaxEntries)));
		for (std::uint32_t frame = 602; frame < 735; ++frame)
			f.lights.PruneParticleLightCache(frame);
		Require(f.lights.particleLightsReferences.size() == 1);
		RequireConsistent(f.lights);
	}

	auto MakeEmitter(std::string a_name, std::uint64_t a_sequence)
	{
		LightLimitFix::ParticleEmitterLight emitter;
		emitter.node = std::make_shared<RE::NiParticleSystem>();
		emitter.node->name = a_name + "-node";
		emitter.particleData = std::make_shared<RE::NiPSysData>();
		emitter.particleData->name = a_name + "-data";
		emitter.sequence = a_sequence;
		return emitter;
	}

	void SeedRetirement(Fixture& f)
	{
		f.lights.currentParticleEmitters.reserve(11);
		f.lights.queuedParticleEmitters.reserve(13);
		f.lights.particleLightRetirementSpare.emitters.reserve(17);
		f.lights.currentBillboardLights.reserve(7);
		f.lights.queuedBillboardLights.reserve(9);
		f.lights.particleLightRetirementSpare.billboards.reserve(19);
		f.lights.queuedEmitterIndices.rehash(23);
		f.lights.queuedBillboardIndices.rehash(29);
		f.lights.particleLightRetirementSpare.emitterIndices.rehash(31);
		f.lights.particleLightRetirementSpare.billboardIndices.rehash(37);
		f.lights.currentParticleEmitters.push_back(MakeEmitter("old", 1));
		f.lights.queuedParticleEmitters.push_back(MakeEmitter("queued", 2));
		LightLimitFix::ResolvedBillboardLight current{}, queued{};
		current.sequence = 10;
		queued.sequence = 20;
		f.lights.currentBillboardLights.push_back(current);
		f.lights.queuedBillboardLights.push_back(queued);
		f.lights.queuedEmitterIndices[f.GetKey(0)] = 0;
		f.lights.queuedBillboardIndices[f.GetKey(1)] = 0;
		f.lights.nextParticleLightSequence = 21;
	}

	void TestResetOwnershipAndStorageReuse()
	{
		Fixture f;
		SeedRetirement(f);
		const auto oldCapacity = f.lights.currentParticleEmitters.capacity();
		const auto queuedCapacity = f.lights.queuedParticleEmitters.capacity();
		const auto spareCapacity = f.lights.particleLightRetirementSpare.emitters.capacity();
		const auto oldBillboardCapacity = f.lights.currentBillboardLights.capacity();
		const auto queuedBillboardCapacity = f.lights.queuedBillboardLights.capacity();
		const auto spareBillboardCapacity = f.lights.particleLightRetirementSpare.billboards.capacity();
		const auto queuedEmitterBuckets = f.lights.queuedEmitterIndices.bucket_count();
		const auto queuedBillboardBuckets = f.lights.queuedBillboardIndices.bucket_count();
		const auto spareEmitterBuckets = f.lights.particleLightRetirementSpare.emitterIndices.bucket_count();
		const auto spareBillboardBuckets = f.lights.particleLightRetirementSpare.billboardIndices.bucket_count();
		const auto caller = std::this_thread::get_id();
		unsigned releases = 0;
		engineRetirement = [&](std::string_view a_name) {
			if (a_name.starts_with("old")) {
				++releases;
				Require(std::this_thread::get_id() == caller);
				RequireUnlocked(f.lights);
				Require(f.lights.currentParticleEmitters.size() == 1);
				Require(f.lights.currentParticleEmitters[0].sequence == 2);
				Require(f.lights.currentBillboardLights[0].sequence == 20);
				Require(f.lights.queuedEmitterIndices.empty() && f.lights.queuedBillboardIndices.empty());
				Require(f.lights.nextParticleLightSequence == 0);
				Require(f.lights.particleLightDiagnostics.currentEmitters.load() == 1);
				Require(f.lights.particleLightDiagnostics.currentBillboards.load() == 1);
			}
		};
		f.lights.Reset();
		Require(releases == 2);
		Require(f.lights.currentParticleEmitters.capacity() == queuedCapacity);
		Require(f.lights.queuedParticleEmitters.capacity() == spareCapacity);
		Require(f.lights.particleLightRetirementSpare.emitters.capacity() == oldCapacity);
		Require(f.lights.currentBillboardLights.capacity() == queuedBillboardCapacity);
		Require(f.lights.queuedBillboardLights.capacity() == spareBillboardCapacity);
		Require(f.lights.particleLightRetirementSpare.billboards.capacity() == oldBillboardCapacity);
		Require(f.lights.queuedEmitterIndices.bucket_count() == spareEmitterBuckets);
		Require(f.lights.queuedBillboardIndices.bucket_count() == spareBillboardBuckets);
		Require(f.lights.particleLightRetirementSpare.emitterIndices.bucket_count() == queuedEmitterBuckets);
		Require(f.lights.particleLightRetirementSpare.billboardIndices.bucket_count() == queuedBillboardBuckets);
		Require(f.lights.particleLightRetirementSpare.emitters.empty());
		Require(f.lights.particleLightRetirementSpare.billboards.empty());
		Require(f.lights.particleLightRetirementSpare.emitterIndices.empty());
		Require(f.lights.particleLightRetirementSpare.billboardIndices.empty());
		engineRetirement = {};
		f.lights.queuedParticleEmitters.push_back(MakeEmitter("next", 3));
		f.lights.Reset();
		Require(f.lights.currentParticleEmitters[0].sequence == 3);
		Require(f.lights.queuedParticleEmitters.capacity() == oldCapacity);
		Require(f.lights.particleLightRetirementSpare.emitters.capacity() == queuedCapacity);
		Require(f.lights.currentParticleEmitters.capacity() == spareCapacity);
		Require(f.lights.currentBillboardLights.capacity() == spareBillboardCapacity);
		Require(f.lights.queuedBillboardLights.capacity() == oldBillboardCapacity);
		Require(f.lights.particleLightRetirementSpare.billboards.capacity() == queuedBillboardCapacity);
		Require(f.lights.queuedEmitterIndices.bucket_count() == queuedEmitterBuckets);
		Require(f.lights.queuedBillboardIndices.bucket_count() == queuedBillboardBuckets);
		Require(f.lights.particleLightRetirementSpare.emitterIndices.bucket_count() == spareEmitterBuckets);
		Require(f.lights.particleLightRetirementSpare.billboardIndices.bucket_count() == spareBillboardBuckets);
	}

	void TestNestedResetAndQueueCallback()
	{
		Fixture f;
		SeedRetirement(f);
		bool nested = false;
		unsigned releases = 0;
		const auto caller = std::this_thread::get_id();
		engineRetirement = [&](std::string_view a_name) {
			++releases;
			Require(std::this_thread::get_id() == caller);
			RequireUnlocked(f.lights);
			if (a_name == "old-node") {
				Require(!nested);
				nested = true;
				f.lights.Reset();
				Require(f.lights.currentParticleEmitters.empty());
				Require(f.lights.particleLightRetirementSpare.emitters.empty());
				std::lock_guard lock(f.lights.particleLightsQueueMutex);
				f.lights.queuedParticleEmitters.push_back(MakeEmitter("callback", 4));
				f.lights.queuedEmitterIndices[f.GetKey(2)] = 0;
			}
		};
		f.lights.Reset();
		Require(nested && releases == 4);
		Require(f.lights.currentParticleEmitters.empty());
		Require(f.lights.queuedParticleEmitters.size() == 1);
		Require(f.lights.queuedParticleEmitters[0].sequence == 4);
		Require(f.lights.queuedEmitterIndices.size() == 1);
		Require(f.lights.particleLightRetirementSpare.emitters.empty());
		engineRetirement = {};
		f.lights.Reset();
		Require(f.lights.currentParticleEmitters[0].sequence == 4);
	}
}

int main()
{
	TestCursorRepairAndRehash();
	TestBoundedServiceAndFrameWrap();
	TestExpiryBeforeTouchAndInvalidation();
	TestBillboardReplacementAndFinalNameRelease();
	TestRetirementCallbackMutationBetweenBatches();
	TestInsertionFailureRollback();
	TestCapacityReclamation();
	TestResetOwnershipAndStorageReuse();
	TestNestedResetAndQueueCallback();
	return 0;
}
