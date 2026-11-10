#include "EngineFixes/VRShadowBatchPolicy.h"
#include "Features/LightLimitFix/ActorShadowHistory.h"
#include "Features/LightLimitFix/ShadowCullingDrawScope.h"
#include "Features/LightLimitFix/ShadowCullingPolicy.h"
#include "Features/LightLimitFix/ShadowCullingSnapshot.h"

#include <array>
#include <atomic>
#include <iostream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <thread>
#include <utility>

namespace RE
{
	template <class T>
	struct NiPointer
	{
		T* value = nullptr;
		T* get() const { return value; }
		T* operator->() const { return value; }
		explicit operator bool() const { return value != nullptr; }
	};
	struct NiPoint3
	{
		float x = 0, y = 0, z = 0;
		bool operator==(const NiPoint3&) const = default;
	};
	struct NiAVObject
	{
		struct
		{
			NiPoint3 translate;
		} world;
	};
	struct NiCamera
	{};
	struct ShadowSceneNode
	{};
	struct TESObjectREFR
	{
		NiPoint3 position;
		struct Handle
		{
			std::uint32_t value = 1;
			std::uint32_t native_handle() const { return value; }
		} handle;
		NiPoint3 GetPosition() const { return position; }
		Handle GetHandle() const { return handle; }
	};
	struct NiLight
	{
		struct Data
		{
			NiPoint3 radius;
		} data;
		const Data& GetLightRuntimeData() const { return data; }
	};
	class BSShadowLight
	{
	public:
		NiPointer<NiLight> light;
		NiPoint3 worldTranslate;
	};
	struct BSGeometry
	{
		struct
		{
			NiPoint3 center;
			float radius;
		} worldBound;
		bool actor = false, dynamic = false, protectedGeometry = false;
		NiAVObject* actorRoot = nullptr;
		unsigned lightAssignments = 0;
		TESObjectREFR* actorReference = nullptr;
		struct Data
		{
			void* skinInstance = nullptr;
		} data;
		const Data& GetGeometryRuntimeData() const { return data; }
	};
	struct BSCullingProcess
	{
		const NiCamera* camera = nullptr;
	};
	struct BSShader
	{
		enum class Type
		{
			Lighting,
			Utility
		};
		struct TypeValue
		{
			Type value = Type::Utility;
			mutable unsigned reads = 0;
			Type get() const
			{
				++reads;
				return value;
			}
		} shaderType;
	};
	class BSRenderPass
	{
	public:
		BSShader* shader = nullptr;
		BSGeometry* geometry = nullptr;
		std::uint8_t numLights = 0, numShadowLights = 0;
	};
}
namespace globals
{
	struct State
	{
		bool loading = false;
		bool IsSaveLoadSafeModeActive() const { return loading; }
	} testState;
	auto* state = &testState;
}
std::uint64_t clockMs = 1000;
std::uint64_t GetTickCount64() { return clockMs; }

namespace LightLimitFixDetail::ShadowCulling
{
#include "shadow_culling_state.h"
	Caster Classify(const RE::BSGeometry& geometry)
	{
		return { geometry.actor, geometry.dynamic, geometry.protectedGeometry, geometry.actorRoot, geometry.actor ? geometry.actorReference : nullptr };
	}
#include "shadow_culling_collection.h"
#include "shadow_culling_draw.h"
}
namespace C = LightLimitFixDetail::ShadowCulling;

void Require(bool condition, std::source_location location = std::source_location::current())
{
	if (!condition)
		throw std::runtime_error("Shadow draw regression at line " + std::to_string(location.line()));
}

