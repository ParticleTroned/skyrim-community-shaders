#include "GrassHiZ.h"
#include "Globals.h"
#include "GpuPass.h"
#include "GrassD3DState.h"
#include "Util.h"

#include <algorithm>
#include <bit>
#include <limits>

namespace
{
	class OutputState
	{
	public:
		explicit OutputState(ID3D11DeviceContext* context) : ctx(context)
		{
			std::array<ID3D11RenderTargetView*, 8> raw{};
			ctx->OMGetRenderTargets(8, raw.data(), depth.put());
			for (size_t i = 0; i < raw.size(); ++i) targets[i].attach(raw[i]);
			std::array<ID3D11UnorderedAccessView*, 8> rawUAVs{};
			ctx->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 8, rawUAVs.data());
			for (size_t i = 0; i < rawUAVs.size(); ++i) uavs[i].attach(rawUAVs[i]);
			rawUAVs.fill(nullptr);
			ctx->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 8, rawUAVs.data(), nullptr);
		}
		~OutputState()
		{
			std::array<ID3D11RenderTargetView*, 8> raw{};
			UINT count = 0;
			for (UINT i = 0; i < raw.size(); ++i) {
				raw[i] = targets[i].get();
				if (raw[i])
					count = i + 1;
			}
			std::array<ID3D11UnorderedAccessView*, 8> rawUAVs{};
			for (size_t i = 0; i < rawUAVs.size(); ++i) rawUAVs[i] = uavs[i].get();
			ctx->OMSetRenderTargetsAndUnorderedAccessViews(count, raw.data(), depth.get(), count,
				8 - count, count < 8 ? rawUAVs.data() + count : nullptr, nullptr);
		}
		OutputState(const OutputState&) = delete;
		OutputState& operator=(const OutputState&) = delete;

	private:
		ID3D11DeviceContext* ctx;
		std::array<winrt::com_ptr<ID3D11RenderTargetView>, 8> targets;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 8> uavs;
		winrt::com_ptr<ID3D11DepthStencilView> depth;
	};
}

void GrassHiZ::SetupResources()
{
	pyramid.SetupResources();
}
void GrassHiZ::Reset()
{
	pyramid.Reset();
	builtFrame = UINT32_MAX;
	sourceIdentity = nullptr;
	depthViewIdentity = nullptr;
	depthView = nullptr;
	depthViewportSlack = 0;
}
#ifdef DEVBENCH_BRIDGE_ENABLED
bool GrassHiZ::RecordFailure(Failure reason)
{
	if (diagnostics.load(std::memory_order_relaxed)) {
		lastFailure.store(reason, std::memory_order_relaxed);
		++failures[size_t(reason)];
	}
	return false;
}
std::array<uint64_t, size_t(GrassHiZ::Failure::Count)> GrassHiZ::FailureCounts() const
{
	std::array<uint64_t, size_t(Failure::Count)> result{};
	for (size_t i = 0; i < result.size(); ++i)
		result[i] = failures[i].load(std::memory_order_relaxed);
	return result;
}
void GrassHiZ::ObserveDepthState(const D3D11_DEPTH_STENCIL_DESC& state)
{
	if (!diagnostics.load(std::memory_order_relaxed))
		return;
	observed[0].store(state.DepthEnable, std::memory_order_relaxed);
	observed[1].store(state.DepthFunc, std::memory_order_relaxed);
}
void GrassHiZ::ObserveViewport(const D3D11_VIEWPORT& viewport, uint32_t count, const D3D11_TEXTURE2D_DESC& source)
{
	if (!diagnostics.load(std::memory_order_relaxed))
		return;
	observed[2].store(count, std::memory_order_relaxed);
	observed[3].store(std::bit_cast<uint32_t>(viewport.TopLeftX), std::memory_order_relaxed);
	observed[4].store(std::bit_cast<uint32_t>(viewport.TopLeftY), std::memory_order_relaxed);
	observed[5].store(std::bit_cast<uint32_t>(viewport.Width), std::memory_order_relaxed);
	observed[6].store(std::bit_cast<uint32_t>(viewport.Height), std::memory_order_relaxed);
	observed[7].store(std::bit_cast<uint32_t>(viewport.MinDepth), std::memory_order_relaxed);
	observed[8].store(std::bit_cast<uint32_t>(viewport.MaxDepth), std::memory_order_relaxed);
	observed[9].store(source.Width, std::memory_order_relaxed);
	observed[10].store(source.Height, std::memory_order_relaxed);
}
GrassHiZ::FailureObservation GrassHiZ::ObservedFailures() const
{
	const auto read = [&](size_t index) { return observed[index].load(std::memory_order_relaxed); };
	return { read(0), read(1), read(2), read(9), read(10),
		std::bit_cast<float>(read(3)), std::bit_cast<float>(read(4)),
		std::bit_cast<float>(read(5)), std::bit_cast<float>(read(6)),
		std::bit_cast<float>(read(7)), std::bit_cast<float>(read(8)) };
}
#	define GRASS_HIZ_FAIL(reason) return RecordFailure(Failure::reason)
#else
#	define GRASS_HIZ_FAIL(reason) return false
#endif
void GrassHiZ::Allocate(uint32_t nextWidth, uint32_t nextHeight, uint32_t eyes)
{
	pyramid.Allocate(nextWidth, nextHeight, eyes);
	width = nextWidth;
	height = nextHeight;
	mipCount = pyramid.Mips();
	builtFrame = UINT32_MAX;
}

