#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <utility>
#include <vector>

namespace
{
	using uint = unsigned;

	void Require(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "Particle clustering assertion failed at " << a_location.file_name() << ':' << a_location.line() << '\n';
			std::abort();
		}
	}

	struct Vector3
	{
		float x = 0, y = 0, z = 0;
		bool operator==(const Vector3&) const = default;
		Vector3 operator-(Vector3 a_other) const { return { x - a_other.x, y - a_other.y, z - a_other.z }; }
		Vector3 operator/(float a_scale) const { return { x / a_scale, y / a_scale, z / a_scale }; }
		Vector3 operator*(float a_scale) const { return { x * a_scale, y * a_scale, z * a_scale }; }
		Vector3& operator/=(float a_scale) { return *this = *this / a_scale; }
		Vector3& operator+=(Vector3 a_other)
		{
			x += a_other.x;
			y += a_other.y;
			z += a_other.z;
			return *this;
		}
		float GetDistance(Vector3 a_other) const
		{
			const auto difference = *this - a_other;
			return std::sqrt(difference.x * difference.x + difference.y * difference.y + difference.z * difference.z);
		}
	};
	using float3 = Vector3;

	namespace RE
	{
		using NiPoint3 = Vector3;
		struct NiColorA
		{
			float red = 1, green = 1, blue = 1, alpha = 1;
		};
		struct NiParticleSystem
		{
			struct RuntimeData
			{
				bool isWorldspace = true;
			} runtime;
			struct Bound
			{
				float radius = 1;
				Vector3 center;
			} worldBound;
			struct ModelData
			{
				Bound modelBound;
			} model;
			struct World
			{
				float scale = 1;
				Vector3 translate;
			} world;
			RuntimeData& GetParticleSystemRuntimeData() { return runtime; }
			unsigned modelReads = 0;
			ModelData& GetModelData()
			{
				++modelReads;
				return model;
			}
		};
		struct NiPSysData
		{
			struct RuntimeData
			{
				float* radii = nullptr;
				float* sizes = nullptr;
				Vector3* positions = nullptr;
				NiColorA* color = nullptr;
				std::uint32_t maxNumVertices = 0;
				std::uint32_t numVertices = 0;
			} runtime;
			std::vector<float> radii;
			std::vector<float> sizes;
			std::vector<Vector3> positions;
			std::vector<NiColorA> colors;
			std::optional<std::uint32_t> activeCount;
			std::uint32_t GetActiveVertexCount() const { return activeCount.value_or(static_cast<std::uint32_t>(radii.size())); }
			RuntimeData& GetParticlesRuntimeData()
			{
				const auto allocatedCount = static_cast<std::uint32_t>(radii.size());
				runtime = { radii.data(), sizes.data(), positions.data(), colors.empty() ? nullptr : colors.data(), allocatedCount, allocatedCount };
				return runtime;
			}
		};
	}

	enum class LightFlags : unsigned
	{
		Simple = 1,
		Particle = 2,
		AffectWater = 4
	};
	struct Flags
	{
		void set(LightFlags a_flag) { value |= static_cast<unsigned>(a_flag); }
		unsigned value = 0;
	};
	struct LightData
	{
		Vector3 color;
		float radius = 0;
		struct Position
		{
			Vector3 data;
		} positionWS[2];
		Flags lightFlags;
	};
	struct ParticleEmitterLight
	{
		std::shared_ptr<RE::NiParticleSystem> node;
		std::shared_ptr<RE::NiPSysData> particleData;
		RE::NiColorA color;
		float radiusMult = 1;
		float saturationMult = 1;
		std::uint64_t sequence = 0;
	};
	struct ResolvedBillboardLight
	{
		Vector3 position;
		float radius = 0;
		Vector3 color;
		std::uint64_t sequence = 0;
	};

