#include "Utils/RuntimeToggle.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
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
		constexpr uint kPRECIPITATION_OCCLUSION_MAP = 1;
	}
}
struct ID3D11ComputeShader
{};
struct ID3D11SamplerState
{};
struct ID3D11ShaderResourceView
{
	template <class Desc>
	void GetDesc(Desc*) const
	{}
};
struct ID3D11DepthStencilView : ID3D11ShaderResourceView
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
	T** put() { return &value; }
	View& operator=(std::nullptr_t)
	{
		value = nullptr;
		return *this;
	}
	explicit operator bool() const { return value != nullptr; }
};
namespace winrt
{
	template <class T>
	using com_ptr = View<T>;
}
namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F run;
		explicit scope_exit(F f) : run(std::move(f)) {}
		~scope_exit() { run(); }
	};
}
struct D3D11_TEXTURE2D_DESC
{};
struct D3D11_DEPTH_STENCIL_VIEW_DESC
{};
struct D3D11_TEXTURE3D_DESC
{
	uint Width, Height, Depth, MipLevels, Format, Usage, BindFlags, CPUAccessFlags, MiscFlags;
};
struct D3D11_SHADER_RESOURCE_VIEW_DESC
{
	uint Format, ViewDimension;
	struct
	{
		uint MostDetailedMip, MipLevels;
	} Texture3D;
};
struct D3D11_UNORDERED_ACCESS_VIEW_DESC
{
	uint Format, ViewDimension;
	struct
	{
		uint MipSlice, FirstWSlice, WSize;
	} Texture3D;
};
struct D3D11_SAMPLER_DESC
{
	uint Filter, AddressU, AddressV, AddressW, ComparisonFunc;
	float MinLOD, MaxLOD;
};
constexpr uint DXGI_FORMAT_R16G16B16A16_FLOAT = 1, DXGI_FORMAT_R16_UINT = 2, DXGI_FORMAT_R32_UINT = 3, DXGI_FORMAT_R8_UNORM = 4;
constexpr uint D3D11_USAGE_DEFAULT = 0, D3D11_BIND_SHADER_RESOURCE = 1, D3D11_BIND_UNORDERED_ACCESS = 2;
constexpr uint D3D11_SRV_DIMENSION_TEXTURE3D = 0, D3D11_UAV_DIMENSION_TEXTURE3D = 0;
constexpr uint D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR = 0, D3D11_TEXTURE_ADDRESS_CLAMP = 0, D3D11_COMPARISON_LESS_EQUAL = 0;
constexpr float D3D11_FLOAT32_MAX = 1e38f;
namespace Allocation
{
	inline unsigned calls = 0, failAt = 0, liveTextures = 0;
	inline std::function<void()> duringCreate;
	void Step()
	{
		if (++calls == failAt)
			throw std::runtime_error("injected resource creation failure");
		if (duringCreate) {
			auto callback = std::move(duringCreate);
			duringCreate = {};
			callback();
		}
	}
}
struct ID3D11Device
{
	ID3D11SamplerState sampler;
	int CreateSamplerState(const D3D11_SAMPLER_DESC*, ID3D11SamplerState** output)
	{
		Allocation::Step();
		*output = &sampler;
		return 0;
	}
};
namespace DX
{
	void ThrowIfFailed(int result)
	{
		if (result)
			throw std::runtime_error("resource creation failed");
	}
}
struct Texture3D
{
	Texture3D() { ++Allocation::liveTextures; }
	template <class Desc>
	Texture3D(const Desc&, const char*)
	{
		Allocation::Step();
		++Allocation::liveTextures;
	}
	~Texture3D() { --Allocation::liveTextures; }
	template <class Desc>
	void CreateSRV(const Desc&)
	{
		Allocation::Step();
	}
	void CreateUAV(const D3D11_UNORDERED_ACCESS_VIEW_DESC&) { Allocation::Step(); }
	void CreateDSV(const D3D11_DEPTH_STENCIL_VIEW_DESC&) { Allocation::Step(); }
	template <class Desc>
	void GetDesc(Desc*) const
	{}
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
	void PSGetShaderResources(UINT slot, UINT, ID3D11ShaderResourceView** views) { *views = pixelResources[slot]; }
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
	uint32_t frameCount = 1;
	bool blocked = false;
	bool pendingPostLoadRuntimeReset = false;
	bool IsSaveLoadSafeModeActive() const { return blocked; }
	bool IsEngineSaveLoadActivityActive() const { return false; }
	bool IsMainOrLoadingMenuOpen() const { return false; }
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
			Texture2D* texture = nullptr;
			ID3D11ShaderResourceView* depthSRV = nullptr;
			ID3D11DepthStencilView* views[1]{};
		} depthStencils[2];
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
		inline ID3D11Device* device = nullptr;
	}
}
namespace Util
{
	void SetResourceName(ID3D11SamplerState*, const char*) {}
	inline bool rendererAvailable = true;
	bool GetRendererContextLock(Renderer* renderer, RecordingContext* context) { return renderer && context && rendererAvailable; }
	struct RendererOwnership
	{
		bool owned;
		explicit RendererOwnership(bool available) : owned(available) {}
		explicit operator bool() const { return owned; }
	};
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
	bool HasCurrentShadowData() const { return false; }
	bool HasProbeUpdateResources() const;
	SkylightingCB GetCommonBufferData(bool inWorld);
	void Prepass();
	void SetupRenderTargetResources();
	void SetupResources();
	void ApplyProbeGridQuality();
	const char* GetPerformanceCostMeasurementWaitText() const;
	unsigned compilations = 0;
	void CompileComputeShaders() { ++compilations; }
	Texture3D* texProbeArray = nullptr;
	Texture3D* texAccumFramesArray = nullptr;
	Texture3D* texShadowBitmask = nullptr;
	Texture3D* texShadowVisibility = nullptr;
	UINT probeArrayDims[3] = { 192, 192, 96 };
	void QueueResetSkylighting(bool rebuild = false);
	bool HasPendingReset() const;
	void EarlyPrepass();
	bool UpdateInteriorState();
	void ResetSkylighting();
};

