#include "VRMenuPointerOverlay.h"

#include "../../Utils/ResourceName.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <d3d11_1.h>
#include <limits>

namespace
{
	template <class T, size_t N>
	struct OwnedPointers
	{
		std::array<T*, N> values{};
		~OwnedPointers()
		{
			for (auto* value : values)
				if (value)
					value->Release();
		}
		bool Any() const
		{
			return std::any_of(values.begin(), values.end(), [](auto* value) { return value != nullptr; });
		}
	};

	struct CaptureState
	{
		ID3D11DeviceContext* context;
		bool modified = false;
		OwnedPointers<ID3D11RenderTargetView, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets;
		OwnedPointers<ID3D11ClassInstance, D3D11_SHADER_MAX_INTERFACES> classes;
		winrt::com_ptr<ID3D11DepthStencilView> dsv;
		winrt::com_ptr<ID3D11BlendState> blend;
		winrt::com_ptr<ID3D11DepthStencilState> depth;
		winrt::com_ptr<ID3D11PixelShader> ps;
		FLOAT blendFactor[4]{};
		UINT sampleMask = 0, stencilRef = 0, classCount = D3D11_SHADER_MAX_INTERFACES;
		UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		UINT scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
		std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};

		explicit CaptureState(ID3D11DeviceContext* value) : context(value)
		{
			context->OMGetRenderTargets(static_cast<UINT>(targets.values.size()), targets.values.data(), dsv.put());
			context->OMGetBlendState(blend.put(), blendFactor, &sampleMask);
			context->OMGetDepthStencilState(depth.put(), &stencilRef);
			context->PSGetShader(ps.put(), classes.values.data(), &classCount);
			context->RSGetViewports(&viewportCount, viewports.data());
			context->RSGetScissorRects(&scissorCount, scissors.data());
		}
		~CaptureState()
		{
			if (!modified)
				return;
			context->OMSetRenderTargets(static_cast<UINT>(targets.values.size()), targets.values.data(), dsv.get());
			context->OMSetBlendState(blend.get(), blendFactor, sampleMask);
			context->OMSetDepthStencilState(depth.get(), stencilRef);
			context->PSSetShader(ps.get(), classes.values.data(), classCount);
			context->RSSetViewports(viewportCount, viewports.data());
			context->RSSetScissorRects(scissorCount, scissors.data());
		}
	};

}

bool VRMenuPointerOverlay::EnsureResources(ID3D11Device* device, uint32_t newWidth, uint32_t newHeight, const char** reason)
{
	if (owner.get() == device && width == newWidth && height == newHeight && texture && target && layer && blend && depth)
		return true;
	// Retry a failed allocation only after a layout/device change or an explicit reset.
	if (failedOwner.get() == device && failedWidth == newWidth && failedHeight == newHeight) {
		if (reason)
			*reason = creationFailure;
		return false;
	}
	auto check = [&](HRESULT result, const char* operation) {
		if (SUCCEEDED(result))
			return true;
		failedOwner.copy_from(device);
		failedWidth = newWidth;
		failedHeight = newHeight;
		std::snprintf(creationFailure, sizeof(creationFailure), "ui-pointer-overlay-%s-failed-0x%08lX", operation, static_cast<unsigned long>(result));
		if (reason)
			*reason = creationFailure;
		return false;
	};
	winrt::com_ptr<ID3D11Texture2D> newTexture;
	winrt::com_ptr<ID3D11RenderTargetView> newTarget;
	winrt::com_ptr<ID3D11ShaderResourceView> newLayer;
	winrt::com_ptr<ID3D11BlendState> newBlend;
	winrt::com_ptr<ID3D11DepthStencilState> newDepth;
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = newWidth;
	desc.Height = newHeight;
	desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (!check(device->CreateTexture2D(&desc, nullptr, newTexture.put()), "texture"))
		return false;
	Util::SetResourceName(newTexture.get(), "Upscaling::VRMenuPointerOverlay");
	if (!check(device->CreateRenderTargetView(newTexture.get(), nullptr, newTarget.put()), "rtv"))
		return false;
	Util::SetResourceName(newTarget.get(), "Upscaling::VRMenuPointerOverlay RTV");
	if (!check(device->CreateShaderResourceView(newTexture.get(), nullptr, newLayer.put()), "srv"))
		return false;
	Util::SetResourceName(newLayer.get(), "Upscaling::VRMenuPointerOverlay SRV");
	D3D11_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (!check(device->CreateBlendState(&blendDesc, newBlend.put()), "blend"))
		return false;
	Util::SetResourceName(newBlend.get(), "Upscaling::VRMenuPointerOverlayBlend");
	D3D11_DEPTH_STENCIL_DESC depthDesc{};
	depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	if (!check(device->CreateDepthStencilState(&depthDesc, newDepth.put()), "depth"))
		return false;
	Util::SetResourceName(newDepth.get(), "Upscaling::VRMenuPointerOverlayDepth");
	owner.copy_from(device);
	failedOwner = nullptr;
	texture = std::move(newTexture);
	target = std::move(newTarget);
	layer = std::move(newLayer);
	blend = std::move(newBlend);
	depth = std::move(newDepth);
	width = newWidth;
	height = newHeight;
	captured = false;
	return true;
}

