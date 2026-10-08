#include "Api/AcceptedDrawGeometryScope.h"
#include <d3d11.h>

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
	CSX::Api::AcceptedDrawGeometryScope geometryScope;
	unsigned suppressionDepth = 0;
	bool dispatching = false;
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}
	struct AcceptedDrawRegistry
	{
		static bool IsDispatching() { return dispatching; }
	};
}
namespace CSX::Api
{
#include "accepted_draw_geometry_match_under_test.h"

}

namespace RE
{
	struct Geometry
	{
		Geometry* parent = nullptr;
		bool culled = false;
		bool GetAppCulled() const { return culled; }
	};
	using NiAVObject = Geometry;
	struct PlayerCharacter
	{
		struct Nodes
		{
			struct
			{
				Geometry* value = nullptr;
				Geometry* get() const { return value; }
			} UIPointerGeo;
		} nodes;
		bool hasNodes = true;
		static inline PlayerCharacter* player = nullptr;
		static inline unsigned lookups = 0;
		static PlayerCharacter* GetSingleton()
		{
			++lookups;
			return player;
		}
		Nodes* GetVRNodeData() { return hasNodes ? &nodes : nullptr; }
	};
}

struct Upscaling
{
	bool latched = true;
	bool vrMenuParallelBridgeDrawInProgress = false;
	unsigned vrMenuDrawInterfaceDepth = 1;
	bool captureSucceeds = true;
	unsigned captures = 0;
	bool instanced = false;
	std::array<int64_t, 5> arguments{};
	mutable unsigned latchQueries = 0;
	bool IsVRRenderScaleModeLatched() const
	{
		++latchQueries;
		return latched;
	}
	const void* vrMenuPointerGeometry = nullptr;
	uint32_t vrMenuPointerPresentationFrame = UINT_MAX;
	bool vrMenuPointerPresentationVisible = false;
	ID3D11ShaderResourceView* GetCurrentVRMenuPointerOverlay(uint32_t);
	struct
	{
		ID3D11ShaderResourceView* layer = nullptr;
		void Invalidate(uint32_t, uint32_t) { layer = nullptr; }
		ID3D11ShaderResourceView* GetLayer(uint32_t, uint32_t) const { return layer; }
	} vrMenuPointerOverlay;
	uint32_t contractGeneration = 1;
	uint32_t GetActiveVRRenderScaleContractGeneration() const { return contractGeneration; }
	bool CaptureVRMenuPointerOverlay(ID3D11DeviceContext*, UINT a_indices, UINT a_instances,
		UINT a_start, INT a_base, UINT a_firstInstance, bool a_instanced)
	{
		++captures;
		arguments = { a_indices, a_instances, a_start, a_base, a_firstInstance };
		instanced = a_instanced;
		return captureSucceeds;
	}
	static bool TryCaptureVRMenuPointerDraw(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT, bool);
};
namespace globals
{
	struct State
	{
		uint32_t frameCount = 1;
	} stateValue;
	State* state = &stateValue;
	namespace game
	{
		bool isVR = true;
	}
	namespace d3d
	{
		ID3D11DeviceContext* context = nullptr;
	}
	namespace features
	{
		Upscaling upscaling;
	}
}
bool explicitMenu = true;
unsigned menuQueries = 0;
bool IsExplicitVRMenuPresentationContextActive()
{
	++menuQueries;
	return explicitMenu;
}
bool IsVRMenuPointerVisible(const RE::NiAVObject*);
#include "vr_menu_pointer_presentation_under_test.h"
#include "vr_menu_pointer_under_test.h"
#include "vr_menu_pointer_visibility_under_test.h"

