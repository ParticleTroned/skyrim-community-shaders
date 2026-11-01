#pragma once

#include <array>
#include <d3d11_1.h>
#include <wrl/client.h>

namespace NeuralRendering
{
	enum class ComputeStatePolicy
	{
		PreserveBindings,
		ClearBindingsAndPredication
	};

	/** Restore the CS shader, leading constant-buffer windows and resource slots with optional dispatch isolation. */
	template <UINT SRVCount, UINT UAVCount = 1, ComputeStatePolicy Policy = ComputeStatePolicy::ClearBindingsAndPredication, UINT ConstantBufferCount = 1>
	class ComputeStateGuard
	{
		static_assert(SRVCount > 0 && SRVCount <= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
		static_assert(UAVCount > 0 && UAVCount <= D3D11_PS_CS_UAV_REGISTER_COUNT);
		static_assert(ConstantBufferCount > 0 && ConstantBufferCount <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);

	public:
		explicit ComputeStateGuard(ID3D11DeviceContext* context) noexcept : context_(context)
		{
			if (!context_)
				return;
			classCount_ = static_cast<UINT>(classes_.size());
			context_->CSGetShader(shader_.GetAddressOf(), classes_.data(), &classCount_);
			context_->CSGetShaderResources(0, SRVCount, srvs_.data());
			context_->CSGetUnorderedAccessViews(0, UAVCount, uavs_.data());
			if (SUCCEEDED(context_->QueryInterface(IID_PPV_ARGS(context1_.GetAddressOf()))))
				context1_->CSGetConstantBuffers1(0, ConstantBufferCount, buffers_.data(), firstConstant_.data(), constantCount_.data());
			else
				context_->CSGetConstantBuffers(0, ConstantBufferCount, buffers_.data());
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
			// Whole/null bindings use the base API, including on drivers without offsetting.
			for (UINT index = 0; index < ConstantBufferCount; ++index) {
				auto* cb = buffers_[index];
				if (context1_ && cb && (firstConstant_[index] != 0 || constantCount_[index] != D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT))
					context1_->CSSetConstantBuffers1(index, 1, &cb, &firstConstant_[index], &constantCount_[index]);
				else
					context_->CSSetConstantBuffers(index, 1, &cb);
				if (cb)
					cb->Release();
			}
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
		Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader_;
		std::array<ID3D11UnorderedAccessView*, UAVCount> uavs_{};
		std::array<ID3D11Buffer*, ConstantBufferCount> buffers_{};
		std::array<UINT, ConstantBufferCount> firstConstant_{}, constantCount_{};
		Microsoft::WRL::ComPtr<ID3D11Predicate> predicate_;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classes_{};
		UINT classCount_ = 0;
		std::array<ID3D11ShaderResourceView*, SRVCount> srvs_{};
		BOOL predicateValue_ = FALSE;
	};
}
