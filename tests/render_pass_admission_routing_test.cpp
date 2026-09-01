#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
	int effectRTTI;
	int otherRTTI;
	unsigned rttiReads{};
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

namespace RE
{
	struct NiPoint3
	{
		float distance{};
		float GetDistance(const NiPoint3& other) const { return std::abs(distance - other.distance); }
	};
	struct BSGeometry
	{
		struct
		{
			NiPoint3 center;
			float radius{};
		} worldBound;
	};
	struct BSShaderProperty
	{
		enum class EShaderPropertyFlag : uint32_t
		{
			kMultiTextureLandscape = 1,
			kNoTransparencyMultiSample = 2
		};
		struct
		{
			uint32_t value{};
			bool all(EShaderPropertyFlag flag) const { return (value & static_cast<uint32_t>(flag)) != 0; }
			bool any(EShaderPropertyFlag flag) const { return all(flag); }
		} flags;
		const void* rtti = &otherRTTI;
		const void* GetRTTI() const
		{
			++rttiReads;
			return rtti;
		}
	};
	struct BSEffectShaderProperty : BSShaderProperty
	{
		void* lightData{};
		BSEffectShaderProperty() { rtti = &effectRTTI; }
	};
	struct BSShader
	{
		enum class Type
		{
			Lighting,
			Effect
		};
		struct
		{
			Type value{ Type::Lighting };
			Type get() const { return value; }
		} shaderType;
	};
	struct BSRenderPass
	{
		BSShader* shader{};
		BSShaderProperty* shaderProperty{};
		BSGeometry* geometry{};
	};
}

namespace globals
{
	struct ShaderCache
	{
		bool enabled = true;
		bool IsEnabled() const { return enabled; }
	} cache;
	ShaderCache* shaderCache = &cache;
	struct State
	{
		bool inWorld = true;
	} stateStorage;
	State* state = &stateStorage;
	namespace rtti
	{
		struct
		{
			const void* get() const { return &effectRTTI; }
		} BSEffectShaderPropertyRTTI;
	}
}

struct LightLimitFix
{
	struct
	{
		bool EnableParticleLights = true;
		bool EnableParticleLightsCulling = true;
	} settings;
	struct ParticleLightReference
	{
		bool valid = true;
		struct
		{
			bool cull = true;
		} config;
	} configuredReference;
	unsigned configCalls{}, addCalls{};
	bool* observedAdmission{};
	bool addAccepted = true;
	bool throwEffect{};
	ParticleLightReference GetParticleLightConfigs(RE::BSRenderPass* pass, RE::BSEffectShaderProperty* property)
	{
		++configCalls;
		Check(property == pass->shaderProperty, "effect property must match the admitted pass");
		Check(!observedAdmission || *observedAdmission, "admission must be invalidated before effect processing");
		if (throwEffect)
			throw std::runtime_error("effect callback");
		return configuredReference;
	}
	bool AddParticleLight(RE::BSRenderPass*, const ParticleLightReference&)
	{
		++addCalls;
		Check(!observedAdmission || *observedAdmission, "light insertion must retain invalidation");
		return addAccepted;
	}
	bool CheckParticleLights(RE::BSRenderPass*, uint32_t, bool* = nullptr);
};

struct TerrainBlending
{
	enum class RenderPassImmediatelyAction
	{
		Draw,
		Skip,
		DrawTwice
	};
	struct
	{
		bool Enabled = true;
		float TerrainCullDistance{};
	} settings;
	struct RenderPass
	{
		RE::BSRenderPass* pass;
		uint32_t technique;
		bool alphaTest;
		uint32_t flags;
	};
	bool renderDepth{}, renderTerrainDepth{};
	RE::NiPoint3 averageEyePosition;
	std::vector<RenderPass> terrainRenderPasses, renderPasses;
	struct
	{
		unsigned terrainDepthDoubleDrawCalls{}, queueTerrainCalls{}, queueNoBlendCalls{};
	} tbHookDiagnostics;
	unsigned resets{};
	bool* observedAdmission{};
	bool throwReset{};
	void ResetTerrainDepth()
	{
		++resets;
		Check(!observedAdmission || *observedAdmission, "admission must be invalidated before depth reset");
		if (throwReset)
			throw std::runtime_error("depth callback");
	}
	RenderPassImmediatelyAction OnRenderPassImmediately(RE::BSRenderPass*, uint32_t, bool, uint32_t, bool* = nullptr);
};

#include "render_pass_admission_routing_under_test.h"