void TestPointerAdmission()
{
	int contextIdentity, otherContextIdentity, pass, nestedPass;
	RE::Geometry pointer, otherGeometry;
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(&contextIdentity);
	auto* otherContext = reinterpret_cast<ID3D11DeviceContext*>(&otherContextIdentity);
	globals::d3d::context = context;
	RE::PlayerCharacter player;
	RE::PlayerCharacter::player = &player;
	player.nodes.UIPointerGeo.value = &pointer;
	auto& upscaling = globals::features::upscaling;
	auto draw = [&](bool instanced = true, ID3D11DeviceContext* ctx = nullptr) {
		return Upscaling::TryCaptureVRMenuPointerDraw(ctx ? ctx : context, 6, 2, 3, -7, 1, instanced);
	};
	auto rejects = [&] {
		const auto captures = upscaling.captures;
		Require(!draw() && captures == upscaling.captures, "unrelated or inactive draw was captured");
	};
	rejects();
	geometryScope.Begin(&pass);
	rejects();
	geometryScope.Activate(&pass, &otherGeometry);
	const auto queries = menuQueries;
	const auto latchQueries = upscaling.latchQueries;
	rejects();
	Require(menuQueries == queries, "unrelated geometry queried menu state");
	Require(upscaling.latchQueries == latchQueries, "unrelated geometry queried render-scale state");
	geometryScope.Activate(&pass, &pointer);
	Require(draw(), "native pointer was not admitted");
	Require(upscaling.instanced && upscaling.arguments == std::array<int64_t, 5>{ 6, 2, 3, -7, 1 },
		"stereo draw arguments changed");
	Require(draw(false) && !upscaling.instanced, "indexed draw family changed");
	upscaling.captureSucceeds = false;
	Require(!draw(), "failed replay suppressed the native laser");
	upscaling.captureSucceeds = true;
	geometryScope.Begin(&nestedPass);
	rejects();
	geometryScope.Activate(&nestedPass, &otherGeometry);
	rejects();
	geometryScope.End(&nestedPass);
	Require(draw(), "nested draw lost the parent pointer identity");
	geometryScope.Suspend();
	rejects();
	geometryScope.Activate(&pass, &pointer);
	suppressionDepth = 1;
	rejects();
	suppressionDepth = 0;
	dispatching = true;
	rejects();
	dispatching = false;
	player.nodes.UIPointerGeo.value = nullptr;
	rejects();
	player.nodes.UIPointerGeo.value = &pointer;
	player.hasNodes = false;
	rejects();
	player.hasNodes = true;
	RE::PlayerCharacter::player = nullptr;
	rejects();
	RE::PlayerCharacter::player = &player;
	const auto lookups = RE::PlayerCharacter::lookups;
	globals::game::isVR = false;
	rejects();
	globals::game::isVR = true;
	upscaling.vrMenuParallelBridgeDrawInProgress = true;
	rejects();
	upscaling.vrMenuParallelBridgeDrawInProgress = false;
	Require(RE::PlayerCharacter::lookups == lookups, "inactive paths inspected player geometry");
	upscaling.latched = false;
	rejects();
	upscaling.latched = true;
	upscaling.vrMenuDrawInterfaceDepth = 0;
	Require(draw(), "native scene pointer outside DrawInterface was not captured");
	explicitMenu = false;
	rejects();
	explicitMenu = true;
	pointer.culled = true;
	rejects();
	pointer.culled = false;
	upscaling.vrMenuDrawInterfaceDepth = 1;
	Require(!draw(true, otherContext), "auxiliary context was admitted");
	Require(!Upscaling::TryCaptureVRMenuPointerDraw(nullptr, 6, 2, 0, 0, 0, true), "null context was admitted");
	Require(!Upscaling::TryCaptureVRMenuPointerDraw(context, 0, 2, 0, 0, 0, true), "empty index draw was admitted");
	Require(!Upscaling::TryCaptureVRMenuPointerDraw(context, 6, 0, 0, 0, 0, true), "empty instance draw was admitted");
	globals::state = nullptr;
	rejects();
	globals::state = &globals::stateValue;
	geometryScope.End(&pass);
	rejects();
	RE::PlayerCharacter::player = nullptr;
}

void TestPointerVisibility()
{
	RE::Geometry pointer, parent, grandparent;
	pointer.parent = &parent;
	parent.parent = &grandparent;
	Require(IsVRMenuPointerVisible(&pointer), "visible pointer rejected");
	grandparent.culled = true;
	Require(!IsVRMenuPointerVisible(&pointer), "hidden ancestor left the overlay visible");
	grandparent.culled = false;
	grandparent.parent = &pointer;
	Require(!IsVRMenuPointerVisible(&pointer), "cyclic ancestry was not bounded");
	Require(!IsVRMenuPointerVisible(nullptr), "missing pointer was visible");
}

void TestPointerStereoPresentation()
{
	RE::Geometry pointer, replacement, parent;
	pointer.parent = &parent;
	RE::PlayerCharacter player;
	player.nodes.UIPointerGeo.value = &pointer;
	RE::PlayerCharacter::player = &player;
	auto& upscaling = globals::features::upscaling;
	upscaling = {};
	upscaling.vrMenuPointerGeometry = &pointer;
	int layerIdentity;
	auto* layer = reinterpret_cast<ID3D11ShaderResourceView*>(&layerIdentity);
	upscaling.vrMenuPointerOverlay.layer = layer;
	Require(upscaling.GetCurrentVRMenuPointerOverlay(10) == layer, "first eye lost visible pointer");
	parent.culled = true;
	Require(upscaling.GetCurrentVRMenuPointerOverlay(10) == layer, "visibility changed between eyes");
	Require(upscaling.GetCurrentVRMenuPointerOverlay(11) == nullptr, "hidden pointer survived into next presentation");
	parent.culled = false;
	Require(upscaling.GetCurrentVRMenuPointerOverlay(11) == nullptr, "late visibility change affected only second eye");
	Require(upscaling.GetCurrentVRMenuPointerOverlay(12) == layer, "new visible frame did not recover");
	player.nodes.UIPointerGeo.value = &replacement;
	Require(upscaling.GetCurrentVRMenuPointerOverlay(13) == nullptr, "replacement geometry consumed an old pointer layer");
	player.nodes.UIPointerGeo.value = &pointer;
	explicitMenu = false;
	Require(upscaling.GetCurrentVRMenuPointerOverlay(14) == nullptr, "closed menu retained pointer");
	explicitMenu = true;
	upscaling.vrMenuPointerOverlay.layer = nullptr;
	Require(upscaling.GetCurrentVRMenuPointerOverlay(15) == nullptr, "missing current-frame layer reused pointer");
	int pass, contextIdentity;
	globals::d3d::context = reinterpret_cast<ID3D11DeviceContext*>(&contextIdentity);
	geometryScope.Begin(&pass);
	geometryScope.Activate(&pass, &pointer);
	globals::state->frameCount = 15;
	Require(!Upscaling::TryCaptureVRMenuPointerDraw(globals::d3d::context, 12, 2, 0, 0, 0, true) && upscaling.captures == 0,
		"late capture changed the frozen stereo presentation");
	geometryScope.End(&pass);
	RE::PlayerCharacter::player = nullptr;
}

int main()
try {
	TestPointerAdmission();
	TestPointerVisibility();
	TestPointerStereoPresentation();
	std::cout << "VR menu pointer: identity, scope, outside-interface capture, stereo arguments, freshness and visibility passed\n";
} catch (const std::exception& e) {
	std::cerr << e.what() << '\n';
	return 1;
}