bool VRMenuPointerOverlay::Capture(ID3D11DeviceContext* context, ID3D11PixelShader* capturePS, const DrawArguments& draw,
	uint32_t renderWidth, uint32_t renderHeight, uint32_t displayWidth, uint32_t displayHeight,
	uint32_t frame, uint32_t generation, float offsetX, float offsetY, const char** reason)
{
	if (!attempted || capturedFrame != frame || capturedGeneration != generation) {
		attempted = true;
		capturedFrame = frame;
		capturedGeneration = generation;
		captured = failed = false;
	}
	const bool previouslyFailed = failed;
	// Exceptions or rejected draws invalidate the whole frame, including earlier captures.
	failed = true;
	auto reject = [&](const char* message) {
		failed = true;
		if (reason)
			*reason = message;
		return false;
	};
	if (previouslyFailed)
		return reject("ui-pointer-overlay-frame-invalid");
	if (!context || !capturePS || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
		return reject("ui-pointer-overlay-invalid-context-shader");
	constexpr uint32_t maxDimension = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	if (!renderWidth || !renderHeight || !displayWidth || !displayHeight ||
		renderWidth > maxDimension || renderHeight > maxDimension || displayWidth > maxDimension || displayHeight > maxDimension ||
		!std::isfinite(offsetX) || !std::isfinite(offsetY) || std::abs(offsetX) > displayWidth || std::abs(offsetY) > displayHeight)
		return reject("ui-pointer-overlay-invalid-dimensions");
	if (!draw.indexCount || draw.indexCount > 65536 || !draw.instanceCount || draw.instanceCount > 2 ||
		(!draw.instanced && draw.instanceCount != 1) || draw.startIndex > std::numeric_limits<UINT>::max() - draw.indexCount ||
		draw.startInstance > std::numeric_limits<UINT>::max() - draw.instanceCount)
		return reject("ui-pointer-overlay-invalid-draw");
	winrt::com_ptr<ID3D11Device> device, shaderDevice;
	context->GetDevice(device.put());
	capturePS->GetDevice(shaderDevice.put());
	if (!device || device != shaderDevice)
		return reject("ui-pointer-overlay-foreign-shader");
	if (captured && (owner != device || width != displayWidth || height != displayHeight))
		return reject("ui-pointer-overlay-frame-layout-changed");
	D3D11_PRIMITIVE_TOPOLOGY topology{};
	context->IAGetPrimitiveTopology(&topology);
	winrt::com_ptr<ID3D11VertexShader> vs;
	winrt::com_ptr<ID3D11HullShader> hs;
	winrt::com_ptr<ID3D11DomainShader> ds;
	winrt::com_ptr<ID3D11GeometryShader> gs;
	context->VSGetShader(vs.put(), nullptr, nullptr);
	context->HSGetShader(hs.put(), nullptr, nullptr);
	context->DSGetShader(ds.put(), nullptr, nullptr);
	context->GSGetShader(gs.put(), nullptr, nullptr);
	if (!vs || hs || ds || gs || topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
		return reject("ui-pointer-overlay-unsupported-pipeline");
	OwnedPointers<ID3D11UnorderedAccessView, D3D11_1_UAV_SLOT_COUNT> outputUAVs;
	OwnedPointers<ID3D11Buffer, D3D11_SO_BUFFER_SLOT_COUNT> streamOutputs;
	const UINT uavCount = device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT;
	context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, uavCount, outputUAVs.values.data());
	context->SOGetTargets(static_cast<UINT>(streamOutputs.values.size()), streamOutputs.values.data());
	winrt::com_ptr<ID3D11Predicate> predicate;
	BOOL predicateValue = FALSE;
	context->GetPredication(predicate.put(), &predicateValue);
	if (outputUAVs.Any() || streamOutputs.Any() || predicate)
		return reject("ui-pointer-overlay-draw-side-effects");
	CaptureState previous(context);
	if (previous.viewportCount != 1)
		return reject("ui-pointer-overlay-unsupported-viewports");
	auto viewport = previous.viewports[0];
	if (!std::isfinite(viewport.TopLeftX) || !std::isfinite(viewport.TopLeftY) || !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height) ||
		!std::isfinite(viewport.MinDepth) || !std::isfinite(viewport.MaxDepth) || viewport.MinDepth < 0 || viewport.MaxDepth > 1 || viewport.MinDepth > viewport.MaxDepth ||
		viewport.TopLeftX < 0 || viewport.TopLeftY < 0 || viewport.Width <= 0 || viewport.Height <= 0 ||
		viewport.TopLeftX + viewport.Width > renderWidth || viewport.TopLeftY + viewport.Height > renderHeight)
		return reject("ui-pointer-overlay-invalid-viewport");
	const float scaleX = static_cast<float>(displayWidth) / renderWidth;
	const float scaleY = static_cast<float>(displayHeight) / renderHeight;
	viewport.TopLeftX = viewport.TopLeftX * scaleX + offsetX;
	viewport.TopLeftY = viewport.TopLeftY * scaleY + offsetY;
	viewport.Width *= scaleX;
	viewport.Height *= scaleY;
	if (viewport.TopLeftX + viewport.Width > D3D11_VIEWPORT_BOUNDS_MAX || viewport.TopLeftY + viewport.Height > D3D11_VIEWPORT_BOUNDS_MAX)
		return reject("ui-pointer-overlay-invalid-transformed-viewport");
	auto scissors = previous.scissors;
	for (UINT i = 0; i < previous.scissorCount; ++i) {
		if (scissors[i].left > scissors[i].right || scissors[i].top > scissors[i].bottom)
			return reject("ui-pointer-overlay-invalid-scissor");
		auto lower = [](LONG value, float scale, float offset, uint32_t extent) { return static_cast<LONG>(std::clamp(std::floor(static_cast<double>(value) * scale + offset), 0.0, static_cast<double>(extent))); };
		auto upper = [](LONG value, float scale, float offset, uint32_t extent) { return static_cast<LONG>(std::clamp(std::ceil(static_cast<double>(value) * scale + offset), 0.0, static_cast<double>(extent))); };
		scissors[i] = { lower(scissors[i].left, scaleX, offsetX, displayWidth), lower(scissors[i].top, scaleY, offsetY, displayHeight),
			upper(scissors[i].right, scaleX, offsetX, displayWidth), upper(scissors[i].bottom, scaleY, offsetY, displayHeight) };
	}
	if (!EnsureResources(device.get(), displayWidth, displayHeight, reason)) {
		failed = true;
		return false;
	}
	if (!captured) {
		constexpr FLOAT transparent[4]{};
		context->ClearRenderTargetView(target.get(), transparent);
	}
	previous.modified = true;
	auto* output = target.get();
	context->OMSetRenderTargets(1, &output, nullptr);
	constexpr FLOAT factor[4]{};
	context->OMSetBlendState(blend.get(), factor, UINT_MAX);
	context->OMSetDepthStencilState(depth.get(), 0);
	context->PSSetShader(capturePS, nullptr, 0);
	context->RSSetViewports(1, &viewport);
	context->RSSetScissorRects(previous.scissorCount, scissors.data());
	if (draw.instanced)
		context->DrawIndexedInstanced(draw.indexCount, draw.instanceCount, draw.startIndex, draw.baseVertex, draw.startInstance);
	else
		context->DrawIndexed(draw.indexCount, draw.startIndex, draw.baseVertex);
	captured = true;
	failed = false;
	if (reason)
		*reason = "ui-pointer-overlay-captured";
	return true;
}

