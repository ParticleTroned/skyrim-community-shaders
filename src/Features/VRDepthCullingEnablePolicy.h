#pragma once

namespace VRDepthCullingEnablePolicy
{
	constexpr bool IsEnabled(
		bool a_isInterior,
		bool a_depthCullingEnabled,
		bool a_enableInInteriors)
	{
		return a_depthCullingEnabled && (!a_isInterior || a_enableInInteriors);
	}
}
