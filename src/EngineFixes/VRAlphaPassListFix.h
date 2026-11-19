#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
#endif

namespace VRAlphaPassListFix
{
	/** Invalidate membership around native queue consumption and renderer destruction. */
	class RetirementScope
	{
	public:
		RetirementScope() noexcept;
		~RetirementScope();
		RetirementScope(const RetirementScope&) = delete;
		RetirementScope& operator=(const RetirementScope&) = delete;
	};
	/** Install the verified VR linked-alpha-list integrity guard. */
	void Install();
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Inspect or temporarily enable the guard for a controlled runtime comparison. */
	nlohmann::json Control(const nlohmann::json& args);
#endif
}
