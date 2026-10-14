#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <source_location>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	using uint = unsigned;

	void Require(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "Particle detection assertion failed at " << a_location.file_name() << ':' << a_location.line() << '\n';
			std::abort();
		}
	}

	thread_local std::function<void()> duringLuminance;
	struct Vector3
	{
		float x = 0, y = 0, z = 0;
		bool operator==(const Vector3&) const = default;
		Vector3 operator-(Vector3 a_other) const { return { x - a_other.x, y - a_other.y, z - a_other.z }; }
		float Length() const
		{
			if (duringLuminance) {
				auto action = std::exchange(duringLuminance, {});
				action();
			}
			return std::sqrt(x * x + y * y + z * z);
		}
		float Dot(Vector3 a_other) const { return x * a_other.x + y * a_other.y + z * a_other.z; }
	};
	using float3 = Vector3;
	namespace RE
	{
		using NiPoint3 = Vector3;
		struct ShadowSceneNode
		{};
	}
	namespace eastl
	{
		template <class T>
		using vector = std::vector<T>;
	}

	struct ShaderCache
	{
		std::atomic_bool enabled{ true };
		std::atomic<unsigned> queries{ 0 };
		bool IsEnabled()
		{
			++queries;
			return enabled.load();
		}
	};
	namespace globals
	{
		ShaderCache shader;
		ShaderCache* shaderCache = &shader;
		int stateStorage = 1;
		int* state = &stateStorage;
		namespace d3d
		{
			int contextStorage = 1;
			int* context = &contextStorage;
		}
		namespace game
		{
			struct ShaderManagerState
			{
				RE::ShadowSceneNode* shadowSceneNode[1];
			};
			RE::ShadowSceneNode scene;
			ShaderManagerState manager{ { &scene } };
			ShaderManagerState* smState = &manager;
			bool isVR = false;
		}
		namespace features
		{
			int inverseSquareLighting = 0;
		}
	}

	std::atomic<unsigned> snapshotAllocationAttempts{ 0 };
	std::atomic_bool failNextSnapshotAllocation{ false };
	template <class T>
	std::shared_ptr<T> AllocateParticleSnapshot()
	{
		++snapshotAllocationAttempts;
		if (failNextSnapshotAllocation.exchange(false))
			throw std::bad_alloc();
		return std::make_shared<T>();
	}
	namespace logger
	{
		std::atomic<unsigned> failures{ 0 };
		template <class... Args>
		void warn(Args&&...)
		{
			++failures;
		}
		template <class... Args>
		void error(Args&&...)
		{
			++failures;
		}
	}

	float fadeStart = 0;
	float fadeEnd = 0;
	namespace REL
	{
		struct RelocationID
		{
			explicit RelocationID(std::uint64_t a_se, std::uint64_t a_ae) : se(a_se)
			{
				Require((a_se == 527668 && a_ae == 414582) || (a_se == 527669 && a_ae == 414583));
			}
			std::uintptr_t address() const { return reinterpret_cast<std::uintptr_t>(se == 527668 ? &fadeStart : &fadeEnd); }
			std::uint64_t se;
		};
	}

