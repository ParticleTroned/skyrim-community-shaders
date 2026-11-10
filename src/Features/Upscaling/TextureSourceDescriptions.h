#pragma once

#include <d3d11.h>
#include <winrt/base.h>

namespace UpscalingTextures
{
	/** Operation-local immutable geometry; the COM reference keeps its exact source alive. */
	class TextureSourceDescription
	{
	public:
		/** Captures or reuses the exact owned source; failure clears the description. */
		[[nodiscard]] bool Capture(ID3D11Resource* a_source);
		/** Reuse requires the exact captured interface and its retained texture owner. */
		[[nodiscard]] bool Matches(ID3D11Resource* a_source) const
		{
			return texture_ && source_ == a_source;
		}
		/** Returns immutable captured geometry, or an empty description after failure. */
		[[nodiscard]] const D3D11_TEXTURE2D_DESC& Desc() const { return desc_; }

	private:
		winrt::com_ptr<ID3D11Texture2D> texture_;
		ID3D11Resource* source_ = nullptr;
		D3D11_TEXTURE2D_DESC desc_{};
	};

	/** Source geometry only; callers must still validate the current vendor contract and destinations. */
	struct VRInputSourceDescriptions
	{
		TextureSourceDescription color, motion, reactive, transparency;

		/** Retains all four sources as one group; failed recapture clears the entire group. */
		[[nodiscard]] bool Capture(ID3D11Resource* a_color, ID3D11Resource* a_motion,
			ID3D11Resource* a_reactive, ID3D11Resource* a_transparency);
		[[nodiscard]] bool Matches(ID3D11Resource* a_color, ID3D11Resource* a_motion,
			ID3D11Resource* a_reactive, ID3D11Resource* a_transparency) const
		{
			return color.Matches(a_color) && motion.Matches(a_motion) &&
			       reactive.Matches(a_reactive) && transparency.Matches(a_transparency);
		}
	};
}
