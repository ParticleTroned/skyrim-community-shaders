#include "ShaderCacheEnablePolicy.h"

#include <cstdint>

namespace
{
	using ShaderCacheEnablePolicy::Action;
	using ShaderCacheEnablePolicy::ApplyMode;

	constexpr bool CoversEveryStateCombination()
	{
		for (std::uint32_t bits = 0; bits < (1u << 3); ++bits) {
			const bool enabled = (bits & (1u << 0)) != 0;
			const bool enableRequested = (bits & (1u << 1)) != 0;
			const bool targetEnabled = (bits & (1u << 2)) != 0;

			const auto hydration = ShaderCacheEnablePolicy::Resolve({
				.enabled = enabled,
				.enableRequested = enableRequested,
				.targetEnabled = targetEnabled,
				.mode = ApplyMode::StartupHydration,
			});
			const auto expectedHydration =
				enabled == targetEnabled && enableRequested == targetEnabled ?
					Action::None :
					Action::Hydrate;
			if (hydration != expectedHydration)
				return false;

			const auto runtime = ShaderCacheEnablePolicy::Resolve({
				.enabled = enabled,
				.enableRequested = enableRequested,
				.targetEnabled = targetEnabled,
				.mode = ApplyMode::RuntimeTransition,
			});
			const auto expectedRuntime =
				enableRequested == targetEnabled ?
					Action::None :
					Action::Transition;
			if (runtime != expectedRuntime)
				return false;
		}
		return true;
	}

	constexpr bool DuplicateRuntimeEnableIsNoOp()
	{
		return ShaderCacheEnablePolicy::Resolve({
				   .enabled = true,
				   .enableRequested = true,
				   .targetEnabled = true,
				   .mode = ApplyMode::RuntimeTransition,
			   }) == Action::None;
	}

	constexpr bool StartupDisableHydratesWithoutTransition()
	{
		return ShaderCacheEnablePolicy::Resolve({
				   .enabled = true,
				   .enableRequested = true,
				   .targetEnabled = false,
				   .mode = ApplyMode::StartupHydration,
			   }) == Action::Hydrate;
	}

	static_assert(CoversEveryStateCombination());
	static_assert(DuplicateRuntimeEnableIsNoOp());
	static_assert(StartupDisableHydratesWithoutTransition());
}

int main() {}
