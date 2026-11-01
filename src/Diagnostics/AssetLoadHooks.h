#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>

namespace CSX::Diagnostics::Stutters::AssetLoadHooks
{
	/** Install forwarding loader hooks independently of texture-streaming policy. */
	void Install();
	/** Publish availability of the existing optional DDS and device hooks. */
	void SetDDSAvailable(bool available) noexcept;
	void SetTextureResourceAvailable(bool available) noexcept;
	/** Read actual hook coverage without accessing engine objects. */
	nlohmann::json Status();
}
#endif
