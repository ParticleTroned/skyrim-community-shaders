#pragma once

#include <DirectXTex.h>
#include <stdexcept>

namespace CSX::Screenshot
{
	/** Preserve display-encoded SDR bytes when subsequent conversion removes an sRGB format tag. */
	inline void PreserveNativeSdrBytes(DirectX::ScratchImage& image)
	{
		const auto format = DirectX::MakeLinear(image.GetMetadata().format);
		if ((format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_B8G8R8A8_UNORM) ||
			!image.OverrideFormat(format))
			throw std::invalid_argument("native capture requires RGBA8 or BGRA8 display bytes");
	}
}
