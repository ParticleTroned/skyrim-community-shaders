#include "ShaderCacheDisablePolicy.h"

#include <cstdint>

namespace
{
	using ShaderCacheDisablePolicy::DisableRequestAction;
	using ShaderCacheDisablePolicy::PendingDisableAction;

	constexpr bool CoversEveryEnableRequestCombination()
	{
		for (std::uint32_t bits = 0; bits < (1u << 3); ++bits) {
			const ShaderCacheDisablePolicy::EnableRequestInputs inputs{
				.enableAlreadyRequested = (bits & (1u << 0)) != 0,
				.vrRenderScaleRequested = (bits & (1u << 1)) != 0,
				.vrRenderScaleLatched = (bits & (1u << 2)) != 0,
			};
			const bool expected = !inputs.enableAlreadyRequested &&
			                      inputs.vrRenderScaleRequested &&
			                      !inputs.vrRenderScaleLatched;
			if (ShaderCacheDisablePolicy::ShouldRequestRelatchOnEnable(inputs) != expected)
				return false;
		}
		return true;
	}

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
				!inputs.pendingDisable       ? PendingDisableAction::None :
				inputs.enableRequested       ? PendingDisableAction::Cancel :
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

	static_assert(CoversEveryEnableRequestCombination());
	static_assert(CoversEveryDisableRequestCombination());
	static_assert(CoversEveryPendingDisableCombination());
	static_assert(CompletesOnlyAfterNativeRestore());
	static_assert(ReEnableCancelsDeferredDisable());

	struct DeferredDisableFixture
	{
		std::atomic_bool pending{ true };
		std::atomic_bool requested{ false };
		std::atomic_bool enabled{ true };
		bool restored = true;
		bool statusOwned = true;
		std::uint32_t statusReads = 0;

		void Enable()
		{
			requested.store(true);
			pending.store(false);
			enabled.store(true);
		}

		void Disable()
		{
			requested.store(false);
			pending.store(true);
		}

		struct Authority
		{
			DeferredDisableFixture& state;
			std::uint32_t enablesBeforeAcquire = 0;
			bool disableBeforeAcquire = false;
			bool owned = false;
			std::uint32_t acquisitions = 0;

			void lock()
			{
				// Model publications after the service fast check but before ownership.
				for (std::uint32_t i = 0; i < enablesBeforeAcquire; ++i)
					state.Enable();
				if (disableBeforeAcquire)
					state.Disable();
				owned = true;
				++acquisitions;
			}

			void unlock() { owned = false; }
		} authority{ *this };

		PendingDisableAction Service()
		{
			return ShaderCacheDisablePolicy::ApplyPendingDisable(
				authority, pending, requested, enabled, [this] {
					statusOwned = statusOwned && authority.owned;
					++statusReads;
					return restored;
				});
		}
	};

	bool CoversDeferredDisablePublicationOrdering()
	{
		DeferredDisableFixture stable;
		stable.pending.store(false);
		if (stable.Service() != PendingDisableAction::None ||
			stable.authority.acquisitions != 0 || stable.statusReads != 0)
			return false;

		for (std::uint32_t enables = 1; enables <= 2; ++enables) {
			DeferredDisableFixture newerEnable;
			newerEnable.authority.enablesBeforeAcquire = enables;
			if (newerEnable.Service() != PendingDisableAction::None ||
				!newerEnable.requested.load() || !newerEnable.enabled.load() ||
				newerEnable.pending.load() || newerEnable.statusReads != 0 ||
				newerEnable.authority.owned)
				return false;
		}

		DeferredDisableFixture serviceFirst;
		if (serviceFirst.Service() != PendingDisableAction::Complete ||
			serviceFirst.enabled.load() || serviceFirst.pending.load() ||
			!serviceFirst.statusOwned || serviceFirst.statusReads != 1)
			return false;
		serviceFirst.Enable();
		serviceFirst.Enable();
		if (serviceFirst.Service() != PendingDisableAction::None ||
			!serviceFirst.enabled.load() || !serviceFirst.requested.load())
			return false;

		DeferredDisableFixture freshDisable;
		freshDisable.authority.enablesBeforeAcquire = 1;
		freshDisable.authority.disableBeforeAcquire = true;
		if (freshDisable.Service() != PendingDisableAction::Complete ||
			freshDisable.enabled.load() || freshDisable.requested.load() ||
			freshDisable.pending.load() || !freshDisable.statusOwned)
			return false;

		DeferredDisableFixture waiting;
		waiting.restored = false;
		if (waiting.Service() != PendingDisableAction::None ||
			!waiting.enabled.load() || !waiting.pending.load() || !waiting.statusOwned)
			return false;
		waiting.requested.store(true);
		if (waiting.Service() != PendingDisableAction::Cancel ||
			!waiting.enabled.load() || waiting.pending.load() || waiting.statusReads != 1)
			return false;
		return true;
	}
}

int main()
{
	return CoversDeferredDisablePublicationOrdering() ? 0 : 1;
}