#include "particle_light_detection_cap_under_test.h"
	static_assert(MAX_LIGHTS == 1024);

	struct LightLimitFix
	{
		struct CachedParticleLight
		{
			float grey = 0;
			Vector3 position;
			float radius = 0;
		};
		struct ParticleLightSnapshot;
#include "particle_light_detection_builder_under_test.h"
		struct LightData
		{
			Vector3 color;
			float fade = 1;
			float radius = 0;
			float invRadius = 0;
			struct Position
			{
				Vector3 data;
				bool operator==(const Position&) const = default;
			} positionWS[2];
			bool operator==(const LightData&) const = default;
		};
		struct ResolvedBillboardLight
		{
			bool flicker = false;
		};
		struct Settings
		{
			bool EnableParticleLightsDetection = true;
			float MaxParticleDistance = 0;
		} settings;
		struct Resource
		{
			bool resource = true;
			bool srv = true;
		};
		std::unique_ptr<Resource> lights = std::make_unique<Resource>();
		std::unique_ptr<Resource> lightIndexList = std::make_unique<Resource>();
		std::unique_ptr<Resource> lightGrid = std::make_unique<Resource>();
		std::unique_ptr<Resource> contactShadowIndexList = std::make_unique<Resource>();
		std::unique_ptr<Resource> contactShadowGrid = std::make_unique<Resource>();
		std::atomic<std::shared_ptr<const ParticleLightSnapshot>> cachedParticleLights;
		std::atomic_bool particleLightsDetectionEnabled{ true };
		std::atomic_bool particleLightSnapshotAllocationFailed{ false };
		uint lightCount = 0;
		int eyeCount = 1;
		std::array<Vector3, 2> eyePositionCached{};
		unsigned structureUpdates = 0;
		bool sceneSnapshotAvailable = true;
		const void* GetSceneLightSnapshot(RE::ShadowSceneNode*) { return sceneSnapshotAvailable ? this : nullptr; }
		void UpdateStructure() { ++structureUpdates; }

		void SetParticleLightDetectionEnabled(bool a_enabled);
		std::shared_ptr<ParticleLightSnapshot> CreateParticleLightSnapshot();
		void PublishParticleLights(std::shared_ptr<const ParticleLightSnapshot> a_snapshot);
		float CalculateLuminance(const CachedParticleLight& a_light, const RE::NiPoint3& a_point);
		void AddParticleLightLuminance(RE::NiPoint3& a_position, int& a_hits, float& a_level);
		float CalculateLightDistance(float3 a_position, float a_radius);
		void AddCachedParticleLights(eastl::vector<LightData>& a_output, LightData& a_light, ParticleLightSnapshotBuilder& a_detection, const ResolvedBillboardLight* a_billboard = nullptr);
		void Prepass();
		void UpdateLights();
	};

	void ApplyLegacyParticleLightFlicker(LightLimitFix::LightData&, const LightLimitFix::ResolvedBillboardLight& a_billboard, int)
	{
		Require(!a_billboard.flicker);
	}

#include "particle_light_detection_snapshot_under_test.h"

#include "particle_light_detection_methods_under_test.h"

