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
	/** Check once per native sun render so disabled small-part culling leaves sun draws on the unscoped fast path. */
	bool NeedsSunDrawScope();
	/** Reject only lightless utility shadow draws inside a matched native light render scope. */
	bool SkipShadowDraw(const RE::BSRenderPass* pass, std::uint32_t technique);
	/** Publish immutable stereo demand and retained native shadow-camera identities. */
	void Update(const Settings& settings, bool featureEnabled);
	/** Draw shared native shadow-culling controls, disabled when unavailable. */
	void DrawSettings(Settings& settings, bool advanced);
	/** Report requested versus available controls; diagnostics compile out of production. */
	nlohmann::json Status(const Settings& settings);
}
