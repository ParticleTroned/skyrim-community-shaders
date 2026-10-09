#pragma once

#include "Features/VR/WandBeamGeometry.h"

#include <d3d11_1.h>

namespace WandBeamRenderer
{
	/** Draws a clipped beam within the caller's isolated overlay graphics state. */
	bool Draw(ID3D11Device* a_device, ID3D11DeviceContext1* a_context,
		const WandBeamGeometry::ScreenBeam& a_beam, const WandBeamGeometry::Viewport& a_viewport, const std::array<float, 4>& a_colour);

	/** Composites a bounded beam rectangle and restores compute bindings. */
	bool Composite(ID3D11Device* a_device, ID3D11DeviceContext1* a_context,
		ID3D11UnorderedAccessView* a_target, const WandBeamGeometry::ScreenBeam& a_beam,
		const WandBeamGeometry::Viewport& a_viewport, const std::array<float, 4>& a_colour);

	inline constexpr float RadiusPixels = 1.5f;
}