bool GrassHiZ::Build(ID3D11DeviceContext1* context, uint32_t frame)
{
	auto& depth = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto source = reinterpret_cast<ID3D11ShaderResourceView*>(depth.depthSRV);
	winrt::com_ptr<ID3D11Resource> sourceResource;
	if (source)
		source->GetResource(sourceResource.put());
	winrt::com_ptr<ID3D11DepthStencilView> bound;
	context->OMGetRenderTargets(0, nullptr, bound.put());
	if (!bound)
		GRASS_HIZ_FAIL(MissingTarget);
	D3D11_DEPTH_STENCIL_VIEW_DESC boundView{};
	bound->GetDesc(&boundView);
	if (boundView.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D || boundView.Texture2D.MipSlice != 0)
		GRASS_HIZ_FAIL(TargetView);
	winrt::com_ptr<ID3D11Resource> boundResource;
	bound->GetResource(boundResource.put());
	if (sourceResource != boundResource) {
		if (boundResource.get() != reinterpret_cast<ID3D11Resource*>(depth.texture))
			GRASS_HIZ_FAIL(TargetMismatch);
		if (depthViewIdentity != boundResource || !depthView) {
			winrt::com_ptr<ID3D11Texture2D> mainTexture;
			if (FAILED(boundResource->QueryInterface(__uuidof(ID3D11Texture2D), mainTexture.put_void())))
				GRASS_HIZ_FAIL(TargetFormat);
			D3D11_TEXTURE2D_DESC mainDesc{};
			mainTexture->GetDesc(&mainDesc);
			D3D11_SHADER_RESOURCE_VIEW_DESC mainView{};
			mainView.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			mainView.Texture2D.MipLevels = 1;
			if (mainDesc.Format == DXGI_FORMAT_R32_TYPELESS)
				mainView.Format = DXGI_FORMAT_R32_FLOAT;
			else if (mainDesc.Format == DXGI_FORMAT_R24G8_TYPELESS)
				mainView.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			else
				GRASS_HIZ_FAIL(TargetFormat);
			if (!(mainDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) || mainDesc.SampleDesc.Count != 1 || mainDesc.ArraySize != 1)
				GRASS_HIZ_FAIL(TargetLayout);
			winrt::com_ptr<ID3D11ShaderResourceView> next;
			if (FAILED(globals::d3d::device->CreateShaderResourceView(mainTexture.get(), &mainView, next.put())))
				GRASS_HIZ_FAIL(TargetSRV);
			Util::SetResourceName(next.get(), "GrassOptimizations::MainDepth SRV");
			depthView = std::move(next);
			depthViewIdentity = boundResource;
		}
		source = depthView.get();
		sourceResource = boundResource;
	}
	winrt::com_ptr<ID3D11Texture2D> sourceTexture;
	if (FAILED(sourceResource->QueryInterface(__uuidof(ID3D11Texture2D), sourceTexture.put_void())))
		GRASS_HIZ_FAIL(SourceTexture);
	D3D11_TEXTURE2D_DESC desc{};
	sourceTexture->GetDesc(&desc);
	D3D11_SHADER_RESOURCE_VIEW_DESC sourceView{};
	source->GetDesc(&sourceView);
	winrt::com_ptr<ID3D11DepthStencilState> state;
	UINT reference = 0;
	context->OMGetDepthStencilState(state.put(), &reference);
	if (state) {
		D3D11_DEPTH_STENCIL_DESC policy{};
		state->GetDesc(&policy);
		if (!policy.DepthEnable || (policy.DepthFunc != D3D11_COMPARISON_LESS &&
									   policy.DepthFunc != D3D11_COMPARISON_LESS_EQUAL && policy.DepthFunc != D3D11_COMPARISON_EQUAL)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			ObserveDepthState(policy);
#endif
			GRASS_HIZ_FAIL(DepthState);
		}
	}
	D3D11_VIEWPORT viewport{};
	UINT count = 1;
	context->RSGetViewports(&count, &viewport);
	const UINT eyes = globals::game::isVR ? 2 : 1;
	if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || sourceView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
		sourceView.Texture2D.MostDetailedMip != 0)
		GRASS_HIZ_FAIL(SourceLayout);
	if (count != 1 || viewport.TopLeftX != 0 || viewport.TopLeftY != 0 || !std::isfinite(viewport.Width) ||
		!std::isfinite(viewport.Height) || viewport.Width < eyes || viewport.Height < 2 ||
		viewport.Width > desc.Width || viewport.Height > desc.Height || viewport.MinDepth != 0 ||
		!std::isfinite(viewport.MaxDepth) || viewport.MaxDepth < 0.9999f || viewport.MaxDepth > 1) {
#ifdef DEVBENCH_BRIDGE_ENABLED
		ObserveViewport(viewport, count, desc);
#endif
		GRASS_HIZ_FAIL(Viewport);
	}
	if (desc.Width % eyes)
		GRASS_HIZ_FAIL(Extent);
	const UINT currentWidth = UINT(viewport.Width), currentHeight = UINT(viewport.Height);
	if (currentWidth % eyes || currentWidth != viewport.Width || currentHeight != viewport.Height)
		GRASS_HIZ_FAIL(Extent);
	if (builtFrame == frame && sourceIdentity == sourceResource && activeWidth == currentWidth && activeHeight == currentHeight) {
		depthViewportSlack = std::max(depthViewportSlack, 1.0f - viewport.MaxDepth + 4 * std::numeric_limits<float>::epsilon());
		return true;
	}
	const UINT nextWidth = std::bit_ceil((desc.Width / eyes + 1) / 2) * eyes;
	const UINT nextHeight = std::bit_ceil((desc.Height + 1) / 2);
	if (nextWidth > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || nextHeight > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
		GRASS_HIZ_FAIL(Extent);
	if (width != nextWidth || height != nextHeight || !pyramid.Allocated())
		Allocate(nextWidth, nextHeight, eyes);
#if defined(DEVBENCH_BRIDGE_ENABLED) || defined(TRACY_SUPPORT)
	CS_GPU_PASS("GrassOptimizations::BuildHiZ");
#endif
	OutputState restoreOutputs(context);
	if (!pyramid.Build(context, source, desc.Width, desc.Height, currentWidth, currentHeight, eyes))
		GRASS_HIZ_FAIL(Resources);

	scale = { float(currentWidth) / (width * 2), float(currentHeight) / (height * 2), 0, 0 };
	depthViewportSlack = 1.0f - viewport.MaxDepth + 4 * std::numeric_limits<float>::epsilon();
	sourceIdentity = sourceResource;
	activeWidth = currentWidth;
	activeHeight = currentHeight;
	builtFrame = frame;
	return true;
}
#undef GRASS_HIZ_FAIL
