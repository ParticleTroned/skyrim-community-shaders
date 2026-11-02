#pragma once

#include <d3d11.h>
#include <utility>
#include <winrt/base.h>

namespace UpscalingTextures
{
	/** Operation-local immutable geometry; the COM reference keeps its exact source alive. */
	class TextureSourceDescription
	{
	public:
		/** Captures or reuses the exact owned source; failure clears the description. */
		[[nodiscard]] bool Capture(ID3D11Resource* a_source)
		{
			if (Matches(a_source))
				return true;
			TextureSourceDescription captured;
			if (!a_source || FAILED(a_source->QueryInterface(IID_PPV_ARGS(captured.texture_.put()))) || !captured.texture_) {
				*this = {};
				return false;
			}
			captured.texture_->GetDesc(&captured.desc_);
			captured.source_ = a_source;
			*this = std::move(captured);
			return true;
		}

		/** Reuse requires the exact captured interface and its retained texture owner. */
		[[nodiscard]] bool Matches(ID3D11Resource* a_source) const
		{
			return texture_ && source_ == a_source;
		}
		[[nodiscard]] const D3D11_TEXTURE2D_DESC& Desc() const { return desc_; }

	private:
		winrt::com_ptr<ID3D11Texture2D> texture_;
		ID3D11Resource* source_ = nullptr;
		D3D11_TEXTURE2D_DESC desc_{};
	};

	/** Native SE inputs used to validate and create the same resource transaction. */
	struct SourceDescriptions
	{
		TextureSourceDescription color, motion;

		[[nodiscard]] bool Capture(ID3D11Resource* a_color, ID3D11Resource* a_motion)
		{
			if (Matches(a_color, a_motion))
				return true;
			SourceDescriptions captured;
			if (!captured.color.Capture(a_color) || !captured.motion.Capture(a_motion)) {
				*this = {};
				return false;
			}
			*this = std::move(captured);
			return true;
		}
		[[nodiscard]] bool Matches(ID3D11Resource* a_color, ID3D11Resource* a_motion) const
		{
			return color.Matches(a_color) && motion.Matches(a_motion);
		}
	};
}
