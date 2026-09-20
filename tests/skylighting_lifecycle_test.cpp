
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using UINT = unsigned int;
using uint = unsigned int;
#define CS_GPU_PASS(name)
constexpr uint kOcclusionCornerCount = 4;
constexpr uint kAllOcclusionCornersMask = 15;
struct float3
{
	float x = 0, y = 0, z = 0;
	float3 operator-(const float3& b) const { return { x - b.x, y - b.y, z - b.z }; }
	float3 operator*(const float3& b) const { return { x * b.x, y * b.y, z * b.z }; }
	float3 operator/(const float3& b) const { return { x / b.x, y / b.y, z / b.z }; }
};
struct float4
{
	float x = 0, y = 0, z = 0, w = 0;
};
namespace REX::W32
{
	struct XMFLOAT4X4
	{
		float data[16]{};
	};
}
namespace DirectX
{
	struct XMINT3
	{
		int x = 0, y = 0, z = 0;
	};
}
namespace RE
{
	struct Sky
	{
		enum class Mode
		{
			kNone,
			kFull
		};
		struct
		{
			Mode value = Mode::kFull;
			Mode get() const { return value; }
		} mode;
	};
	namespace RENDER_TARGETS_DEPTHSTENCIL
	{
		constexpr uint kSHADOWMAPS_ESRAM = 0;
	}
}
struct ID3D11ComputeShader
{};
struct ID3D11SamplerState
{};
struct ID3D11ShaderResourceView
{};
struct ID3D11UnorderedAccessView
{
	std::array<float, 4> floats{};
	std::array<UINT, 4> uints{};
};
template <class T>
struct View
{
	T* value = nullptr;
	T* get() const { return value; }
	View& operator=(std::nullptr_t)
	{
		value = nullptr;
		return *this;
	}
	explicit operator bool() const { return value != nullptr; }
};
struct Texture3D
{
	ID3D11ShaderResourceView srvObject;
	ID3D11UnorderedAccessView uavObject;
	View<ID3D11ShaderResourceView> srv{ &srvObject };
	View<ID3D11UnorderedAccessView> uav{ &uavObject };
};
using Texture2D = Texture3D;
struct RecordingContext
{
	unsigned dispatchCount = 0;
	unsigned dispatchedSlices = 0;
	std::array<ID3D11ShaderResourceView*, 64> pixelResources{};
	void CSSetSamplers(uint, uint, ID3D11SamplerState* const*) {}
	void CSSetShaderResources(uint, uint, ID3D11ShaderResourceView* const*) {}
	void CSSetUnorderedAccessViews(uint, uint, ID3D11UnorderedAccessView* const*, const UINT*) {}
	void CSSetShader(ID3D11ComputeShader*, void*, uint) {}
	void Dispatch(uint, uint, uint slices)
	{
		++dispatchCount;
		dispatchedSlices = slices;
	}
	unsigned clearCount = 0;
	std::vector<UINT> unboundSlots;
	std::function<void()> duringClear;
	void PSSetShaderResources(UINT slot, UINT count, ID3D11ShaderResourceView* const* views)
	{
		if (count != 1)
			throw std::runtime_error("unexpected SRV count");
		pixelResources[slot] = views[0];
		if (!views[0])
			unboundSlots.push_back(slot);
	}
	void Cleared()
	{
		++clearCount;
		if (duringClear) {
			auto callback = std::move(duringClear);
			duringClear = {};
			callback();
		}
	}
	void ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* view, const float* values)
	{
		for (unsigned i = 0; i < 4; ++i)
			view->floats[i] = values[i];
		Cleared();
	}
	void ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* view, const UINT* values)
	{
		for (unsigned i = 0; i < 4; ++i)
			view->uints[i] = values[i];
		Cleared();
	}
};
struct RecordingState
{
	bool isMapMenuOpen = false;
	unsigned updates = 0;
	std::function<void()> update;
	void UpdateFeatureData(bool inWorld)
	{
		if (!inWorld)
			throw std::runtime_error("expected world publication");
		++updates;
		update();
	}
};
struct Renderer
{
	struct Data
	{
		struct
		{
			ID3D11ShaderResourceView* depthSRV = nullptr;
		} depthStencils[1];
	} data;
	Data& GetDepthStencilData() { return data; }
};
struct Deferred
{
	Texture3D* directionalShadowLights = nullptr;
};
namespace globals
{
	inline RecordingState* state = nullptr;
	inline Deferred* deferred = nullptr;
	namespace game
	{
		inline Renderer* renderer = nullptr;
		inline RE::Sky* sky = nullptr;
	}
	namespace d3d
	{
		inline RecordingContext* context = nullptr;
		inline void* device = nullptr;
	}
}
namespace Util
{
	inline bool interior = false;
	inline float3 eye;
	float3 GetEyePosition(uint) { return eye; }
	bool IsInterior() { return interior; }
}
struct Skylighting
{
#include "skylighting_buffer_under_test.h"
	bool probeUpdateBufferEnabled = false;
	bool loaded = true;
	void* resourceDevice = nullptr;
	Texture2D* texOcclusion = nullptr;
	View<ID3D11ComputeShader> probeUpdateCompute;
	View<ID3D11SamplerState> comparisonSampler;
	bool IsRuntimeActive() const { return loaded && settings.EnableSkylighting; }
	bool HasCurrentShadowData() const { return false; }
	bool HasProbeUpdateResources() const;
	SkylightingCB GetCommonBufferData(bool inWorld);
	void Prepass();
	void SetupRenderTargetResources();
	void SetupResources() { throw std::runtime_error("unexpected volume replacement"); }
	Texture3D* texProbeArray = nullptr;
	Texture3D* texAccumFramesArray = nullptr;
	Texture3D* texShadowBitmask = nullptr;
	Texture3D* texShadowVisibility = nullptr;
	UINT probeArrayDims[3] = { 192, 192, 96 };
	void QueueResetSkylighting();
	bool UpdateInteriorState();
	void ResetSkylighting();
};

