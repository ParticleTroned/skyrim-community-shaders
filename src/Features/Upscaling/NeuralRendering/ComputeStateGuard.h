#pragma once

#include <array>
#include <d3d11.h>
#include <wrl/client.h>

namespace NeuralRendering
{
	enum class ComputeStatePolicy
	{
		PreserveBindings,
		ClearBindingsAndPredication
	};

	/** Restore the CS shader, b0 and leading resource slots with optional dispatch isolation. */
	template <UINT SRVCount, UINT UAVCount = 1, ComputeStatePolicy Policy = ComputeStatePolicy::ClearBindingsAndPredication>
	class ComputeStateGuard
	{
		static_assert(SRVCount > 0 && SRVCount <= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
		static_assert(UAVCount > 0 && UAVCount <= D3D11_PS_CS_UAV_REGISTER_COUNT);

	public:
		explicit ComputeStateGuard(ID3D11DeviceContext* context) noexcept : context_(context)
		{
			if (!context_)
				return;
			classCount_ = static_cast<UINT>(classes_.size());
			context_->CSGetShader(shader_.GetAddressOf(), classes_.data(), &classCount_);
			context_->CSGetShaderResources(0, SRVCount, srvs_.data());
			context_->CSGetUnorderedAccessViews(0, UAVCount, uavs_.data());
			context_->CSGetConstantBuffers(0, 1, cb_.GetAddressOf());
			if constexpr (Policy == ComputeStatePolicy::ClearBindingsAndPredication) {
				context_->GetPredication(predicate_.GetAddressOf(), &predicateValue_);
				context_->SetPredication(nullptr, FALSE);
				Unbind();
			}
		}
		ComputeStateGuard(const ComputeStateGuard&) = delete;
		ComputeStateGuard& operator=(const ComputeStateGuard&) = delete;
		~ComputeStateGuard() noexcept
		{
			if (!Captured())
				return;
			Unbind();
			context_->CSSetShader(shader_.Get(), classes_.data(), classCount_);
			auto* cb = cb_.Get();
			context_->CSSetConstantBuffers(0, 1, &cb);
			context_->CSSetShaderResources(0, SRVCount, srvs_.data());
			context_->CSSetUnorderedAccessViews(0, UAVCount, uavs_.data(), nullptr);
			if constexpr (Policy == ComputeStatePolicy::ClearBindingsAndPredication)
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
		[[nodiscard]] bool Captured() const noexcept { return context_ != nullptr; }

		/** Release only the resource slots owned by this dispatch. */
		void Unbind() const noexcept
		{
			if (!Captured())
				return;
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
		UINT classCount_ = 0;
		std::array<ID3D11ShaderResourceView*, SRVCount> srvs_{};
		BOOL predicateValue_ = FALSE;
	};
}
