#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>

namespace RE
{
	struct BSLight
	{
		bool portalStrict = false;
		void* portalGraph = this;
		bool affectWater = true;
		bool shadow = false;
		bool IsShadowLight() const { return shadow; }
	};
	struct NiAVObject
	{
		enum class Flag
		{
			kHidden
		};
	};
	struct NiLight
	{
		struct Flags
		{
			bool hidden = false;
			bool any(NiAVObject::Flag) const { return hidden; }
		} flags;
		const Flags& GetFlags() const { return flags; }
	};
	struct BSRenderPass
	{
		BSLight** sceneLights = nullptr;
		uint32_t numLights = 0;
		uint32_t passEnum = 0;
	};
}
struct ShaderCache
{
	bool enabled = true;
	bool IsEnabled() const { return enabled; }
};
namespace globals
{
	namespace features
	{
		struct LightLimitFixState
		{
			bool loaded = true;
		} lightLimitFix;
	}
	inline ShaderCache cache;
	inline ShaderCache* shaderCache = &cache;
}
namespace logger
{
	template <class... Args>
	void debug(const char*, Args&&...)
	{}
}
struct LightLimitFix
{
#include "editor_water_policy_under_test.h"
};
struct LightEditor
{
#include "editor_water_diagnostics_members.h"
};
LightEditor::WaterRoutingDiagnostics LightEditor::waterRoutingDiagnostics{};
#include "editor_water_diagnostics_under_test.h"

int main()
{
	LightEditor editor;
	RE::BSLight selected, unrelated;
	RE::NiLight sceneNode;
	auto& diagnostics = LightEditor::waterRoutingDiagnostics;
	RE::BSLight* lights[]{ &unrelated, &selected, &selected };
	RE::BSRenderPass pass{ lights, 3, 0x1A2B };

	// The surviving native LLF hook must resolve to a real declared observer.
	InvokeWaterPassHook(&pass);
	assert(diagnostics.waterPassCount == 0);
	editor.UpdateWaterRoutingDiagnostics(&selected, &sceneNode);
	assert(diagnostics.available && diagnostics.observerAvailable);
	assert(diagnostics.llfWaterCandidate && !diagnostics.skyrimWaterPassEligible);
	InvokeWaterPassHook(&pass);
	assert(diagnostics.waterPassCount == 1);
	assert(diagnostics.matchedWaterPassCount == 1);
	assert(diagnostics.lastPassEnum == pass.passEnum);
	assert(diagnostics.lastSceneLightIndex == 1);
	assert(diagnostics.lastSceneLightCount == 3);

	// Identical selection keeps evidence; a new owner classification resets it.
	editor.UpdateWaterRoutingDiagnostics(&selected, &sceneNode);
	assert(diagnostics.matchedWaterPassCount == 1);
	selected.portalStrict = true;
	editor.UpdateWaterRoutingDiagnostics(&selected, &sceneNode);
	assert(!diagnostics.llfWaterCandidate && diagnostics.skyrimWaterPassEligible);
	assert(diagnostics.waterPassCount == 0 && diagnostics.matchedWaterPassCount == 0);
	InvokeWaterPassHook(nullptr);
	assert(diagnostics.waterPassCount == 1 && diagnostics.matchedWaterPassCount == 0);

	// Changing the retained light cannot continue reporting the old target.
	editor.UpdateWaterRoutingDiagnostics(&unrelated, &sceneNode);
	pass.sceneLights = &lights[1];
	pass.numLights = 2;
	InvokeWaterPassHook(&pass);
	assert(diagnostics.waterPassCount == 1 && diagnostics.matchedWaterPassCount == 0);
	assert(diagnostics.lastPassEnum == 0);
	unrelated.affectWater = false;
	editor.UpdateWaterRoutingDiagnostics(&unrelated, &sceneNode);
	assert(!diagnostics.llfWaterCandidate && !diagnostics.skyrimWaterPassEligible);
	unrelated.affectWater = true;
	unrelated.portalGraph = nullptr;
	editor.UpdateWaterRoutingDiagnostics(&unrelated, &sceneNode);
	assert(!diagnostics.llfWaterCandidate && diagnostics.skyrimWaterPassEligible);

	unrelated.portalGraph = &unrelated;
	unrelated.shadow = true;
	editor.UpdateWaterRoutingDiagnostics(&unrelated, &sceneNode);
	assert(!diagnostics.llfWaterCandidate && diagnostics.skyrimWaterPassEligible);
	unrelated.shadow = false;
	sceneNode.flags.hidden = true;
	editor.UpdateWaterRoutingDiagnostics(&unrelated, &sceneNode);
	assert(!diagnostics.llfWaterCandidate);
	sceneNode.flags.hidden = false;
	globals::cache.enabled = false;
	editor.UpdateWaterRoutingDiagnostics(&unrelated, &sceneNode);
	assert(!diagnostics.llfWaterCandidate);
	globals::features::lightLimitFix.loaded = false;
	editor.UpdateWaterRoutingDiagnostics(&unrelated, &sceneNode);
	assert(!diagnostics.observerAvailable && diagnostics.skyrimWaterPassEligible);

	// Restore/deselection clears the borrowed comparison pointer before owners release.
	editor.UpdateWaterRoutingDiagnostics(nullptr, &sceneNode);
	assert(!diagnostics.available && diagnostics.target == nullptr);
	InvokeWaterPassHook(&pass);
	assert(diagnostics.waterPassCount == 0 && diagnostics.matchedWaterPassCount == 0);
	std::cout << "Native SE water-observer contract and diagnostics lifecycle passed\n";
}
