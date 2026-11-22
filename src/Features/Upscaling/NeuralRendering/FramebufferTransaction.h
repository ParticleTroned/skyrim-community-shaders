#pragma once

#include "ComputeSubrect.h"

#include <array>
#include <d3d11.h>
#include <utility>
#include <wrl/client.h>

namespace NeuralRendering
{
	/// Stage a framebuffer in private storage and publish only a complete pair.
	class FramebufferTransaction
	{
	public:
		enum class Preparation
		{
			FullSurface,
			DeferredRegions
		};
		/** One finalized compute rectangle within an eye's output region. */
		struct EyeRegion
		{
			ID3D11Resource* target = nullptr;
			UINT subresource = 0;
			UINT baseOffsetX = 0;
			ComputeSubrect output;
			ComputeSubrect compute;
		};

		/** Capture state immediately; regional preparation defers all pixel copies. */
		FramebufferTransaction(ID3D11DeviceContext* context, ID3D11Texture2D* destination,
			ID3D11Texture2D* scratch, Preparation preparation = Preparation::FullSurface) noexcept :
			context_(context), destination_(destination), scratch_(scratch)
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
			captured_ = true;
			width_ = dst.Width;
			height_ = dst.Height;
			if (preparation == Preparation::FullSurface) {
				preparationAttempted_ = initialized_ = fullSurface_ = true;
				Copy(scratch_.Get(), destination_.Get());
			}
		}
		FramebufferTransaction(const FramebufferTransaction&) = delete;
		FramebufferTransaction& operator=(const FramebufferTransaction&) = delete;
		~FramebufferTransaction() noexcept
		{
			if (!captured_)
				return;
			context_->SetPredication(predicate_.Get(), predicateValue_);
			context_->OMSetRenderTargets(static_cast<UINT>(targets_.size()), targets_.data(), depth_.Get());
			for (auto* target : targets_)
				if (target)
					target->Release();
		}
		/** Reports state protection, independently of pixel initialization. */
		[[nodiscard]] bool IsValid() const noexcept { return captured_; }

		/** Validate the entire frozen eye set before copying; any failed/repeated attempt is terminal. */
		bool PrepareRegions(const std::array<EyeRegion, 2>& eyes, UINT activeEyeMask,
			UINT eyeCount, UINT eyeWidth, UINT eyeHeight) noexcept
		{
			initialized_ = false;
			if (std::exchange(preparationAttempted_, true) || !captured_ || publicationAttempted_ ||
				(eyeCount != 1 && eyeCount != 2) || !activeEyeMask ||
				(activeEyeMask & ~((1u << eyeCount) - 1u)) != 0 ||
				!eyeWidth || std::uint64_t(eyeWidth) * eyeCount != width_ || eyeHeight != height_)
				return false;
			std::array<D3D11_BOX, 2> boxes{};
			UINT count = 0;
			std::uint64_t area = 0;
			for (UINT eye = 0; eye < eyeCount; ++eye) {
				if ((activeEyeMask & (1u << eye)) == 0)
					continue;
				const auto& region = eyes[eye];
				if (region.target != scratch_.Get() || region.subresource != 0 ||
					region.baseOffsetX != std::uint64_t(eye) * eyeWidth ||
					!region.output.Fits(eyeWidth, eyeHeight) ||
					!region.compute.Fits(region.output.width, region.output.height))
					return false;
				// Nested Fits checks and the exact eye partition prove every addition and disjointness.
				const UINT left = region.baseOffsetX + region.output.baseX + region.compute.baseX;
				const UINT top = region.output.baseY + region.compute.baseY;
				boxes[count++] = { left, top, 0, left + region.compute.width, top + region.compute.height, 1 };
				area += region.compute.Area();
			}
			boxes_ = boxes;
			boxCount_ = count;
			fullSurface_ = area == std::uint64_t(width_) * height_;
			Copy(scratch_.Get(), destination_.Get());
			initialized_ = true;
			return true;
		}

		/// Publish once, after complete output handling; the caller must unbind conflicting compute views.
		bool Commit(bool completePair) noexcept
		{
			if (std::exchange(publicationAttempted_, true) || !captured_ || !initialized_ || !completePair)
				return false;
			Copy(destination_.Get(), scratch_.Get());
			return true;
		}

	private:
		void Copy(ID3D11Texture2D* destination, ID3D11Texture2D* source) const noexcept
		{
			if (fullSurface_) {
				context_->CopyResource(destination, source);
				return;
			}
			for (UINT index = 0; index < boxCount_; ++index) {
				const auto& box = boxes_[index];
				context_->CopySubresourceRegion(destination, 0, box.left, box.top, 0, source, 0, &box);
			}
		}

		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> destination_;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> scratch_;
		std::array<D3D11_BOX, 2> boxes_{};
		UINT width_ = 0, height_ = 0, boxCount_ = 0;
		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets_{};
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_;
		Microsoft::WRL::ComPtr<ID3D11Predicate> predicate_;
		BOOL predicateValue_ = FALSE;
		bool captured_ = false;
		bool preparationAttempted_ = false;
		bool initialized_ = false;
		bool fullSurface_ = false;
		bool publicationAttempted_ = false;
	};
}