void CheckActorPartHistory()
{
	C::ActorShadowHistory history(C::ActorPartDistances({}));
	std::array<std::array<double, 3>, 2> eyes{};
	const auto beyond = [&](std::uint32_t handle, double distance, std::uint64_t time = 1000) {
		return history.Beyond(handle, { distance + C::EyeDistanceMargin, 0, 0 }, eyes, 2, time, 0);
	};
	Require(!beyond(1, 475));
	Require(beyond(1, 501));
	Require(beyond(1, 475));
	Require(!beyond(1, 450));
	Require(!beyond(1, 475));
	Require(beyond(1, 501));
	Require(!beyond(2, 475));
	Require(!beyond(513, 475));
	Require(!beyond(513, 501));
	Require(beyond(1, 475));
	Require(beyond(1, 501));
	Require(!beyond(1, 475, 1251));
	Require(beyond(1, 501, 1252));
	Require(!beyond(1, 475, 1251));
	Require(!beyond(0, 10000));
	Require(!beyond(1, std::numeric_limits<double>::quiet_NaN(), 1253));
	Require(!beyond(1, 475, 1254));
	eyes[1] = { 1500, 0, 0 };
	Require(!beyond(1, 1000, 1255));
	eyes = {};
	Require(!history.Beyond(1, { 2000, 0, 0 }, eyes, 0, 1256, 0));
	Require(!history.Beyond(1, { 2000, 0, 0 }, eyes, 3, 1257, 0));

	// Coarse clock ticks cannot order two frames with different eye positions.
	C::ActorShadowHistory ordered(C::ActorPartDistances({}));
	Require(ordered.Beyond(1, { 2000, 0, 0 }, eyes, 2, 1300, 10));
	eyes[1] = { 1500, 0, 0 };
	Require(!ordered.Beyond(1, { 2000, 0, 0 }, eyes, 2, 1300, 11));
	eyes = {};
	Require(!ordered.Beyond(1, { 2000, 0, 0 }, eyes, 2, 1300, 10));
	Require(!ordered.Beyond(1, { 987, 0, 0 }, eyes, 2, 1300, 11));
	Require(!ordered.Beyond(513, { 2000, 0, 0 }, eyes, 2, 1550, 12));
	Require(ordered.Beyond(513, { 2000, 0, 0 }, eyes, 2, 1551, 13));
	Require(!ordered.Beyond(1, { 2000, 0, 0 }, eyes, 2, 1551, 13));

	std::atomic_bool allCorrect{ true };
	std::array<std::thread, 4> workers;
	for (auto& worker : workers)
		worker = std::thread([&] {
			for (unsigned i = 0; i < 1000; ++i)
				if (!beyond(1, 1000, 1300))
					allCorrect = false;
		});
	for (auto& worker : workers)
		worker.join();
	Require(allCorrect);

	{
		const auto member = &C::Snapshot::actorPartHistory;
		RE::ShadowSceneNode scene, otherScene;
		auto previous = std::make_shared<C::Snapshot>();
		previous->settings.smallActorParts = true;
		previous->scene.value = &scene;
		previous->sampledAt = 1000;
		previous->frame = 20;
		previous.get()->*member = std::make_shared<C::ActorShadowHistory>(C::ActorPartDistances({}));
		auto next = *previous;
		Require(C::ActorPartHistoryFor(next, previous) == previous.get()->*member);
		Require(C::ActorPartHistoryFor(next, {}) != previous.get()->*member);
		next.frame += 1;
		Require(C::ActorPartHistoryFor(next, previous) == previous.get()->*member);
		next.frame = previous->frame - 1;
		Require(C::ActorPartHistoryFor(next, previous) != previous.get()->*member);
		previous->frame = UINT32_MAX;
		next.frame = 0;
		Require(C::ActorPartHistoryFor(next, previous) != previous.get()->*member);
		previous->frame = 20;
		next = *previous;
		next.sampledAt += C::SnapshotMaxAgeMs + 1;
		Require(C::ActorPartHistoryFor(next, previous) != previous.get()->*member);
		next.sampledAt = previous->sampledAt - 1;
		Require(C::ActorPartHistoryFor(next, previous) != previous.get()->*member);
		next = *previous;
		next.scene.value = &otherScene;
		Require(C::ActorPartHistoryFor(next, previous) != previous.get()->*member);
		next = *previous;
		next.settings.actorDistanceMeters += 1;
		Require(C::ActorPartHistoryFor(next, previous) != previous.get()->*member);
		for (auto distanceMember : { &C::Settings::actorPartDistanceMeters, &C::Settings::actorPartReturnDistanceMeters, &C::Settings::actorPartRadiusCentimeters }) {
			next = *previous;
			next.settings.*distanceMember += 0.1f;
			Require(C::ActorPartHistoryFor(next, previous) != previous.get()->*member);
		}
		for (auto settingMember : { &C::Settings::actorPartDistanceMeters, &C::Settings::actorPartReturnDistanceMeters, &C::Settings::actorPartRadiusCentimeters }) {
			next = *previous;
			next.settings.*settingMember = 0;
			Require(!C::ActorPartHistoryFor(next, previous));
		}
		next.settings.smallActorParts = false;
		Require(!C::ActorPartHistoryFor(next, previous));
	}

	C::ActorShadowHistory tuned(C::ActorPartDistances(C::Parse({ { "actorPartDistanceMeters", 20 }, { "actorPartReturnDistanceMeters", 10 } })));
	const auto tunedBeyond = [&](double meters) {
		return tuned.Beyond(1, { meters / C::MetersPerUnit + C::EyeDistanceMargin, 0, 0 }, eyes, 2, 2000, 1);
	};
	Require(!tunedBeyond(19));
	Require(tunedBeyond(21));
	Require(tunedBeyond(15));
	Require(!tunedBeyond(9));
	Require(!tunedBeyond(15));
}

