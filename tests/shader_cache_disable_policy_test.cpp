#include "ShaderCacheDisablePolicy.h"

#include <cstdint>

namespace
{
	using ShaderCacheDisablePolicy::DisableRequestAction;
	using ShaderCacheDisablePolicy::PendingDisableAction;

	constexpr bool CoversEveryDisableRequestCombination()
	{
		for (std::uint32_t bits = 0; bits < (1u << 2); ++bits) {
			const ShaderCacheDisablePolicy::DisableRequestInputs inputs{
				.shaderCacheEnabled = (bits & (1u << 0)) != 0,
				.vrNativeRestoreRequired = (bits & (1u << 1)) != 0,
			};
			const auto expected =
				inputs.shaderCacheEnabled && inputs.vrNativeRestoreRequired ?
					DisableRequestAction::DeferUntilNativeRestore :
					DisableRequestAction::DisableImmediately;
			if (ShaderCacheDisablePolicy::ResolveDisableRequest(inputs) != expected)
				return false;
		}
		return true;
	}

	constexpr bool CoversEveryPendingDisableCombination()
	{
		for (std::uint32_t bits = 0; bits < (1u << 3); ++bits) {
			const ShaderCacheDisablePolicy::PendingDisableInputs inputs{
				.pendingDisable = (bits & (1u << 0)) != 0,
				.enableRequested = (bits & (1u << 1)) != 0,
				.nativeTargetsRestored = (bits & (1u << 2)) != 0,
			};
			const auto expected =
				!inputs.pendingDisable ? PendingDisableAction::None :
				inputs.enableRequested ? PendingDisableAction::Cancel :
				inputs.nativeTargetsRestored ? PendingDisableAction::Complete :
				                              PendingDisableAction::None;
			if (ShaderCacheDisablePolicy::ResolvePendingDisable(inputs) != expected)
				return false;
		}
		return true;
	}

	constexpr bool CompletesOnlyAfterNativeRestore()
	{
		bool enabled = true;
		bool pendingDisable = false;

		const auto request = ShaderCacheDisablePolicy::ResolveDisableRequest({
			.shaderCacheEnabled = enabled,
			.vrNativeRestoreRequired = true,
		});
		if (request != DisableRequestAction::DeferUntilNativeRestore)
			return false;
		pendingDisable = true;

		const auto beforeRestore = ShaderCacheDisablePolicy::ResolvePendingDisable({
			.pendingDisable = pendingDisable,
			.enableRequested = false,
			.nativeTargetsRestored = false,
		});
		if (beforeRestore != PendingDisableAction::None || !enabled)
			return false;

		const auto afterRestore = ShaderCacheDisablePolicy::ResolvePendingDisable({
			.pendingDisable = pendingDisable,
			.enableRequested = false,
			.nativeTargetsRestored = true,
		});
		if (afterRestore != PendingDisableAction::Complete)
			return false;
		pendingDisable = false;
		enabled = false;
		return !pendingDisable && !enabled;
	}

	constexpr bool ReEnableCancelsDeferredDisable()
	{
		return ShaderCacheDisablePolicy::ResolvePendingDisable({
			.pendingDisable = true,
			.enableRequested = true,
			.nativeTargetsRestored = true,
		}) == PendingDisableAction::Cancel;
	}

	static_assert(CoversEveryDisableRequestCombination());
	static_assert(CoversEveryPendingDisableCombination());
	static_assert(CompletesOnlyAfterNativeRestore());
	static_assert(ReEnableCancelsDeferredDisable());
}

int main() {}