#include "skylighting_lifecycle_under_test.h"

void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
struct Fixture
{
	RecordingContext context;
	Texture3D probes, confidence, mask, visibility, occlusion;
	RecordingState state;
	Renderer renderer;
	RE::Sky sky;
	ID3D11ComputeShader compute;
	ID3D11SamplerState sampler;
	Skylighting::SkylightingCB published{};
	Skylighting feature;
	Fixture()
	{
		globals::d3d::context = &context;
		Util::interior = false;
		Util::eye = {};
		globals::state = &state;
		globals::game::renderer = &renderer;
		globals::game::sky = &sky;
		feature.texOcclusion = &occlusion;
		feature.probeUpdateCompute.value = &compute;
		feature.comparisonSampler.value = &sampler;
		state.update = [&] { published = feature.GetCommonBufferData(true); };
		feature.texProbeArray = &probes;
		feature.texAccumFramesArray = &confidence;
		feature.texShadowBitmask = &mask;
		feature.texShadowVisibility = &visibility;
	}
	void Ready()
	{
		feature.ResetSkylighting();
		feature.needsOcclusionRefresh = false;
		feature.settings.EnableIncrementalProbeUpdates = false;
		feature.settings.OcclusionUpdateInterval = 3;
		feature.settings.ProbeUpdateInterval = 6;
	}
	void Publish(bool inWorld = true) { published = feature.GetCommonBufferData(inWorld); }
};
int main()
try {
	unsigned scenarios = 0;
	{
		Fixture f;
		f.feature.QueueResetSkylighting();
		f.feature.QueueResetSkylighting();
		Require(f.context.clearCount == 0, "queueing must not touch the graphics context");
		f.feature.ResetSkylighting();
		Require(f.context.clearCount == 4, "duplicate requests must coalesce into one reset");
		Require(!f.feature.queuedResetSkylighting, "completed reset must consume its request");
		Require(f.feature.needsOcclusionRefresh, "cleared probes still require a fresh capture");
		Require(f.context.unboundSlots == std::vector<UINT>{ 50, 53 }, "history SRVs must be unbound before clears");
		Require(f.probes.uavObject.floats[0] > 3.54f && f.probes.uavObject.floats[1] == 0, "SH clear must be neutral");
		Require(f.confidence.uavObject.uints == std::array<UINT, 4>{ 0, 0, 0, 0 }, "confidence and jitter must reset");
		Require(f.mask.uavObject.uints[0] == UINT32_MAX, "shadow history must start fully lit");
		Require(f.visibility.uavObject.floats == std::array<float, 4>{ 1, 1, 1, 1 }, "visibility must start fully lit");
		Require(f.feature.forceProbeUpdateThisFrame && f.feature.forcedFullUpdateFrames == 1, "rebuild must force a full probe update");
		Require(f.feature.probeUpdateFrameCounter == 0 && f.feature.occlusionUpdateFrameCounter == 0, "rebuild must restart cadence");
		Require(f.feature.probeUpdateSliceCursor == 0 && f.feature.probeUpdateCornerMask == 0, "rebuild must restart the slice sweep");
		++scenarios;
	}
	{
		Fixture f;
		f.context.duringClear = [&]() { f.feature.QueueResetSkylighting(); };
		f.feature.ResetSkylighting();
		Require(f.feature.queuedResetSkylighting, "request delivered during reset must survive");
		f.feature.ResetSkylighting();
		Require(!f.feature.queuedResetSkylighting && f.context.clearCount == 8, "retained request must run on the next reset");
		++scenarios;
	}
	{
		Fixture f;
		globals::d3d::context = nullptr;
		f.feature.needsOcclusionRefresh = false;
		f.feature.ResetSkylighting();
		Require(f.feature.queuedResetSkylighting && f.feature.needsOcclusionRefresh, "missing context must retain invalidation");
		Require(f.context.clearCount == 0, "missing context must not issue GPU work");
		globals::d3d::context = &f.context;
		f.feature.ResetSkylighting();
		Require(!f.feature.queuedResetSkylighting && f.context.clearCount == 4, "reset must recover when the context returns");
		++scenarios;
	}
	for (unsigned missing = 0; missing < 11; ++missing) {
		Fixture f;
		switch (missing) {
		case 0:
			f.feature.texProbeArray = nullptr;
			break;
		case 1:
			f.probes.srv.value = nullptr;
			break;
		case 2:
			f.probes.uav.value = nullptr;
			break;
		case 3:
			f.feature.texAccumFramesArray = nullptr;
			break;
		case 4:
			f.confidence.uav.value = nullptr;
			break;
		case 5:
			f.feature.texShadowBitmask = nullptr;
			break;
		case 6:
			f.mask.srv.value = nullptr;
			break;
		case 7:
			f.mask.uav.value = nullptr;
			break;
		case 8:
			f.feature.texShadowVisibility = nullptr;
			break;
		case 9:
			f.visibility.srv.value = nullptr;
			break;
		case 10:
			f.visibility.uav.value = nullptr;
			break;
		}
		f.feature.needsOcclusionRefresh = false;
		f.feature.ResetSkylighting();
		Require(f.feature.queuedResetSkylighting && f.feature.needsOcclusionRefresh, "missing history resource must preserve reset and capture requirements");
		Require(f.context.clearCount == 0, "partial resources must not be cleared");
		++scenarios;
	}
	{
		Fixture f;
		Require(!f.feature.UpdateInteriorState(), "exterior classification mismatch");
		f.feature.ResetSkylighting();
		f.feature.needsOcclusionRefresh = false;
		Require(!f.feature.UpdateInteriorState() && !f.feature.queuedResetSkylighting, "stable exterior must retain cached history");
		Util::interior = true;
		Require(f.feature.UpdateInteriorState() && f.feature.queuedResetSkylighting, "interior entry must invalidate history");
		f.feature.ResetSkylighting();
		Require(f.feature.UpdateInteriorState() && !f.feature.queuedResetSkylighting, "stable interior must not repeatedly queue resets");
		Util::interior = false;
		Require(!f.feature.UpdateInteriorState() && f.feature.queuedResetSkylighting, "exterior return must invalidate history");
		Require(f.context.clearCount == 8, "transition detection must not touch graphics resources");
		++scenarios;
	}

	{
		Fixture f;
		f.Ready();
		const float cellSize = f.feature.settings.ProbeFieldSize / f.feature.probeArrayDims[0];
		Util::eye.x = 2 * cellSize;
		f.Publish();
		Require(f.published.Enabled && f.published.ValidMargin[0] == -2, "first publication must carry the newly exposed probe margin");
		f.Publish();
		Require(f.published.ValidMargin[0] == -2 && f.feature.forcedFullUpdateFrames == 1, "repeated HDR publication must retain movement and full-update debt");
		f.feature.Prepass();
		Require(f.context.dispatchCount == 1 && f.context.dispatchedSlices == 96, "first valid dispatch must update the entire probe depth");
		Require(f.feature.prevCellID.x == 2 && f.feature.forcedFullUpdateFrames == 0, "only dispatch may commit the probe location and rebuild debt");
		f.Publish();
		Require(f.published.ValidMargin[0] == 0 && !f.feature.forceProbeUpdateThisFrame, "completed update must retire movement debt");
		for (uint frame = 1; frame <= 6; ++frame) {
			f.Publish();
			f.feature.Prepass();
		}
		Require(f.context.dispatchCount == 2, "stable updates must retain the configured six-frame cadence");
		++scenarios;
	}
	for (uint unavailable = 0; unavailable < 4; ++unavailable) {
		Fixture f;
		f.Ready();
		Util::eye.x = f.feature.settings.ProbeFieldSize / f.feature.probeArrayDims[0];
		f.Publish();
		if (unavailable == 0)
			f.feature.probeUpdateCompute.value = nullptr;
		if (unavailable == 1)
			globals::d3d::context = nullptr;
		if (unavailable == 2)
			f.sky.mode.value = RE::Sky::Mode::kNone;
		if (unavailable == 3)
			f.visibility.srv.value = nullptr;
		f.feature.Prepass();
		Require(f.context.dispatchCount == 0 && f.feature.forcedFullUpdateFrames == 1 && f.feature.prevCellID.x == 0, "unavailable pass inputs must preserve all outstanding update debt");
		if (unavailable != 1) {
			Require(!f.published.Enabled && f.state.updates == 1, "late input loss must disable the published buffer before unbinding probes");
			f.Publish();
			Require(!f.published.Enabled && f.feature.forcedFullUpdateFrames == 1, "unavailable buffer queries must not retire rebuild debt");
		}
		f.feature.probeUpdateCompute.value = &f.compute;
		globals::d3d::context = &f.context;
		f.sky.mode.value = RE::Sky::Mode::kFull;
		f.visibility.srv.value = &f.visibility.srvObject;
		f.Publish();
		Require(f.published.Enabled && f.published.ValidMargin[0] == -1, "recovery must preserve the pending movement margin");
		f.feature.Prepass();
		Require(f.context.dispatchCount == 1 && !f.feature.forcedFullUpdateFrames, "recovered resources must complete one full update");
		++scenarios;
	}
	{
		Fixture f;
		f.Ready();
		f.Publish();
		f.feature.QueueResetSkylighting();
		f.feature.Prepass();
		Require(f.context.dispatchCount == 0 && !f.published.Enabled && f.state.updates == 1, "late load invalidation must disable the uploaded settings before unbinding");
		Require(!f.context.pixelResources[50] && !f.context.pixelResources[53], "invalidated histories must be unbound");
		Require(f.feature.queuedResetSkylighting, "blocked sampling must preserve the reset request for capture");
		f.feature.Prepass();
		Require(f.state.updates == 1, "already-disabled buffers must not be republished repeatedly");
		++scenarios;
	}
	{
		Fixture f;
		f.Ready();
		f.feature.needsOcclusionRefresh = true;
		f.Publish();
		f.feature.needsOcclusionRefresh = false;
		f.feature.Prepass();
		Require(f.context.dispatchCount == 0 && f.feature.forcedFullUpdateFrames == 1, "a later capture cannot authorize dispatch with a disabled published buffer");
		f.Publish(false);
		f.feature.Prepass();
		Require(f.context.dispatchCount == 0, "reflection publication must not authorize a world probe update");
		f.Publish();
		f.feature.Prepass();
		Require(f.context.dispatchCount == 1 && !f.feature.forcedFullUpdateFrames, "fresh world publication must authorize the pending rebuild");
		++scenarios;
	}
	{
		Fixture f;
		f.Ready();
		f.feature.forcedFullUpdateFrames = 0;
		f.probes.uavObject.floats[0] = 1.25f;
		const uint clears = f.context.clearCount;
		f.feature.SetupRenderTargetResources();
		Require(!f.feature.queuedResetSkylighting && !f.feature.needsOcclusionRefresh, "same-device target replacement must not introduce scene invalidation");
		Require(f.context.clearCount == clears && f.probes.uavObject.floats[0] == 1.25f && !f.feature.forcedFullUpdateFrames, "same-device replacement must preserve world-space histories and update cadence");
		++scenarios;
	}
	std::cout << scenarios << " production lifecycle scenarios passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
