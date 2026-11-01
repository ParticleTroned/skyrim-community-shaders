#include "MonoCoverage.h"
#include "Utils/ResourceName.h"
#include <cstring>
#include <utility>

namespace NeuralRendering
{
	bool MonoCoverage::Initialize(ID3D11Device* device)
	{
		if (!device) return false;
		if (flags_) return true;
		D3D11_BUFFER_DESC description{};
		description.ByteWidth = 16;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		Microsoft::WRL::ComPtr<ID3D11Buffer> flags;
		if (FAILED(device->CreateBuffer(&description, nullptr, &flags))) return false;
		Util::SetResourceName(flags.Get(), "NeuralRendering::PackedCoverage");
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = DXGI_FORMAT_R32_TYPELESS; srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		srv.BufferEx.NumElements = 4; srv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = DXGI_FORMAT_R32_TYPELESS; uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.NumElements = 4; uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		if (FAILED(device->CreateShaderResourceView(flags.Get(), &srv, &input_)) ||
			FAILED(device->CreateUnorderedAccessView(flags.Get(), &uav, &output_))) return false;
		Util::SetResourceName(input_.Get(), "NeuralRendering::PackedCoverage SRV");
		Util::SetResourceName(output_.Get(), "NeuralRendering::PackedCoverage UAV");
		description.Usage = D3D11_USAGE_STAGING; description.BindFlags = description.MiscFlags = 0;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		for (auto& readback : readbacks_) {
			if (FAILED(device->CreateBuffer(&description, nullptr, &readback.staging))) return false;
			Util::SetResourceName(readback.staging.Get(), "NeuralRendering::PackedCoverageReadback");
			const D3D11_QUERY_DESC query{ D3D11_QUERY_EVENT, 0 };
			if (FAILED(device->CreateQuery(&query, &readback.ready))) return false;
			Util::SetResourceName(readback.ready.Get(), "NeuralRendering::PackedCoverageReady");
		}
		flags_ = std::move(flags);
		return true;
	}
	void MonoCoverage::Clear(ID3D11DeviceContext* context) const
	{
		const UINT clear[4]{};
		context->ClearUnorderedAccessViewUint(output_.Get(), clear);
	}
	void MonoCoverage::QueueReadback(ID3D11DeviceContext* context, std::uint32_t frame)
	{
		for (auto& readback : readbacks_) {
			if (readback.pending) continue;
			context->CopyResource(readback.staging.Get(), flags_.Get());
			context->End(readback.ready.Get());
			readback.frame = frame; readback.pending = true;
			return;
		}
	}
	std::optional<MonoCoverage::Proof> MonoCoverage::Poll(ID3D11DeviceContext* context)
	{
		std::optional<Proof> proof;
		for (auto& readback : readbacks_) {
			if (!readback.pending) continue;
			const auto result = context->GetData(readback.ready.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (result == S_FALSE) continue;
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const auto mappedResult = SUCCEEDED(result) ? context->Map(readback.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped) : result;
			if (mappedResult == DXGI_ERROR_WAS_STILL_DRAWING) continue;
			readback.pending = false;
			std::uint32_t uncovered = 1;
			if (SUCCEEDED(mappedResult)) {
				std::memcpy(&uncovered, mapped.pData, sizeof(uncovered));
				context->Unmap(readback.staging.Get(), 0);
			}
			if (!proof || static_cast<std::int32_t>(readback.frame - proof->frame) > 0)
				proof = Proof{ readback.frame, uncovered == 0 };
		}
		return proof;
	}
	void MonoCoverage::Abandon() noexcept
	{
		(void)flags_.Detach(); (void)input_.Detach(); (void)output_.Detach();
		for (auto& readback : readbacks_) { (void)readback.staging.Detach(); (void)readback.ready.Detach(); }
	}
}
