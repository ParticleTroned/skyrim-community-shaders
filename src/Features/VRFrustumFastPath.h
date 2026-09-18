#pragma once

namespace VRFrustumFastPath
{
	/** Install the optional evaluator only on the verified native VR implementation. */
	void Install();
	/** Change admission for subsequent depth traversals without modifying native state. */
	void SetEnabled(bool enabled);
	bool IsInstalled();

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Retain native results while comparing eligible predictions and all visited masks. */
	bool SetVerification(bool enabled);
	/** Keep sampled native-path diagnostics comparable without fabricating sphere calls. */
	class NativeScope
	{
	public:
		NativeScope();
		~NativeScope();
		NativeScope(const NativeScope&) = delete;
		NativeScope& operator=(const NativeScope&) = delete;

	private:
		bool previous;
	};
#endif
}

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
namespace VRFrustumFastPath
{
	nlohmann::json GetStatus();
}
#endif
