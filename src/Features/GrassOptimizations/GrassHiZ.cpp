#include "GrassHiZ.h"
#include "Globals.h"
#include "GpuPass.h"
#include "GrassD3DState.h"
#include "Util.h"

#include <bit>

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
	constants = std::make_unique<Buffer>(ConstantBufferDesc(32), nullptr, "GrassOptimizations::DepthParameters");
	shader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassDepthCS.hlsl", {}, "cs_5_0", "main", "GrassOptimizations::DepthCS");
}
void GrassHiZ::Reset()
{
	shader.Reset();
	builtFrame = UINT32_MAX;
	sourceIdentity = nullptr;
	depthViewIdentity = nullptr;
	depthView = nullptr;
}
void GrassHiZ::Allocate(uint32_t nextWidth, uint32_t nextHeight, uint32_t eyes)
{
	const UINT levels = UINT(std::min(std::bit_width(nextWidth / eyes), std::bit_width(nextHeight)));
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = nextWidth;
	desc.Height = nextHeight;
	desc.MipLevels = levels;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	winrt::com_ptr<ID3D11Texture2D> next;
	DX::ThrowIfFailed(globals::d3d::device->CreateTexture2D(&desc, nullptr, next.put()));
	Util::SetResourceName(next.get(), "GrassOptimizations::DepthPyramid");
	winrt::com_ptr<ID3D11ShaderResourceView> nextSRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC view{};
	view.Format = desc.Format;
	view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	view.Texture2D.MipLevels = levels;
	DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(next.get(), &view, nextSRV.put()));
	Util::SetResourceName(nextSRV.get(), "GrassOptimizations::DepthPyramid SRV");
	std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> nextMipSRVs(levels);
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> nextMipUAVs(levels);
	for (UINT mip = 0; mip < levels; ++mip) {
		view.Texture2D.MostDetailedMip = mip;
		view.Texture2D.MipLevels = 1;
		DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(next.get(), &view, nextMipSRVs[mip].put()));
		Util::SetResourceName(nextMipSRVs[mip].get(), "GrassOptimizations::DepthMip%u SRV", mip);
		D3D11_UNORDERED_ACCESS_VIEW_DESC output{};
		output.Format = desc.Format;
		output.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		output.Texture2D.MipSlice = mip;
		DX::ThrowIfFailed(globals::d3d::device->CreateUnorderedAccessView(next.get(), &output, nextMipUAVs[mip].put()));
		Util::SetResourceName(nextMipUAVs[mip].get(), "GrassOptimizations::DepthMip%u UAV", mip);
	}
	texture = std::move(next);
	srv = std::move(nextSRV);
	mipSRVs = std::move(nextMipSRVs);
	mipUAVs = std::move(nextMipUAVs);
	width = nextWidth;
	height = nextHeight;
	mipCount = levels;
	builtFrame = UINT32_MAX;
}
bool GrassHiZ::Build(ID3D11DeviceContext1* context, uint32_t frame)
{
	auto& depth = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto source = reinterpret_cast<ID3D11ShaderResourceView*>(depth.depthSRV);
	if (!constants || !shader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassDepthCS.hlsl", {}, "cs_5_0", "main", "GrassOptimizations::DepthCS"))
		return false;
	winrt::com_ptr<ID3D11Resource> sourceResource;
	if (source)
		source->GetResource(sourceResource.put());
	winrt::com_ptr<ID3D11DepthStencilView> bound;
	context->OMGetRenderTargets(0, nullptr, bound.put());
	if (!bound)
		return false;
	D3D11_DEPTH_STENCIL_VIEW_DESC boundView{};
	bound->GetDesc(&boundView);
	if (boundView.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D || boundView.Texture2D.MipSlice != 0)
		return false;
	winrt::com_ptr<ID3D11Resource> boundResource;
	bound->GetResource(boundResource.put());
	if (sourceResource != boundResource) {
		if (boundResource.get() != reinterpret_cast<ID3D11Resource*>(depth.texture))
			return false;
		if (depthViewIdentity != boundResource || !depthView) {
			winrt::com_ptr<ID3D11Texture2D> mainTexture;
			if (FAILED(boundResource->QueryInterface(__uuidof(ID3D11Texture2D), mainTexture.put_void())))
				return false;
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
				return false;
			if (!(mainDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) || mainDesc.SampleDesc.Count != 1 || mainDesc.ArraySize != 1)
				return false;
			winrt::com_ptr<ID3D11ShaderResourceView> next;
			if (FAILED(globals::d3d::device->CreateShaderResourceView(mainTexture.get(), &mainView, next.put())))
				return false;
			Util::SetResourceName(next.get(), "GrassOptimizations::MainDepth SRV");
			depthView = std::move(next);
			depthViewIdentity = boundResource;
		}
		source = depthView.get();
		sourceResource = boundResource;
	}
	winrt::com_ptr<ID3D11Texture2D> sourceTexture;
	if (FAILED(sourceResource->QueryInterface(__uuidof(ID3D11Texture2D), sourceTexture.put_void())))
		return false;
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
		if (!policy.DepthEnable || (policy.DepthFunc != D3D11_COMPARISON_LESS && policy.DepthFunc != D3D11_COMPARISON_LESS_EQUAL))
			return false;
	}
	D3D11_VIEWPORT viewport{};
	UINT count = 1;
	context->RSGetViewports(&count, &viewport);
	const UINT eyes = globals::game::isVR ? 2 : 1;
	if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || sourceView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
		sourceView.Texture2D.MostDetailedMip != 0 || !count || viewport.TopLeftX != 0 || viewport.TopLeftY != 0 ||
		!std::isfinite(viewport.Width) || !std::isfinite(viewport.Height) || viewport.Width < eyes || viewport.Height < 2 ||
		viewport.Width > desc.Width || viewport.Height > desc.Height || desc.Width % eyes || viewport.MinDepth != 0 || viewport.MaxDepth != 1)
		return false;
	const UINT currentWidth = UINT(viewport.Width), currentHeight = UINT(viewport.Height);
	if (currentWidth % eyes || currentWidth != viewport.Width || currentHeight != viewport.Height)
		return false;
	if (builtFrame == frame && sourceIdentity == sourceResource && activeWidth == currentWidth && activeHeight == currentHeight)
		return true;
	const UINT nextWidth = std::bit_ceil((desc.Width / eyes + 1) / 2) * eyes;
	const UINT nextHeight = std::bit_ceil((desc.Height + 1) / 2);
	if (nextWidth > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || nextHeight > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
		return false;
	if (width != nextWidth || height != nextHeight || !texture)
		Allocate(nextWidth, nextHeight, eyes);
#if defined(DEVBENCH_BRIDGE_ENABLED) || defined(TRACY_SUPPORT)
	CS_GPU_PASS("GrassOptimizations::BuildHiZ");
#endif
	GrassD3D::ComputeState restore(context);
	OutputState restoreOutputs(context);
	for (UINT mip = 0; mip < mipCount; ++mip) {
		std::array<UINT, 8> params{ std::max(width >> mip, eyes), std::max(height >> mip, 1u),
			mip ? std::max(width >> (mip - 1), eyes) : desc.Width, mip ? std::max(height >> (mip - 1), 1u) : desc.Height,
			eyes, mip == 0 ? 1u : 0u, currentWidth, currentHeight };
		D3D11_MAPPED_SUBRESOURCE mapped{};
		DX::ThrowIfFailed(context->Map(constants->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		std::memcpy(mapped.pData, params.data(), sizeof(params));
		context->Unmap(constants->resource.get(), 0);
		auto cb = constants->resource.get();
		auto input = mip ? mipSRVs[mip - 1].get() : source;
		auto output = mipUAVs[mip].get();
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetShaderResources(0, 1, &input);
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		context->CSSetShader(shader.get(), nullptr, 0);
		context->Dispatch((params[0] + 7) / 8, (params[1] + 7) / 8, 1);
		ID3D11ShaderResourceView* noInput = nullptr;
		ID3D11UnorderedAccessView* noOutput = nullptr;
		context->CSSetShaderResources(0, 1, &noInput);
		context->CSSetUnorderedAccessViews(0, 1, &noOutput, nullptr);
	}
	scale = { float(currentWidth) / (width * 2), float(currentHeight) / (height * 2), 0, 0 };
	sourceIdentity = sourceResource;
	activeWidth = currentWidth;
	activeHeight = currentHeight;
	builtFrame = frame;
	return true;
}
