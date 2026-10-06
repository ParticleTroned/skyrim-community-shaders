#pragma once

#include <array>
#include <d3d11.h>
#include <wrl/client.h>

namespace NeuralRendering::Color
{
	// Preserve the CS shader, b0, leading resource slots and predication.
	// Input preparation can also release the encoder's additional UAV bindings.
	template <UINT SRVCount, UINT UAVCount = 1>
	class ComputeStateGuard
	{
		static_assert(SRVCount > 0 && SRVCount <= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
		static_assert(UAVCount > 0 && UAVCount <= D3D11_PS_CS_UAV_REGISTER_COUNT);

	public:
		explicit ComputeStateGuard(ID3D11DeviceContext* context) noexcept : context_(context)
		{
			context_->CSGetShader(shader_.GetAddressOf(), classes_.data(), &classCount_);
			context_->CSGetShaderResources(0, SRVCount, srvs_.data());
			context_->CSGetUnorderedAccessViews(0, UAVCount, uavs_.data());
			context_->CSGetConstantBuffers(0, 1, cb_.GetAddressOf());
			context_->GetPredication(predicate_.GetAddressOf(), &predicateValue_);
			context_->SetPredication(nullptr, FALSE);
			Unbind();
		}
		ComputeStateGuard(const ComputeStateGuard&) = delete;
		ComputeStateGuard& operator=(const ComputeStateGuard&) = delete;
		~ComputeStateGuard() noexcept
		{
			Unbind();
			context_->CSSetShader(shader_.Get(), classes_.data(), classCount_);
			auto* cb = cb_.Get();
			context_->CSSetConstantBuffers(0, 1, &cb);
			context_->CSSetShaderResources(0, SRVCount, srvs_.data());
			context_->CSSetUnorderedAccessViews(0, UAVCount, uavs_.data(), nullptr);
			context_->SetPredication(predicate_.Get(), predicateValue_);
			for (UINT i = 0; i < classCount_; ++i)
				if (classes_[i])
					classes_[i]->Release();
			for (auto* view : srvs_)
				if (view)
					view->Release();
			for (auto* view : uavs_)
				if (view)
					view->Release();
		}
		void Unbind() const noexcept
		{
			std::array<ID3D11ShaderResourceView*, SRVCount> srvs{};
			std::array<ID3D11UnorderedAccessView*, UAVCount> uavs{};
			context_->CSSetShaderResources(0, SRVCount, srvs.data());
			context_->CSSetUnorderedAccessViews(0, UAVCount, uavs.data(), nullptr);
		}

	private:
		ID3D11DeviceContext* context_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader_;
		std::array<ID3D11UnorderedAccessView*, UAVCount> uavs_{};
		Microsoft::WRL::ComPtr<ID3D11Buffer> cb_;
		Microsoft::WRL::ComPtr<ID3D11Predicate> predicate_;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classes_{};
		UINT classCount_ = D3D11_SHADER_MAX_INTERFACES;
		std::array<ID3D11ShaderResourceView*, SRVCount> srvs_{};
		BOOL predicateValue_ = FALSE;
	};
}
