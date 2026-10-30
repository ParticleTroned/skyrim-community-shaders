#include "EngineFixes/VRShadowBatchPolicy.h"
#include "Features/LightLimitFix/ShadowCullingDrawScope.h"
#include "Features/LightLimitFix/ShadowCullingPolicy.h"
#include "Features/LightLimitFix/ShadowCullingSnapshot.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <thread>

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
	};
	struct BSCullingProcess
	{
		NiCamera* camera = nullptr;
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
		return { geometry.actor, geometry.dynamic, geometry.protectedGeometry, geometry.actorRoot };
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

int main()
{
	try {
		RE::NiCamera camera;
		RE::NiLight source{ { { 10000, 0, 0 } } };
		RE::BSShadowLight light{ { &source }, { 1000, 0, 0 } }, otherLight;
		RE::BSGeometry geometry{ { { 10000, 0, 0 }, 1 } };
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
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
			Require(shader.shaderType.reads == reads);
		};

		// Both ordinary and actor casters keep native light assignment even when their shadow is culled.
		for (const bool actor : { false, true }) {
			geometry.actor = actor;
			Require(C::Skip(*captured, captured->cameras[0], geometry));
			const auto assignments = geometry.lightAssignments;
			C::Append(&culler, geometry, 3);
			Require(geometry.lightAssignments == assignments + 1);
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
			{
				const C::DrawScope scope{ &light };
				Require(C::SkipPointShadowDraw(&pass, shadowTechnique));
			}
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
		}
		geometry.actor = false;
		{
			const C::DrawScope scope{ &light };
			shader.shaderType.value = RE::BSShader::Type::Lighting;
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
			shader.shaderType.value = RE::BSShader::Type::Utility;
			for (const auto technique : { 0u, VRShadowBatch::kUtilityTechniqueStart - 1,
					 VRShadowBatch::kUtilityTechniqueStart + (1u << 13), VRShadowBatch::kUtilityTechniqueStart + (1u << 21) })
				Require(!C::SkipPointShadowDraw(&pass, technique));
			pass.numLights = 1;
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
			pass.numLights = 0;
			pass.numShadowLights = 1;
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
			pass.numShadowLights = 0;
			Require(!C::SkipPointShadowDraw(nullptr, shadowTechnique));
			pass.geometry = nullptr;
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
			pass.geometry = &geometry;
			pass.shader = nullptr;
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
			pass.shader = &shader;

			for (auto* nestedLight : { static_cast<RE::BSShadowLight*>(nullptr), &otherLight }) {
				try {
					const C::DrawScope nested{ nestedLight };
					requireNoShaderRead();
					throw 1;
				} catch (int) {}
				Require(C::SkipPointShadowDraw(&pass, shadowTechnique));
			}
			bool workerKept = false;
			std::thread worker([&] { workerKept = !C::SkipPointShadowDraw(&pass, shadowTechnique); });
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
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
			light.worldTranslate.x -= 1;
			source.data.radius.x += 1;
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
			source.data.radius.x -= 1;
			light.light.value = nullptr;
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
			light.light.value = &source;

			auto closeEye = std::make_shared<C::Snapshot>(*captured);
			closeEye->eyes[1] = { 9999, 0, 0 };
			C::snapshot.Publish(closeEye);
			Require(!C::SkipPointShadowDraw(&pass, shadowTechnique));
			C::snapshot.Publish({});
			requireNoShaderRead();
			C::snapshot.Publish(captured);
			Require(C::SkipPointShadowDraw(&pass, shadowTechnique));
		}

		// Sun filtering remains a collection policy and never inherits the point-draw context.
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
		std::cout << "Shadow collection and draw routing passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
