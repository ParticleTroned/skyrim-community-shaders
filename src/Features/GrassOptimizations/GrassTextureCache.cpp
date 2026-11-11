#include "GrassTextureCache.h"

#include "GpuPass.h"
#include "Utils/ComputeState.h"

#include <exception>
#include <utility>

ID3D11ShaderResourceView* GrassTextureCache::Get(ID3D11DeviceContext1* context, ID3D11ShaderResourceView* source, uint32_t frame)
{
	if (!source)
		return nullptr;
	if (auto found = entries.find(source); found != entries.end()) {
		found->second.lastFrame = frame;
		return found->second.normalLength ? found->second.normalLength->srv.get() : nullptr;
	}
	if (failed || entries.size() >= kMaxTextures)
		return nullptr;
	try {
		auto& entry = entries.try_emplace(source).first->second;
		entry.source.copy_from(source);
		entry.lastFrame = frame;
		D3D11_SHADER_RESOURCE_VIEW_DESC view{};
		source->GetDesc(&view);
		if (view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
			return nullptr;
		winrt::com_ptr<ID3D11Resource> resource;
		source->GetResource(resource.put());
		auto texture = resource.try_as<ID3D11Texture2D>();
		if (!texture)
			return nullptr;
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		// Rendered and CPU-writable textures can change without replacing their SRV.
		if ((desc.Usage != D3D11_USAGE_DEFAULT && desc.Usage != D3D11_USAGE_IMMUTABLE) || desc.CPUAccessFlags ||
			(desc.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS)))
			return nullptr;
		auto compute = shader.Get(L"Data\\Shaders\\GrassOptimizations\\DetectComplexCS.hlsl", {},
			"cs_5_0", "main", "GrassOptimizations::DetectComplexCS");
		if (!compute) {
			failed = true;
			logger::warn("Grass texture classification unavailable; retaining pixel classification");
			return nullptr;
		}
		auto result = std::make_unique<Buffer>(StructuredBufferDesc<float>(uint64_t{ 1 }), nullptr, "GrassOptimizations::TextureNormalLength");
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srv.Buffer.NumElements = 1;
		result->CreateSRV(srv);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.NumElements = 1;
		result->CreateUAV(uav);
		{
			D3DState::ComputeState restore(context, 1, 1);
			CS_GPU_PASS("GrassOptimizations::ClassifyTexture");
			context->CSSetShader(compute, nullptr, 0);
			context->CSSetShaderResources(0, 1, &source);
			auto output = result->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->Dispatch(1, 1, 1);
		}
		entry.normalLength = std::move(result);
		return entry.normalLength->srv.get();
	} catch (const std::exception& error) {
		failed = true;
		logger::warn("Grass texture cache unavailable; retaining pixel classification: {}", error.what());
		return nullptr;
	}
}

void GrassTextureCache::EndFrame(uint32_t frame)
{
	std::erase_if(entries, [frame](const auto& item) { return uint32_t(frame - item.second.lastFrame) > 1; });
}

void GrassTextureCache::Reset()
{
	entries.clear();
	shader.Reset();
	failed = false;
}