struct ProbeGridPreset
{
	uint Width, Height, Depth;
};
uint ClampProbeGridQuality(uint quality) { return std::min(quality, 2u); }
const ProbeGridPreset& GetProbeGridPreset(uint quality)
{
	static constexpr ProbeGridPreset presets[] = { { 128, 128, 64 }, { 192, 192, 96 }, { 256, 256, 128 } };
	return presets[ClampProbeGridQuality(quality)];
}
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
	ID3D11DepthStencilView dsv;
	ID3D11Device device;
	Skylighting::SkylightingCB published{};
	Skylighting feature;
	Fixture()
	{
		Allocation::calls = Allocation::failAt = 0;
		Allocation::duringCreate = {};
		globals::d3d::device = nullptr;
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
		auto& precipitation = renderer.data.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPRECIPITATION_OCCLUSION_MAP];
		precipitation.texture = &occlusion;
		precipitation.depthSRV = &occlusion.srvObject;
		precipitation.views[0] = &dsv;
	}
	void OwnResources()
	{
		feature.texOcclusion = new Texture2D;
		feature.texProbeArray = new Texture3D;
		feature.texAccumFramesArray = new Texture3D;
		feature.texShadowBitmask = new Texture3D;
		feature.texShadowVisibility = new Texture3D;
		feature.resourceDevice = &device;
		globals::d3d::device = &device;
	}
	void ReleaseResources()
	{
		delete feature.texOcclusion;
		delete feature.texProbeArray;
		delete feature.texAccumFramesArray;
		delete feature.texShadowBitmask;
		delete feature.texShadowVisibility;
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
	for (unsigned failure = 1; failure <= 16; ++failure) {
		Fixture f;
		f.OwnResources();
		f.Ready();
		const auto* probes = f.feature.texProbeArray;
		const auto* occlusion = f.feature.texOcclusion;
		const auto liveTextures = Allocation::liveTextures;
		const auto clears = f.context.clearCount;
		f.feature.settings.ProbeGridQuality = 0;
		f.feature.QueueResetSkylighting(true);
		Allocation::failAt = failure;
		f.feature.EarlyPrepass();
		Require(f.feature.resourceRebuildFailed && f.feature.HasPendingReset(), "allocation failure must retain an explicit failed state");
		Require(!f.feature.IsRuntimeActive(), "failed rebuild must select the vanilla precipitation fallback");
		Require(f.feature.texProbeArray == probes && f.feature.texOcclusion == occlusion && f.feature.probeArrayDims[2] == 96, "partial rebuild must retain the active resources and grid");
		Require(Allocation::liveTextures == liveTextures && f.context.clearCount == clears, "partial replacements must be released without clearing active history");
		Require(std::string(f.feature.GetPerformanceCostMeasurementWaitText()).find("failed") != std::string::npos, "rebuild failure must be visible without logging");
		f.Publish();
		Require(!f.published.Enabled, "failed rebuild must block sampling");
		++f.state.frameCount;
		f.feature.EarlyPrepass();
		Require(Allocation::calls == failure, "failed allocations must not retry every frame");
		Allocation::failAt = 0;
		f.feature.QueueResetSkylighting();
		++f.state.frameCount;
		f.feature.EarlyPrepass();
		Require(!f.feature.resourceRebuildFailed && !f.feature.HasPendingReset() && f.feature.probeArrayDims[2] == 64, "fresh reset request must retry and publish the complete replacement");
		Require(Allocation::liveTextures == liveTextures && f.feature.needsOcclusionRefresh, "successful rebuild must release old resources and require fresh capture");
		f.ReleaseResources();
		++scenarios;
	}
	for (unsigned missing = 0; missing < 6; ++missing) {
		Fixture f;
		f.OwnResources();
		auto& precipitation = f.renderer.data.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPRECIPITATION_OCCLUSION_MAP];
		if (missing == 0)
			globals::d3d::device = nullptr;
		if (missing == 1)
			globals::d3d::context = nullptr;
		if (missing == 2)
			globals::game::renderer = nullptr;
		if (missing == 3)
			precipitation.texture = nullptr;
		if (missing == 4)
			precipitation.depthSRV = nullptr;
		if (missing == 5)
			precipitation.views[0] = nullptr;
		f.feature.SetupResources();
		Require(Allocation::calls == 0 && f.feature.queuedRebuildSkylighting, "missing rebuild inputs must preserve pending work without allocating");
		f.ReleaseResources();
		++scenarios;
	}
	{
		Fixture f;
		f.OwnResources();
		Allocation::duringCreate = [&] { f.feature.QueueResetSkylighting(true); };
		Allocation::failAt = 2;
		f.feature.SetupResources();
		Require(f.feature.resourceRebuildFailed && f.feature.queuedRebuildSkylighting, "request during a failed rebuild must survive");
		Allocation::failAt = 0;
		f.feature.EarlyPrepass();
		Require(!f.feature.HasPendingReset(), "retained request must recover a failed rebuild");
		Allocation::duringCreate = [&] { f.feature.QueueResetSkylighting(true); };
		f.feature.SetupResources();
		Require(f.feature.queuedRebuildSkylighting && !f.feature.resourceRebuildFailed, "request during a successful rebuild must survive publication");
		f.ReleaseResources();
		++scenarios;
	}
	{
		Fixture f;
		f.Ready();
		f.Publish();
		Require(f.published.Enabled, "fixture must publish active probes before late invalidation");
		f.feature.QueueResetSkylighting();
		f.feature.EarlyPrepass();
		Require(!f.published.Enabled && !f.feature.probeUpdateBufferEnabled && f.state.updates == 1, "early reset must invalidate already-published probe data");
		f.feature.needsOcclusionRefresh = false;
		f.feature.Prepass();
		Require(f.context.dispatchCount == 0, "fresh capture must not authorize dispatch with stale probe constants");
		f.Publish();
		f.feature.Prepass();
		Require(f.context.dispatchCount == 1, "fresh publication must resume probe dispatch");
		++scenarios;
	}
	{
		Fixture f;
		f.Ready();
		f.feature.QueueResetSkylighting(true);
		f.feature.ResetSkylighting();
		Require(!f.feature.queuedResetSkylighting && f.feature.queuedRebuildSkylighting, "history reset must leave resource rebuild pending");
		f.feature.needsOcclusionRefresh = false;
		f.Publish();
		Require(!f.published.Enabled, "pending resource rebuild must block probe sampling");
		f.feature.EarlyPrepass();
		Require(Allocation::calls == 0 && f.feature.HasPendingReset(), "missing device must retain resource rebuild");
		f.OwnResources();
		++f.state.frameCount;
		f.feature.EarlyPrepass();
		Require(Allocation::calls == 16 && !f.feature.HasPendingReset(), "resource rebuild must survive consumption of its reset flag");
		f.ReleaseResources();
		globals::d3d::device = nullptr;
		++scenarios;
	}
	{
		Fixture f;
		f.state.blocked = true;
		f.feature.EarlyPrepass();
		Require(f.context.clearCount == 0 && f.feature.queuedResetSkylighting, "load guard must retain reset");
		f.state.blocked = false;
		Util::rendererAvailable = false;
		++f.state.frameCount;
		f.feature.EarlyPrepass();
		Require(f.context.clearCount == 0 && f.feature.queuedResetSkylighting, "renderer contention must retain reset");
		Util::rendererAvailable = true;
		++f.state.frameCount;
		f.context.pixelResources[50] = &f.probes.srvObject;
		f.context.pixelResources[53] = &f.visibility.srvObject;
		f.feature.EarlyPrepass();
		Require(f.context.clearCount == 4 && !f.feature.queuedResetSkylighting, "safe frame must apply reset once");
		Require(f.context.pixelResources[50] == &f.probes.srvObject && f.context.pixelResources[53] == &f.visibility.srvObject, "reset must restore pixel bindings");
		f.feature.QueueResetSkylighting();
		f.feature.EarlyPrepass();
		Require(f.context.clearCount == 4 && f.feature.queuedResetSkylighting, "late request must wait for next frame");
		++f.state.frameCount;
		f.feature.EarlyPrepass();
		Require(f.context.clearCount == 8, "next frame must consume late reset");
		++scenarios;
	}

	{
		Fixture f;
		f.feature.QueueResetSkylighting();
		f.feature.QueueResetSkylighting();
		Require(f.context.clearCount == 0, "queueing must not touch the graphics context");
		f.feature.ResetSkylighting();
		Require(f.context.clearCount == 4, "duplicate requests must coalesce into one reset");
		Require(!f.feature.queuedResetSkylighting, "completed reset must consume its request");
		Require(f.feature.needsOcclusionRefresh, "cleared probes still require a fresh capture");
		Require(f.context.unboundSlots == std::vector<UINT>{ 50, 53, 50, 53 }, "history SRVs must be unbound before clears");
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
