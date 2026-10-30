#pragma once

#include "ShadowCullingPolicy.h"

namespace RE
{
	class BSRenderPass;
}

namespace LightLimitFixDetail::ShadowCulling
{
	/** Install sun-shadow collection filtering after all plugins load, unless SPS owns it. */
	void Install();
	/** Reject only lightless utility shadow draws inside the current point-light render scope. */
	bool SkipPointShadowDraw(const RE::BSRenderPass* pass, std::uint32_t technique);
	/** Publish immutable stereo demand and retained native shadow-camera identities. */
	void Update(const Settings& settings, bool featureEnabled);
	/** Draw shared native shadow-culling controls, disabled when unavailable. */
	void DrawSettings(Settings& settings, bool advanced);
	/** Report requested versus available controls; diagnostics compile out of production. */
	nlohmann::json Status(const Settings& settings);
}
