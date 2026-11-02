#include "ShaderCacheEnablePolicy.h"

#include <cstdint>
#include <initializer_list>

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

	constexpr bool ReverseDepthRequiresCustomShaders()
	{
		for (auto mode : { ApplyMode::StartupHydration, ApplyMode::RuntimeTransition }) {
			for (bool enabled : { false, true }) {
				for (bool requested : { false, true }) {
					if (ShaderCacheEnablePolicy::Resolve({
							.enabled = enabled,
							.enableRequested = requested,
							.targetEnabled = false,
							.mode = mode,
							.customShadersRequired = true,
						}) != Action::None)
						return false;
				}
			}
		}
		return true;
	}

	static_assert(ReverseDepthRequiresCustomShaders());
	static_assert(CoversEveryStateCombination());
	static_assert(DuplicateRuntimeEnableIsNoOp());
	static_assert(StartupDisableHydratesWithoutTransition());
}

int main() {}