ID3D11ShaderResourceView* VRMenuPointerOverlay::GetLayer(uint32_t frame, uint32_t generation) const noexcept
{
	return captured && !failed && capturedFrame == frame && capturedGeneration == generation ? layer.get() : nullptr;
}

void VRMenuPointerOverlay::Invalidate(uint32_t frame, uint32_t generation) noexcept
{
	capturedFrame = frame;
	capturedGeneration = generation;
	attempted = failed = true;
	captured = false;
}

void VRMenuPointerOverlay::Reset() noexcept
{
	owner = nullptr;
	failedOwner = nullptr;
	texture = nullptr;
	target = nullptr;
	layer = nullptr;
	blend = nullptr;
	depth = nullptr;
	width = height = capturedFrame = capturedGeneration = 0;
	attempted = captured = failed = false;
}

VRMenuPointerOverlay::CompositeBinding::CompositeBinding(ID3D11DeviceContext* value, ID3D11ShaderResourceView* layer)
{
	if (value && layer) {
		context = value;
		context->PSGetShaderResources(1, 1, previous.put());
		context->PSSetShaderResources(1, 1, &layer);
	}
}

VRMenuPointerOverlay::CompositeBinding::~CompositeBinding()
{
	if (context) {
		auto* resource = previous.get();
		context->PSSetShaderResources(1, 1, &resource);
	}
}
