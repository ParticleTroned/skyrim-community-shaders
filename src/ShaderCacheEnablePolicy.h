#pragma once

namespace ShaderCacheEnablePolicy
{
	enum class ApplyMode
	{
		StartupHydration,
		RuntimeTransition,
	};

	enum class Action
	{
		None,
		Hydrate,
		Transition,
	};

	struct Inputs
	{
		bool enabled = true;
		bool enableRequested = true;
		bool targetEnabled = true;
		ApplyMode mode = ApplyMode::RuntimeTransition;
	};

	[[nodiscard]] constexpr Action Resolve(const Inputs& a_inputs) noexcept
	{
		if (a_inputs.mode == ApplyMode::StartupHydration) {
			return a_inputs.enabled == a_inputs.targetEnabled &&
			               a_inputs.enableRequested == a_inputs.targetEnabled ?
			           Action::None :
			           Action::Hydrate;
		}

		return a_inputs.enableRequested == a_inputs.targetEnabled ?
		           Action::None :
		           Action::Transition;
	}
}
