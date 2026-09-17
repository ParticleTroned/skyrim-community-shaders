#pragma once

#include <array>
#include <d3d11.h>
#include <wrl/client.h>

namespace NeuralRendering::Color
{
	// All colour/exposure passes touch CS shader, b0, u0, the leading SRVs and
	// predication. One guard prevents the capture and processing paths drifting.
	template <UINT SRVCount>
	class ComputeStateGuard
	{
		static_assert(SRVCount > 0 && SRVCount <= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

	public:
		explicit ComputeStateGuard(ID3D11DeviceContext* context) noexcept : context_(context)
		{
			context_->CSGetShader(shader_.GetAddressOf(), classes_.data(), &classCount_);
			context_->CSGetShaderResources(0, SRVCount, srvs_.data());
			context_->CSGetUnorderedAccessViews(0, 1, uav_.GetAddressOf());
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
			auto* uav = uav_.Get();
			context_->CSSetConstantBuffers(0, 1, &cb);
			context_->CSSetShaderResources(0, SRVCount, srvs_.data());
			context_->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context_->SetPredication(predicate_.Get(), predicateValue_);
			for (UINT i = 0; i < classCount_; ++i)
				if (classes_[i])
					classes_[i]->Release();
			for (auto* view : srvs_)
				if (view)
					view->Release();
		}
		void Unbind() const noexcept
		{
			std::array<ID3D11ShaderResourceView*, SRVCount> srvs{};
			ID3D11UnorderedAccessView* uav = nullptr;
			context_->CSSetShaderResources(0, SRVCount, srvs.data());
			context_->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		}

	private:
		ID3D11DeviceContext* context_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader_;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> cb_;
		Microsoft::WRL::ComPtr<ID3D11Predicate> predicate_;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classes_{};
		UINT classCount_ = D3D11_SHADER_MAX_INTERFACES;
		std::array<ID3D11ShaderResourceView*, SRVCount> srvs_{};
		BOOL predicateValue_ = FALSE;
	};
}