void CheckExistingActorPolicy()
{
	RE::BSGeometry geometry{ { {}, 1 } };
	geometry.actor = true;
	C::Snapshot captured;
	captured.settings.sun = captured.settings.torch = false;
	captured.eyeCount = 2;
	captured.sunSine = 1;
	C::Camera camera;
	camera.pass = C::Pass::Sun;
	for (const auto [distance, culled] : { std::pair{ 47., false }, { 51., true }, { 47., false } }) {
		geometry.worldBound.center.x = static_cast<float>(distance / C::MetersPerUnit + C::EyeDistanceMargin);
		Require(C::Skip(captured, camera, geometry) == culled);
	}
	geometry.worldBound.center.x = 10000;
	captured.settings.actors = false;
	Require(!C::Skip(captured, camera, geometry));
}

void CheckActorPartDraws()
{
	RE::NiCamera sunCamera, pointCamera;
	RE::NiLight source{ { { 1000, 0, 0 } } };
	RE::BSShadowLight light{ { &source }, {} }, sun, unknown;
	RE::TESObjectREFR actor{ { 2000, 0, 0 } };
	RE::BSGeometry geometry{ { { 2000, 0, 0 }, 4 } };
	geometry.actor = true;
	geometry.actorReference = &actor;
	geometry.data.skinInstance = &actor;
	RE::BSShader shader;
	RE::BSRenderPass pass{ &shader, &geometry };
	const auto technique = VRShadowBatch::kUtilityTechniqueStart + VRShadowBatch::kRenderShadowmap;
	C::Snapshot desired;
	desired.settings.sun = desired.settings.torch = desired.settings.actors = desired.settings.smallActorParts = false;
	desired.sunIdentity = &sun;
	desired.eyeCount = 2;
	desired.sunSine = 1;
	desired.cameraCount = 2;
	desired.cameras[0] = { { &sunCamera }, {}, {}, C::Pass::Sun };
	desired.cameras[1] = { { &pointCamera }, { &light }, { &source }, C::Pass::Point, 0, light.worldTranslate, 1000 };
	std::shared_ptr<const C::Snapshot> captured;
	const auto publish = [&] {
		auto next = std::make_shared<C::Snapshot>(desired);
		next->sampledAt = ++clockMs;
		next->frame = captured ? captured->frame + 1 : 1;
		next->actorPartHistory = C::ActorPartHistoryFor(*next, captured);
		captured = std::move(next);
		C::snapshot.Publish(captured);
	};
	const auto draw = [&](RE::BSShadowLight* owner) {
		const C::DrawScope scope{ owner == &sun && !C::NeedsSunDrawScope() ? nullptr : owner };
		return C::SkipShadowDraw(&pass, technique);
	};
	C::active = true;
	publish();
	Require(!C::NeedsSunDrawScope());
	Require(!draw(&sun) && !draw(&light));
	desired.settings.smallActorParts = true;
	publish();
	Require(C::NeedsSunDrawScope());
	Require(draw(&sun) && draw(&light));
	Require(!draw(nullptr) && !draw(&unknown));
	C::appendOriginal = [](RE::BSCullingProcess*, RE::BSGeometry& object, std::int32_t) { ++object.lightAssignments; };
	for (auto* camera : { &sunCamera, &pointCamera }) {
		RE::BSCullingProcess culler{ camera };
		C::Append(&culler, geometry, 0);
	}
	Require(geometry.lightAssignments == 2);

	// All eligible parts follow the reference position, including animation on the far side of the band.
	actor.position.x = 1013;
	Require(draw(&sun));
	actor.position.x = 990;
	geometry.worldBound.center.x = 2100;
	Require(draw(&light));
	actor.position.x = 961;
	Require(!draw(&sun));
	actor.position.x = 990;
	Require(!draw(&light));
	actor.position.x = 2000;
	Require(draw(&sun));
	actor.position.x = 990;
	actor.handle.value = 2;
	Require(!draw(&sun));
	actor.position.x = 2000;

	// A delayed draw from the same clock tick cannot re-arm a newer frame's return band.
	const auto delayed = captured;
	auto newer = std::make_shared<C::Snapshot>(*captured);
	++newer->frame;
	captured = std::move(newer);
	C::snapshot.Publish(captured);
	actor.position.x = 961;
	Require(!draw(&sun));
	actor.position.x = 2000;
	Require(!C::Skip(*delayed, delayed->cameras[0], geometry, true));
	actor.position.x = 990;
	Require(!draw(&sun));
	actor.position.x = 2000;

	// A settings change must reach the draw policy and discard the previous return band.
	desired.settings.actorPartDistanceMeters = 30;
	desired.settings.actorPartReturnDistanceMeters = 5;
	publish();
	Require(!draw(&sun) && !draw(&light));
	desired.settings.actorPartDistanceMeters = 10;
	publish();
	Require(draw(&sun) && draw(&light));
	desired.settings.actorPartReturnDistanceMeters = 10;
	actor.position.x = 1100;
	publish();
	Require(!draw(&sun) && !draw(&light));
	desired.settings.actorPartDistanceMeters = C::DefaultActorPartDistanceMeters;
	desired.settings.actorPartReturnDistanceMeters = C::DefaultActorPartReturnDistanceMeters;
	actor.position.x = 2000;
	publish();

	for (auto member : { &C::Settings::actorPartDistanceMeters, &C::Settings::actorPartReturnDistanceMeters, &C::Settings::actorPartRadiusCentimeters }) {
		const auto original = desired.settings.*member;
		desired.settings.*member = 0;
		publish();
		Require(!C::NeedsSunDrawScope() && !draw(&sun) && !draw(&light));
		Require(!captured->actorPartHistory);
		desired.settings.*member = original;
		publish();
		Require(C::NeedsSunDrawScope() && draw(&sun) && draw(&light));
	}
	desired.settings.actorPartRadiusCentimeters = 5;
	publish();
	Require(!draw(&sun) && !draw(&light));
	desired.settings.actorPartRadiusCentimeters = 20;
	geometry.worldBound.radius = 8;
	publish();
	Require(draw(&sun) && draw(&light));
	desired.settings.actorPartRadiusCentimeters = C::DefaultActorPartRadiusCentimeters;
	geometry.worldBound.radius = 4;
	publish();

	for (auto* owner : { &sun, &light }) {
		geometry.actor = false;
		Require(!draw(owner));
		geometry.actor = true;
		geometry.data.skinInstance = nullptr;
		Require(!draw(owner));
		geometry.data.skinInstance = &actor;
		geometry.protectedGeometry = true;
		Require(!draw(owner));
		geometry.protectedGeometry = false;
		geometry.worldBound.radius = 8;
		Require(!draw(owner));
		geometry.worldBound.radius = std::numeric_limits<float>::quiet_NaN();
		Require(!draw(owner));
		geometry.worldBound.radius = 4;
		shader.shaderType.value = RE::BSShader::Type::Lighting;
		Require(!draw(owner));
		shader.shaderType.value = RE::BSShader::Type::Utility;
		pass.numLights = 1;
		Require(!draw(owner));
		pass.numLights = 0;
		Require(draw(owner));
	}
	desired.eyes[1] = { 1950, 0, 0 };
	publish();
	Require(!draw(&sun) && !draw(&light));
	desired.eyes = {};
	desired.sunSine = 0.2;
	publish();
	Require(!draw(&sun));
	desired.sunSine = 1;
	desired.interior = true;
	desired.settings.interiorTorches = false;
	publish();
	Require(!draw(&light) && draw(&sun));
	desired.settings.interiorTorches = true;
	desired.cameras[1].lightRadius = source.data.radius.x = 10000;
	publish();
	Require(!draw(&light));
	desired.cameras[1].lightRadius = source.data.radius.x = 1000;
	desired.cameras[1].lightPosition = light.worldTranslate = { 2090, 0, 0 };
	publish();
	Require(!draw(&light));
	light.worldTranslate.x += 1;
	Require(!draw(&light));
	globals::state->loading = true;
	Require(!draw(&sun));
	globals::state->loading = false;
	clockMs += C::SnapshotMaxAgeMs + 1;
	Require(!draw(&sun));
#ifdef DEVBENCH_BRIDGE_ENABLED
	Require(C::actorPartDrawsSkipped.load() > 0);
#endif
	C::snapshot.Publish({});
	C::active = false;
}