namespace
{
	void ParticleRouting()
	{
		LightLimitFix lights;
		RE::BSGeometry geometry;
		RE::BSEffectShaderProperty property;
		RE::BSRenderPass pass{ nullptr, &property, &geometry };
		bool invalidated = true;
		lights.observedAdmission = &invalidated;
		const auto expectReadOnly = [&](RE::BSRenderPass* candidate) {
			invalidated = true;
			Check(lights.CheckParticleLights(candidate, 0, &invalidated), "read-only rejection must keep native draw");
			Check(!invalidated && lights.configCalls == 0, "read-only routing must reuse admission");
		};
		expectReadOnly(nullptr);
		pass.geometry = nullptr;
		expectReadOnly(&pass);
		pass.geometry = &geometry;
		pass.shaderProperty = nullptr;
		expectReadOnly(&pass);
		pass.shaderProperty = &property;
		globals::cache.enabled = false;
		expectReadOnly(&pass);
		globals::cache.enabled = true;
		lights.settings.EnableParticleLights = false;
		expectReadOnly(&pass);
		lights.settings.EnableParticleLights = true;
		Check(rttiReads == 0, "disabled processing must not probe RTTI");
		property.rtti = &otherRTTI;
		expectReadOnly(&pass);
		Check(rttiReads == 1, "ordinary rejection needs exactly one RTTI read");
		property.rtti = &effectRTTI;
		property.lightData = &effectRTTI;
		expectReadOnly(&pass);
		property.lightData = nullptr;
		for (const bool valid : { false, true }) {
			for (const bool accepted : { false, true }) {
				for (const bool culling : { false, true }) {
					for (const bool cull : { false, true }) {
						lights.configuredReference.valid = valid;
						lights.configuredReference.config.cull = cull;
						lights.addAccepted = accepted;
						lights.settings.EnableParticleLightsCulling = culling;
						invalidated = false;
						const auto configs = lights.configCalls;
						const auto additions = lights.addCalls;
						const auto reads = rttiReads;
						const bool draw = lights.CheckParticleLights(&pass, 0, &invalidated);
						Check(draw == !(valid && accepted && culling && cull), "particle culling behavior changed");
						Check(invalidated && lights.configCalls == configs + 1 && lights.addCalls == additions + (valid ? 1 : 0), "effect work must be admitted once");
						Check(rttiReads == reads + 1, "effect eligibility must not repeat RTTI work");
					}
				}
			}
		}
		invalidated = false;
		lights.throwEffect = true;
		bool propagated{};
		try {
			lights.CheckParticleLights(&pass, 0, &invalidated);
		} catch (const std::runtime_error&) {
			propagated = true;
		}
		Check(propagated && invalidated, "effect exception must propagate without restoring stale admission");
		lights.throwEffect = false;
		lights.observedAdmission = nullptr;
		lights.CheckParticleLights(&pass, 0);
	}

	void TerrainRouting()
	{
		using Action = TerrainBlending::RenderPassImmediatelyAction;
		RE::BSGeometry geometry;
		RE::BSShaderProperty property;
		RE::BSShader shader;
		RE::BSRenderPass pass{ &shader, &property, &geometry };
		for (const bool enabled : { false, true }) {
			for (const bool cache : { false, true }) {
				for (const bool depth : { false, true }) {
					for (const bool previousTerrain : { false, true }) {
						for (const bool terrain : { false, true }) {
							for (const bool distant : { false, true }) {
								TerrainBlending blending;
								bool invalidated = true;
								blending.observedAdmission = &invalidated;
								blending.settings.Enabled = enabled;
								blending.settings.TerrainCullDistance = 100;
								blending.renderDepth = depth;
								blending.renderTerrainDepth = previousTerrain;
								globals::cache.enabled = cache;
								property.flags.value = terrain ? 1 : 0;
								geometry.worldBound.center.distance = distant ? 200.0f : 0.0f;
								const bool active = enabled && cache;
								const bool inTerrain = terrain && !distant;
								const auto expected = !active ? Action::Draw : depth ? (inTerrain ? Action::DrawTwice : Action::Draw) :
								                                           terrain   ? Action::Skip :
								                                                       Action::Draw;
								Check(blending.OnRenderPassImmediately(&pass, 7, true, 9, &invalidated) == expected, "terrain routing changed");
								const bool reset = active && depth && previousTerrain && !inTerrain;
								Check(invalidated == reset && blending.resets == (reset ? 1U : 0U), "only depth reset may invalidate terrain admission");
								if (expected == Action::Skip) {
									Check(blending.terrainRenderPasses.size() == 1, "terrain replay must be queued");
									const auto& queued = blending.terrainRenderPasses.front();
									Check(queued.pass == &pass && queued.technique == 7 && queued.alphaTest && queued.flags == 9, "queued draw arguments changed");
								}
							}
						}
					}
				}
			}
		}
		globals::cache.enabled = true;
		TerrainBlending blending;
		property.flags.value = 2;
		bool invalidated = true;
		Check(blending.OnRenderPassImmediately(&pass, 7, true, 9, &invalidated) == Action::Skip && !invalidated, "no-blend pass must queue without callbacks");
		Check(blending.renderPasses.size() == 1, "no-blend replay must be queued");
		globals::state->inWorld = false;
		Check(blending.OnRenderPassImmediately(&pass, 7, true, 9, &invalidated) == Action::Draw, "non-world routing must draw immediately");
		globals::state->inWorld = true;
		blending.renderDepth = blending.renderTerrainDepth = true;
		blending.throwReset = true;
		blending.observedAdmission = &invalidated;
		bool propagated{};
		try {
			blending.OnRenderPassImmediately(&pass, 7, true, 9, &invalidated);
		} catch (const std::runtime_error&) {
			propagated = true;
		}
		Check(propagated && invalidated, "depth exception must propagate and retain invalidation");
	}
}

int main()
{
	try {
		ParticleRouting();
		TerrainRouting();
		std::cout << "production particle and terrain admission routing tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
