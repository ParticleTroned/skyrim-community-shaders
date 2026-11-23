#pragma once

#include <array>
#include <d3d11.h>
#include <wrl/client.h>

namespace NeuralRendering
{
	/// Stage a framebuffer in private storage and publish only a complete pair.
	class FramebufferTransaction
	{
	public:
		FramebufferTransaction(ID3D11DeviceContext* context, ID3D11Texture2D* destination,
			ID3D11Texture2D* scratch) noexcept : context_(context), destination_(destination), scratch_(scratch)
		{
			if (!context || !destination || !scratch || destination == scratch ||
				context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
				return;
			D3D11_TEXTURE2D_DESC dst{}, src{};
			destination->GetDesc(&dst);
			scratch->GetDesc(&src);
			if (!dst.Width || !dst.Height || dst.Width != src.Width || dst.Height != src.Height ||
				dst.Format != src.Format || dst.MipLevels != 1 || src.MipLevels != 1 ||
				dst.ArraySize != 1 || src.ArraySize != 1 || dst.SampleDesc.Count != 1 || src.SampleDesc.Count != 1 ||
				dst.Usage != D3D11_USAGE_DEFAULT || src.Usage != D3D11_USAGE_DEFAULT ||
				(src.BindFlags & D3D11_BIND_UNORDERED_ACCESS) == 0)
				return;
			Microsoft::WRL::ComPtr<ID3D11Device> owner, dstOwner, srcOwner;
			context->GetDevice(&owner);
			destination->GetDevice(&dstOwner);
			scratch->GetDevice(&srcOwner);
			if (owner.Get() != dstOwner.Get() || owner.Get() != srcOwner.Get())
				return;
			context_->OMGetRenderTargets(static_cast<UINT>(targets_.size()), targets_.data(), &depth_);
			context_->GetPredication(&predicate_, &predicateValue_);
			context_->OMSetRenderTargets(0, nullptr, nullptr);
			context_->SetPredication(nullptr, FALSE);
			context_->CopyResource(scratch_, destination_);
			valid_ = true;
		}
		FramebufferTransaction(const FramebufferTransaction&) = delete;
		FramebufferTransaction& operator=(const FramebufferTransaction&) = delete;
		~FramebufferTransaction() noexcept
		{
			if (!valid_)
				return;
			context_->SetPredication(predicate_.Get(), predicateValue_);
			context_->OMSetRenderTargets(static_cast<UINT>(targets_.size()), targets_.data(), depth_.Get());
			for (auto* target : targets_)
				if (target)
					target->Release();
		}
		[[nodiscard]] bool IsValid() const noexcept { return valid_; }
		/// The caller must unbind its compute output before committing.
		bool Commit(bool completePair) noexcept
		{
			if (!valid_ || !completePair)
				return false;
			context_->CopyResource(destination_, scratch_);
			return true;
		}

	private:
		ID3D11DeviceContext* context_;
		ID3D11Texture2D* destination_;
		ID3D11Texture2D* scratch_;
		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets_{};
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_;
		Microsoft::WRL::ComPtr<ID3D11Predicate> predicate_;
		BOOL predicateValue_ = FALSE;
		bool valid_ = false;
	};
}