int main()
{
	try {
		CheckActorPartHistory();
		CheckExistingActorPolicy();
		CheckActorPartDraws();
		RE::NiCamera camera;
		RE::NiLight source{ { { 10000, 0, 0 } } };
		RE::BSShadowLight light{ { &source }, { 1000, 0, 0 } }, otherLight;
		RE::BSGeometry geometry{ { { 10000, 0, 0 }, 1 } };
		RE::TESObjectREFR actorReference{ { 10000, 0, 0 } };
		geometry.actorReference = &actorReference;
		RE::BSShader shader;
		RE::BSRenderPass pass{ &shader, &geometry };
		RE::BSCullingProcess culler{ &camera };
		const auto shadowTechnique = VRShadowBatch::kUtilityTechniqueStart + VRShadowBatch::kRenderShadowmap;
		auto captured = std::make_shared<C::Snapshot>();
		captured->sampledAt = clockMs;
		captured->eyeCount = 2;
		captured->cameraCount = 1;
		captured->sunSine = 1;
		captured->cameras[0] = { { &camera }, { &light }, { &source }, C::Pass::Point, 1, light.worldTranslate, 10000 };
		C::snapshot.Publish(captured);
		C::active = true;
		C::appendOriginal = [](RE::BSCullingProcess*, RE::BSGeometry& object, std::int32_t group) {
			Require(group == 3);
			++object.lightAssignments;
		};
		const auto requireNoShaderRead = [&] {
			const auto reads = shader.shaderType.reads;
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
			Require(shader.shaderType.reads == reads);
		};

		// Both ordinary and actor casters keep native light assignment even when their shadow is culled.
		for (const bool actor : { false, true }) {
			geometry.actor = actor;
			Require(C::Skip(*captured, captured->cameras[0], geometry));
			const auto assignments = geometry.lightAssignments;
			C::Append(&culler, geometry, 3);
			Require(geometry.lightAssignments == assignments + 1);
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
			{
				const C::DrawScope scope{ &light };
				Require(C::SkipShadowDraw(&pass, shadowTechnique));
			}
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
		}
		geometry.actor = false;
		{
			const C::DrawScope scope{ &light };
			shader.shaderType.value = RE::BSShader::Type::Lighting;
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
			shader.shaderType.value = RE::BSShader::Type::Utility;
			for (const auto technique : { 0u, VRShadowBatch::kUtilityTechniqueStart - 1,
					 VRShadowBatch::kUtilityTechniqueStart + (1u << 13), VRShadowBatch::kUtilityTechniqueStart + (1u << 21) })
				Require(!C::SkipShadowDraw(&pass, technique));
			pass.numLights = 1;
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
			pass.numLights = 0;
			pass.numShadowLights = 1;
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
			pass.numShadowLights = 0;
			Require(!C::SkipShadowDraw(nullptr, shadowTechnique));
			pass.geometry = nullptr;
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
			pass.geometry = &geometry;
			pass.shader = nullptr;
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
			pass.shader = &shader;

			for (auto* nestedLight : { static_cast<RE::BSShadowLight*>(nullptr), &otherLight }) {
				try {
					const C::DrawScope nested{ nestedLight };
					requireNoShaderRead();
					throw 1;
				} catch (int) {}
				Require(C::SkipShadowDraw(&pass, shadowTechnique));
			}
			bool workerKept = false;
			std::thread worker([&] { workerKept = !C::SkipShadowDraw(&pass, shadowTechnique); });
			worker.join();
			Require(workerKept);

			C::active = false;
			requireNoShaderRead();
			C::active = true;
			globals::state->loading = true;
			requireNoShaderRead();
			globals::state->loading = false;
			globals::state = nullptr;
			requireNoShaderRead();
			globals::state = &globals::testState;
			clockMs += 251;
			requireNoShaderRead();
			clockMs = captured->sampledAt - 1;
			requireNoShaderRead();
			clockMs = captured->sampledAt;
			light.worldTranslate.x += 1;
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
			light.worldTranslate.x -= 1;
			source.data.radius.x += 1;
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
			source.data.radius.x -= 1;
			light.light.value = nullptr;
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
			light.light.value = &source;

			auto closeEye = std::make_shared<C::Snapshot>(*captured);
			closeEye->eyes[1] = { 9999, 0, 0 };
			C::snapshot.Publish(closeEye);
			Require(!C::SkipShadowDraw(&pass, shadowTechnique));
			C::snapshot.Publish({});
			requireNoShaderRead();
			C::snapshot.Publish(captured);
			Require(C::SkipShadowDraw(&pass, shadowTechnique));
		}

		// Ordinary sun filtering remains a collection policy and never inherits the point-draw context.
		auto sun = std::make_shared<C::Snapshot>(*captured);
		sun->cameras[0].pass = C::Pass::Sun;
		C::snapshot.Publish(sun);
		const auto assignments = geometry.lightAssignments;
		C::Append(&culler, geometry, 3);
		Require(geometry.lightAssignments == assignments);
		{
			const C::DrawScope scope{ &light };
			requireNoShaderRead();
		}
		C::active = false;
		C::Append(&culler, geometry, 3);
		Require(geometry.lightAssignments == assignments + 1);
		std::cout << "Shadow collection, actor-part history and draw routing passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