#include "particle_light_detection_emission_under_test.h"
#define ZoneScopedN(...) static_cast<void>(0)
#include "particle_light_detection_update_guard_under_test.h"
#undef ZoneScopedN
#include "particle_light_detection_prepass_guard_under_test.h"

	using Snapshot = LightLimitFix::ParticleLightSnapshot;
	using CachedLight = LightLimitFix::CachedParticleLight;
	using LightData = LightLimitFix::LightData;

	std::shared_ptr<Snapshot> MakeSnapshot(std::initializer_list<CachedLight> a_lights)
	{
		auto result = std::make_shared<Snapshot>();
		for (const auto& light : a_lights)
			result->lights[result->count++] = light;
		return result;
	}

	struct QueryResult
	{
		int hits;
		float level;
	};
	QueryResult Query(LightLimitFix& a_fix, Vector3 a_position = {}, int a_initialHits = 0, float a_initialLevel = 0)
	{
		auto result = QueryResult{ a_initialHits, a_initialLevel };
		const auto originalPosition = a_position;
		a_fix.AddParticleLightLuminance(a_position, result.hits, result.level);
		Require(a_position == originalPosition);
		return result;
	}

	void TestLuminanceAndOrder()
	{
		LightLimitFix fix;
		fix.PublishParticleLights(MakeSnapshot({ { 2, {}, 2 } }));
		for (const auto [distance, contribution] : { std::pair{ 0.0f, 2.0f }, std::pair{ 1.0f, 1.5f }, std::pair{ 2.0f, 0.0f }, std::pair{ 3.0f, 0.0f } }) {
			const auto result = Query(fix, { distance, 0, 0 }, 7, 11);
			Require(result.level == 11 + contribution && result.hits == 7 + (contribution > 0 ? 1 : 0));
		}
		fix.PublishParticleLights(MakeSnapshot({ { std::ldexp(1.0f, 24), {}, 1 }, { 1, {}, 1 }, { 1, {}, 1 } }));
		const auto result = Query(fix);
		Require(result.level == std::ldexp(1.0f, 24) && result.hits == 3);
		fix.PublishParticleLights(MakeSnapshot({}));
		const auto empty = Query(fix, {}, 4, 9);
		Require(empty.hits == 4 && empty.level == 9);
	}

	void TestLuminanceSpecialValues()
	{
		LightLimitFix fix;
		fix.PublishParticleLights(MakeSnapshot({ { -2, {}, 1 }, { 0, {}, 1 } }));
		const auto negative = Query(fix, {}, 3, 10);
		Require(negative.level == 8 && negative.hits == 3);
		fix.PublishParticleLights(MakeSnapshot({ { 2, {}, -1 } }));
		const auto negativeRadius = Query(fix, { 1, 0, 0 });
		Require(negativeRadius.level == 2 && negativeRadius.hits == 1);
		fix.PublishParticleLights(MakeSnapshot({ { std::numeric_limits<float>::quiet_NaN(), {}, 1 } }));
		const auto nan = Query(fix, {}, 3, 10);
		Require(std::isnan(nan.level) && nan.hits == 3);
	}

	void TestDisabledAndAllocationFailure()
	{
		LightLimitFix fix;
		fix.SetParticleLightDetectionEnabled(false);
		Require(!fix.settings.EnableParticleLightsDetection && !fix.particleLightsDetectionEnabled.load());
		const auto allocations = snapshotAllocationAttempts.load();
		const auto shaderQueries = globals::shader.queries.load();
		Require(!fix.CreateParticleLightSnapshot() && snapshotAllocationAttempts.load() == allocations);
		const auto disabled = Query(fix, {}, 5, 12);
		Require(disabled.hits == 5 && disabled.level == 12 && globals::shader.queries.load() == shaderQueries);
		fix.SetParticleLightDetectionEnabled(true);
		Require(fix.settings.EnableParticleLightsDetection && fix.particleLightsDetectionEnabled.load());
		auto snapshot = fix.CreateParticleLightSnapshot();
		Require(snapshot && snapshot->count == 0 && snapshotAllocationAttempts.load() == allocations + 1);
		fix.PublishParticleLights(MakeSnapshot({ { 4, {}, 1 } }));
		globals::shader.enabled = false;
		const auto shaderDisabled = Query(fix, {}, 5, 12);
		Require(shaderDisabled.hits == 5 && shaderDisabled.level == 12);
		globals::shader.enabled = true;
		globals::shaderCache = nullptr;
		const auto missingShader = Query(fix, {}, 5, 12);
		Require(missingShader.hits == 5 && missingShader.level == 12);
		globals::shaderCache = &globals::shader;
		const auto failures = logger::failures.load();
		failNextSnapshotAllocation = true;
		Require(!fix.CreateParticleLightSnapshot() && snapshotAllocationAttempts.load() == allocations + 2);
		Require(logger::failures.load() == failures + 1 && fix.particleLightSnapshotAllocationFailed.load());
		failNextSnapshotAllocation = true;
		Require(!fix.CreateParticleLightSnapshot() && snapshotAllocationAttempts.load() == allocations + 3);
		Require(logger::failures.load() == failures + 1);
		Require(fix.CreateParticleLightSnapshot() && snapshotAllocationAttempts.load() == allocations + 4);
		Require(!fix.particleLightSnapshotAllocationFailed.load());
		failNextSnapshotAllocation = true;
		Require(!fix.CreateParticleLightSnapshot() && snapshotAllocationAttempts.load() == allocations + 5);
		Require(logger::failures.load() == failures + 2);
		fix.PublishParticleLights(nullptr);
		Require(!fix.cachedParticleLights.load());
	}

	void TestReaderOwnershipAndPublication()
	{
		LightLimitFix fix;
		std::atomic<unsigned> retired{ 0 };
		auto original = std::shared_ptr<Snapshot>(new Snapshot, [&](Snapshot* a_snapshot) {
			Require(!fix.cachedParticleLights.load() || fix.cachedParticleLights.load().get() != a_snapshot);
			++retired;
			delete a_snapshot;
		});
		original->lights[0] = { 2, {}, 2 };
		original->lights[1] = { 3, {}, 2 };
		original->count = 2;
		std::weak_ptr<const Snapshot> weakOriginal = original;
		fix.PublishParticleLights(std::move(original));
		auto next = MakeSnapshot({ { 7, {}, 2 } });
		Require(Query(fix).level == 5);
		duringLuminance = [&] {
			std::thread publisher([&] {
				fix.PublishParticleLights(std::move(next));
				Require(!weakOriginal.expired() && retired.load() == 0);
			});
			publisher.join();
		};
		const auto retained = Query(fix);
		Require(retained.level == 5 && retained.hits == 2);
		Require(weakOriginal.expired() && retired.load() == 1);
		Require(Query(fix).level == 7);

		fix.PublishParticleLights(MakeSnapshot({ { 8, {}, 2 }, { 9, {}, 2 } }));
		duringLuminance = [&] {
			std::thread publisher([&] { fix.PublishParticleLights(nullptr); });
			publisher.join();
		};
		const auto clearedReader = Query(fix);
		Require(clearedReader.level == 17 && clearedReader.hits == 2);
		const auto cleared = Query(fix, {}, 4, 13);
		Require(cleared.level == 13 && cleared.hits == 4);
	}

	LightData MakeLight(Vector3 a_position = { 1, 2, 3 }, float a_radius = 2)
	{
		LightData result;
		result.color = { 1, 2, 3 };
		result.radius = a_radius;
		result.positionWS[0].data = a_position;
		result.positionWS[1].data = { 4, 5, 6 };
		return result;
	}

	void TestRenderingAndDetectionSeparation()
	{
		LightLimitFix enabled;
		LightLimitFix disabled;
		disabled.SetParticleLightDetectionEnabled(false);
		enabled.eyeCount = disabled.eyeCount = 2;
		enabled.eyePositionCached[0] = disabled.eyePositionCached[0] = { 10, 20, 30 };
		LightLimitFix::ParticleLightSnapshotBuilder builder, disabledBuilder;
		const auto allocations = snapshotAllocationAttempts.load();
		std::vector<LightData> enabledRender;
		std::vector<LightData> disabledRender;
		auto enabledLight = MakeLight();
		auto disabledLight = enabledLight;
		enabled.AddCachedParticleLights(enabledRender, enabledLight, builder);
		disabled.AddCachedParticleLights(disabledRender, disabledLight, disabledBuilder);
		Require(builder.snapshot && !disabledBuilder.snapshot && snapshotAllocationAttempts.load() == allocations + 1);
		Require(enabledRender == disabledRender && enabledRender.size() == 1);
		Require(enabledRender[0].fade == 1 && enabledRender[0].invRadius == 0.5f);
		Require(builder.snapshot->count == 1 && builder.snapshot->lights[0].radius == 2);
		Require(std::abs(builder.snapshot->lights[0].grey - 1.81f) < 1e-6f);
		Require(builder.snapshot->lights[0].position == Vector3{ 11, 22, 33 });
		enabled.PublishParticleLights(std::move(builder.snapshot));
		const auto detection = Query(enabled, { 11, 22, 33 });
		Require(detection.hits == 1 && std::abs(detection.level - 1.81f) < 1e-6f);

		fadeStart = 0;
		fadeEnd = 16;
		LightLimitFix::ParticleLightSnapshotBuilder fadedBuilder;
		auto fadedLight = MakeLight({ 3, 0, 0 }, 1);
		fadedLight.fade = 0.5f;
		std::vector<LightData> fadedRender;
		enabled.AddCachedParticleLights(fadedRender, fadedLight, fadedBuilder);
		Require(fadedRender.size() == 1 && fadedRender[0].fade == 0.25f);
		Require(fadedBuilder.snapshot->count == 1 && std::abs(fadedBuilder.snapshot->lights[0].grey - 1.81f * 0.25f) < 1e-6f);
		fadeEnd = 0;
	}

	void TestEmissionLimitsAndRejection()
	{
		LightLimitFix fix;
		LightLimitFix::ParticleLightSnapshotBuilder builder{ fix.CreateParticleLightSnapshot(), true };
		std::vector<LightData> output(MAX_LIGHTS - 1);
		builder.snapshot->count = MAX_LIGHTS - 1;
		auto light = MakeLight();
		fix.AddCachedParticleLights(output, light, builder);
		Require(output.size() == MAX_LIGHTS && builder.snapshot->count == MAX_LIGHTS);
		const auto retained = builder.snapshot->lights[MAX_LIGHTS - 1];
		fix.AddCachedParticleLights(output, light, builder);
		Require(output.size() == MAX_LIGHTS && builder.snapshot->count == MAX_LIGHTS);
		Require(builder.snapshot->lights[MAX_LIGHTS - 1].grey == retained.grey && builder.snapshot->lights[MAX_LIGHTS - 1].position == retained.position);
		output.clear();
		fix.AddCachedParticleLights(output, light, builder);
		Require(output.size() == 1 && builder.snapshot->count == MAX_LIGHTS);

		const auto allocations = snapshotAllocationAttempts.load();
		for (const int rejection : { 0, 1, 2, 3, 4 }) {
			builder = {};
			output.clear();
			light = MakeLight();
			fix.settings.MaxParticleDistance = 0;
			if (rejection == 0)
				light.radius = 0;
			else if (rejection == 1)
				light.color = {};
			else if (rejection == 2)
				fix.settings.MaxParticleDistance = 1;
			else if (rejection == 3)
				light.fade = 0;
			else
				output.resize(MAX_LIGHTS);
			fix.AddCachedParticleLights(output, light, builder);
			Require(output.size() == (rejection == 4 ? MAX_LIGHTS : 0) && !builder.attempted && !builder.snapshot);
			Require(snapshotAllocationAttempts.load() == allocations);
		}
		fix.settings.MaxParticleDistance = 0;
		output.clear();
		builder = {};
		failNextSnapshotAllocation = true;
		for (unsigned index = 0; index < 3; ++index) {
			light = MakeLight();
			fix.AddCachedParticleLights(output, light, builder);
		}
		Require(output.size() == 3 && builder.attempted && !builder.snapshot && snapshotAllocationAttempts.load() == allocations + 1);
		fix.PublishParticleLights(std::move(builder.snapshot));
		Require(!fix.cachedParticleLights.load());
		builder = {};
		light = MakeLight();
		fix.AddCachedParticleLights(output, light, builder);
		Require(builder.snapshot && builder.snapshot->count == 1 && snapshotAllocationAttempts.load() == allocations + 2);
	}

	void RestoreRuntimeBoundaries()
	{
		globals::d3d::context = &globals::d3d::contextStorage;
		globals::state = &globals::stateStorage;
		globals::game::smState = &globals::game::manager;
		globals::game::manager.shadowSceneNode[0] = &globals::game::scene;
		globals::game::isVR = false;
	}

	void TestEarlyClearPaths()
	{
		for (const int failure : { 0, 1, 2, 3, 4, 5 }) {
			RestoreRuntimeBoundaries();
			LightLimitFix fix;
			fix.PublishParticleLights(MakeSnapshot({ { 4, {}, 2 } }));
			if (failure == 0)
				globals::d3d::context = nullptr;
			else if (failure == 1)
				fix.lights.reset();
			else if (failure == 2)
				fix.lights->resource = false;
			else if (failure == 3)
				globals::game::smState = nullptr;
			else if (failure == 4)
				globals::game::manager.shadowSceneNode[0] = nullptr;
			else {
				globals::game::isVR = true;
				fix.sceneSnapshotAvailable = false;
			}
			fix.UpdateLights();
			Require(!fix.cachedParticleLights.load());
		}
		for (const int failure : { 0, 1, 2, 3 }) {
			RestoreRuntimeBoundaries();
			LightLimitFix fix;
			fix.PublishParticleLights(MakeSnapshot({ { 4, {}, 2 } }));
			if (failure == 0)
				globals::d3d::context = nullptr;
			else if (failure == 1)
				globals::state = nullptr;
			else if (failure == 2)
				fix.lightGrid.reset();
			else
				fix.contactShadowGrid->srv = false;
			fix.Prepass();
			Require(!fix.cachedParticleLights.load());
		}
		RestoreRuntimeBoundaries();
	}
}

int main()
{
	TestLuminanceAndOrder();
	TestLuminanceSpecialValues();
	TestDisabledAndAllocationFailure();
	TestReaderOwnershipAndPublication();
	TestRenderingAndDetectionSeparation();
	TestEmissionLimitsAndRejection();
	TestEarlyClearPaths();
	std::cout << "Immutable particle detection ownership, luminance, rendering and clear paths passed\n";
}