#include "particle_light_cap_under_test.h"
	static_assert(MAX_LIGHTS == 1024);

	float ResolveParticleSaturation(float a_global, float a_config) { return a_global * a_config; }
	bool IsParticleEmitterBeyondDistance(RE::NiParticleSystem*, Vector3, float a_maxDistance)
	{
		Require(a_maxDistance == 0);
		return false;
	}

	namespace eastl
	{
		template <class T>
		using vector = std::vector<T>;
	}
	namespace SKSE::stl
	{
		template <class Callback>
		struct scope_exit
		{
			explicit scope_exit(Callback a_callback) : callback(std::move(a_callback)) {}
			~scope_exit() { callback(); }
			Callback callback;
		};
	}

	void WithReusableLightData(const std::function<void(std::vector<LightData>&)>& a_action)
	{
#include "particle_light_scratch_under_test.h"
		a_action(lightsData);
	}

	struct Fixture
	{
		struct Settings
		{
			float MaxParticleDistance = 0;
			int MaxParticlesPerEmitter = 4096;
			float ParticleLightsSaturation = 1;
			float ParticleClusterThreshold = 32;
			bool EnableParticleLightsOptimization = true;
			float ParticleBrightness = 1;
			float ParticleRadius = 1;
		} settings;
		int eyeCount = 1;
		std::array<Vector3, 2> eyePositionCached{};
		std::vector<LightData> lightsData;
		std::vector<ParticleEmitterLight> currentParticleEmitters;
		std::vector<ResolvedBillboardLight> currentBillboardLights;
		std::mutex currentParticleLightsMutex;

		static Vector3 Saturation(Vector3 a_color, float a_saturation)
		{
			Require(a_saturation == 1);
			return a_color;
		}
		void SetLightPosition(LightData& a_light, Vector3 a_position) const
		{
			a_light.positionWS[0].data = a_position - eyePositionCached[0];
			a_light.positionWS[1].data = a_position - eyePositionCached[eyeCount == 2 ? 1 : 0];
		}
		void AddCachedParticleLights(std::vector<LightData>& a_output, LightData& a_light, const ResolvedBillboardLight* = nullptr)
		{
			// Conditions use unfaded lights; retain the production capacity and positive-light gates.
			if (a_output.size() < MAX_LIGHTS && a_light.radius > 1e-4f &&
				(a_light.color.x + a_light.color.y + a_light.color.z) > 1e-4f) {
				a_output.push_back(a_light);
			}
		}
		void Run()
		{
#include "particle_light_clustering_under_test.h"
		}
	};

	ParticleEmitterLight Emitter(std::initializer_list<float> a_radii, float a_multiplier = 1, std::uint64_t a_sequence = 0)
	{
		ParticleEmitterLight emitter;
		emitter.node = std::make_shared<RE::NiParticleSystem>();
		emitter.particleData = std::make_shared<RE::NiPSysData>();
		emitter.particleData->radii = a_radii;
		emitter.particleData->sizes.assign(a_radii.size(), 1);
		emitter.particleData->positions.resize(a_radii.size());
		emitter.radiusMult = a_multiplier;
		emitter.sequence = a_sequence;
		return emitter;
	}

	void CheckLight(const LightData& a_light, float a_radius, Vector3 a_color, Vector3 a_position = {})
	{
		Require(a_light.radius == a_radius && std::isfinite(a_light.radius));
		Require(a_light.color == a_color && a_light.positionWS[0].data == a_position);
		Require(a_light.lightFlags.value == 7);
	}

	void TestEffectiveRadius()
	{
		Fixture common;
		common.currentParticleEmitters = { Emitter({ 100, 100 }, 2) };
		common.Run();
		Require(common.lightsData.size() == 1);
		CheckLight(common.lightsData[0], 200, { 2, 2, 2 });

		Fixture different;
		different.currentParticleEmitters = { Emitter({ 100 }, 1), Emitter({ 100 }, 2) };
		different.Run();
		Require(different.lightsData.size() == 2);
		CheckLight(different.lightsData[0], 100, { 1, 1, 1 });
		CheckLight(different.lightsData[1], 200, { 1, 1, 1 });

		Fixture equal;
		equal.currentParticleEmitters = { Emitter({ 100 }, 2), Emitter({ 200 }, 1) };
		equal.Run();
		Require(equal.lightsData.size() == 1);
		CheckLight(equal.lightsData[0], 200, { 2, 2, 2 });

		Fixture global;
		global.settings.ParticleRadius = 3;
		global.currentParticleEmitters = { Emitter({ 50, 50 }, 2) };
		global.Run();
		Require(global.lightsData.size() == 1);
		CheckLight(global.lightsData[0], 300, { 2, 2, 2 });
	}

	void TestParticleSizeScaling()
	{
		Fixture fixture;
		fixture.currentParticleEmitters = { Emitter({ 10, 20 }, 2) };
		fixture.currentParticleEmitters[0].particleData->sizes = { 2, 1 };
		fixture.Run();
		Require(fixture.lightsData.size() == 1);
		CheckLight(fixture.lightsData[0], 40, { 2, 2, 2 });
	}

	void TestThresholds()
	{
		for (const float difference : { 32.0f, 32.25f }) {
			Fixture radius;
			radius.currentParticleEmitters = { Emitter({ 100, 100 + difference }) };
			radius.Run();
			Require(radius.lightsData.size() == (difference == 32 ? 1u : 2u));

			Fixture combined;
			combined.currentParticleEmitters = { Emitter({ 100, 116 }) };
			combined.currentParticleEmitters[0].particleData->positions[1].x = difference - 16;
			combined.Run();
			Require(combined.lightsData.size() == (difference == 32 ? 1u : 2u));
		}
	}

	void TestAveragingAndStereo()
	{
		for (const int eyes : { 1, 2 }) {
			Fixture fixture;
			fixture.eyeCount = eyes;
			fixture.eyePositionCached = { Vector3{ 10, 20, 30 }, Vector3{ 12, 21, 33 } };
			fixture.currentParticleEmitters = { Emitter({ 10, 20 }, 2) };
			auto& emitter = fixture.currentParticleEmitters[0];
			emitter.particleData->sizes = { 1, 1 };
			emitter.particleData->positions = { { 12, 22, 32 }, { 16, 24, 34 } };
			emitter.color = { 0.5f, 0.25f, 1, 0.5f };
			fixture.settings.ParticleBrightness = 2;
			emitter.particleData->colors = { { 1, 0, 0, 0.5f }, { 0, 1, 0, 1 } };
			fixture.Run();
			Require(fixture.lightsData.size() == 1);
			CheckLight(fixture.lightsData[0], 30, { 0.25f, 0.25f, 0 }, { 4, 3, 3 });
			Require(fixture.lightsData[0].positionWS[1].data == (eyes == 2 ? Vector3{ 2, 2, 0 } : Vector3{ 4, 3, 3 }));
		}

		Fixture disabled;
		disabled.settings.EnableParticleLightsOptimization = false;
		disabled.currentParticleEmitters = { Emitter({ 10, 10, 10 }, 2) };
		disabled.Run();
		Require(disabled.lightsData.size() == 3);
		for (const auto& light : disabled.lightsData)
			CheckLight(light, 20, { 1, 1, 1 });
	}

	void TestEmitterCoordinates()
	{
		enum class Coordinates
		{
			Worldspace,
			Local,
			FirstPerson
		};
		const float nan = std::numeric_limits<float>::quiet_NaN();
		for (const int eyes : { 1, 2 }) {
			for (const auto coordinates : { Coordinates::Worldspace, Coordinates::Local, Coordinates::FirstPerson }) {
				Fixture fixture;
				fixture.eyeCount = eyes;
				fixture.eyePositionCached = { Vector3{ 1, 1, 1 }, Vector3{ 2, 3, 4 } };
				fixture.currentParticleEmitters = { Emitter({ 10, 10, 10 }) };
				auto& emitter = fixture.currentParticleEmitters[0];
				emitter.particleData->positions = { { 1, 2, 3 }, { 3, 4, 5 }, { 5, 6, 7 } };
				auto& node = *emitter.node;
				node.runtime.isWorldspace = coordinates == Coordinates::Worldspace;
				node.model.modelBound.radius = 2;
				node.world.scale = 3;
				node.worldBound.radius = coordinates == Coordinates::FirstPerson ? 7.0f : 6.0f;
				node.worldBound.center = { nan, nan, nan };
				node.world.translate = { nan, nan, nan };
				Vector3 translation;
				if (coordinates == Coordinates::Local) {
					translation = { 10, 20, 30 };
					node.world.translate = translation;
				} else if (coordinates == Coordinates::FirstPerson) {
					translation = { 20, 30, 40 };
					node.worldBound.center = translation;
				}
				fixture.Run();
				Require(fixture.lightsData.size() == 1);
				Require(node.modelReads == (coordinates == Coordinates::Worldspace ? 0u : 1u));
				const Vector3 position{ 2 + translation.x, 3 + translation.y, 4 + translation.z };
				CheckLight(fixture.lightsData[0], 10, { 3, 3, 3 }, position);
				Require(fixture.lightsData[0].positionWS[1].data == (eyes == 2 ? position - Vector3{ 1, 2, 3 } : position));
			}
		}

		Fixture inactive;
		inactive.currentParticleEmitters = { Emitter({ 10 }) };
		inactive.currentParticleEmitters[0].node->runtime.isWorldspace = false;
		inactive.currentParticleEmitters[0].particleData->activeCount = 0;
		inactive.Run();
		Require(inactive.lightsData.empty() && inactive.currentParticleEmitters[0].node->modelReads == 0);
	}

	void TestTranslationRoundingOrder()
	{
		const float large = std::ldexp(1.0f, 24);
		for (const bool firstPerson : { false, true }) {
			Fixture fixture;
			fixture.eyePositionCached[0] = { large, 0, 0 };
			fixture.currentParticleEmitters = { Emitter({ 10, 10, 10 }) };
			auto& emitter = fixture.currentParticleEmitters[0];
			emitter.particleData->positions.assign(3, { 1, 0, 0 });
			emitter.node->runtime.isWorldspace = false;
			emitter.node->worldBound.radius = firstPerson ? 2.0f : 1.0f;
			emitter.node->world.translate = { firstPerson ? 0.0f : large, 0, 0 };
			emitter.node->worldBound.center = { firstPerson ? large : 0.0f, 0, 0 };
			fixture.Run();
			Require(fixture.lightsData.size() == 1 && emitter.node->modelReads == 1);
			CheckLight(fixture.lightsData[0], 10, { 3, 3, 3 });
		}
	}

	void TestInvalidEffectiveRadius()
	{
		for (const float multiplier : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max() }) {
			Fixture fixture;
			fixture.currentParticleEmitters = { Emitter({ 10 }), Emitter({ 20 }, multiplier), Emitter({ 12 }) };
			fixture.currentParticleEmitters[0].color = { 1, 0, 0, 1 };
			fixture.currentParticleEmitters[1].color = { 0, 0, 1, 1 };
			fixture.currentParticleEmitters[2].color = { 0, 1, 0, 1 };
			fixture.currentParticleEmitters[2].particleData->positions[0].x = 2;
			fixture.Run();
			Require(fixture.lightsData.size() == 1);
			CheckLight(fixture.lightsData[0], 11, { 1, 1, 0 }, { 1, 0, 0 });
		}
	}

	void TestRadiusSumOverflow()
	{
		for (const float radius : { 1e38f, std::ldexp(1.0f, 126) }) {
			Fixture fixture;
			fixture.currentParticleEmitters = { Emitter({ 1, 1, 1, 1 }, radius) };
			fixture.Run();
			Require(fixture.lightsData.size() == 2);
			Vector3 totalColor;
			for (const auto& light : fixture.lightsData) {
				Require(std::isfinite(light.radius) && light.radius > 0);
				totalColor += light.color;
			}
			Require(totalColor == Vector3{ 4, 4, 4 });
			// Binary radii keep the running average exact, isolating the sum overflow guard.
			if (radius == std::ldexp(1.0f, 126)) {
				CheckLight(fixture.lightsData[0], radius, { 3, 3, 3 });
				CheckLight(fixture.lightsData[1], radius, { 1, 1, 1 });
			}
		}
	}

	void TestScratchReuse()
	{
		LightData* storage = nullptr;
		std::size_t capacity = 0;
		WithReusableLightData([&](auto& a_lights) {
			Require(a_lights.empty() && a_lights.capacity() >= MAX_LIGHTS);
			storage = a_lights.data();
			capacity = a_lights.capacity();
			a_lights.resize(3);
			a_lights[0].radius = 101;
		});
		WithReusableLightData([&](auto& a_lights) {
			Require(a_lights.empty() && a_lights.data() == storage && a_lights.capacity() == capacity);
			a_lights.emplace_back();
			a_lights[0].radius = 102;
			Require(a_lights.size() == 1 && a_lights[0].radius == 102);
		});
		WithReusableLightData([&](auto& a_lights) {
			Require(a_lights.empty() && a_lights.data() == storage && a_lights.capacity() == capacity);
		});
	}

	void TestNestedScratchReuse()
	{
		LightData* outerStorage = nullptr;
		WithReusableLightData([&](auto& a_outer) {
			Require(a_outer.empty());
			outerStorage = a_outer.data();
			a_outer.emplace_back();
			a_outer[0].radius = 11;
			LightData* innerStorage = nullptr;
			for (const float radius : { 21.0f, 22.0f }) {
				WithReusableLightData([&](auto& a_inner) {
					Require(a_inner.empty() && a_inner.data() != outerStorage);
					if (innerStorage)
						Require(a_inner.data() == innerStorage);
					innerStorage = a_inner.data();
					a_inner.emplace_back();
					a_inner[0].radius = radius;
				});
				Require(a_outer.data() == outerStorage && a_outer.size() == 1 && a_outer[0].radius == 11);
			}
		});
		WithReusableLightData([&](auto& a_lights) { Require(a_lights.empty() && a_lights.data() == outerStorage); });
	}

	void TestScratchExceptionRecovery()
	{
		struct ExpectedFailure
		{};
		LightData* outerStorage = nullptr;
		LightData* innerStorage = nullptr;
		bool innerCaught = false;
		bool outerCaught = false;
		try {
			WithReusableLightData([&](auto& a_outer) {
				Require(a_outer.empty());
				outerStorage = a_outer.data();
				a_outer.emplace_back();
				a_outer[0].radius = 31;
				try {
					WithReusableLightData([&](auto& a_inner) {
						Require(a_inner.empty() && a_inner.data() != outerStorage);
						innerStorage = a_inner.data();
						a_inner.emplace_back();
						a_inner[0].radius = 41;
						throw ExpectedFailure{};
					});
				} catch (const ExpectedFailure&) {
					innerCaught = true;
				}
				Require(innerCaught && a_outer.data() == outerStorage && a_outer.size() == 1 && a_outer[0].radius == 31);
				WithReusableLightData([&](auto& a_inner) { Require(a_inner.empty() && a_inner.data() == innerStorage); });
				throw ExpectedFailure{};
			});
		} catch (const ExpectedFailure&) {
			outerCaught = true;
		}
		Require(outerCaught);
		WithReusableLightData([&](auto& a_lights) { Require(a_lights.empty() && a_lights.data() == outerStorage); });
	}

	void TestBillboardOrderAndBudget()
	{
		Fixture ordered;
		ordered.currentParticleEmitters = { Emitter({ 10 }, 1, 1), Emitter({ 10 }, 1, 3) };
		ordered.currentBillboardLights = { { {}, 7, { 0, 1, 0 }, 2 } };
		ordered.Run();
		Require(ordered.lightsData.size() == 3);
		CheckLight(ordered.lightsData[0], 10, { 1, 1, 1 });
		CheckLight(ordered.lightsData[1], 7, { 0, 1, 0 });
		CheckLight(ordered.lightsData[2], 10, { 1, 1, 1 });

		for (const std::uint64_t sequence : { 1u, 2u, 3u }) {
			const bool emitterFirst = sequence <= 2;
			Fixture budget;
			budget.lightsData.resize(MAX_LIGHTS - 1);
			budget.currentParticleEmitters = { Emitter({ 10, 10 }, 2, sequence) };
			budget.currentBillboardLights = { { {}, 7, { 0, 1, 0 }, 2 } };
			budget.Run();
			Require(budget.lightsData.size() == MAX_LIGHTS);
			CheckLight(budget.lightsData.back(), emitterFirst ? 20.0f : 7.0f, emitterFirst ? Vector3{ 2, 2, 2 } : Vector3{ 0, 1, 0 });
		}

		Fixture splitBudget;
		splitBudget.lightsData.resize(MAX_LIGHTS - 1);
		splitBudget.currentParticleEmitters = { Emitter({ 100, 200 }) };
		splitBudget.Run();
		Require(splitBudget.lightsData.size() == MAX_LIGHTS);
		CheckLight(splitBudget.lightsData.back(), 100, { 1, 1, 1 });
	}
}

int main()
{
	TestEffectiveRadius();
	TestParticleSizeScaling();
	TestThresholds();
	TestAveragingAndStereo();
	TestEmitterCoordinates();
	TestTranslationRoundingOrder();
	TestInvalidEffectiveRadius();
	TestRadiusSumOverflow();
	TestScratchReuse();
	TestNestedScratchReuse();
	TestScratchExceptionRecovery();
	TestBillboardOrderAndBudget();
	std::cout << "Particle effective-radius clustering, validity, averaging, stereo and budget order passed\n";
}
