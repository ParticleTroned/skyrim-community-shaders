#pragma once

#include <atomic>
#include <mutex>

namespace ShaderCacheDisablePolicy
{
	struct EnableRequestInputs
	{
		bool enableAlreadyRequested = false;
		bool vrRenderScaleRequested = false;
		bool vrRenderScaleLatched = false;
	};

	[[nodiscard]] constexpr bool ShouldRequestRelatchOnEnable(
		const EnableRequestInputs& a_inputs) noexcept
	{
		return !a_inputs.enableAlreadyRequested &&
		       a_inputs.vrRenderScaleRequested &&
		       !a_inputs.vrRenderScaleLatched;
	}

	enum class DisableRequestAction
	{
		DisableImmediately,
		DeferUntilNativeRestore,
	};

	struct DisableRequestInputs
	{
		bool shaderCacheEnabled = false;
		bool vrNativeRestoreRequired = false;
	};

	[[nodiscard]] constexpr DisableRequestAction ResolveDisableRequest(
		const DisableRequestInputs& a_inputs) noexcept
	{
		return a_inputs.shaderCacheEnabled && a_inputs.vrNativeRestoreRequired ?
		           DisableRequestAction::DeferUntilNativeRestore :
		           DisableRequestAction::DisableImmediately;
	}

	enum class PendingDisableAction
	{
		None,
		Cancel,
		Complete,
	};

	struct PendingDisableInputs
	{
		bool pendingDisable = false;
		bool enableRequested = false;
		bool nativeTargetsRestored = false;
	};

	[[nodiscard]] constexpr PendingDisableAction ResolvePendingDisable(
		const PendingDisableInputs& a_inputs) noexcept
	{
		if (!a_inputs.pendingDisable)
			return PendingDisableAction::None;
		if (a_inputs.enableRequested)
			return PendingDisableAction::Cancel;
		if (a_inputs.nativeTargetsRestored)
			return PendingDisableAction::Complete;
		return PendingDisableAction::None;
	}

	/** @brief Re-read and commit deferred disable under the request publisher's authority. */
	template <class AuthorityMutex, class NativeTargetsRestored>
	[[nodiscard]] PendingDisableAction ApplyPendingDisable(
		AuthorityMutex& a_authorityMutex,
		std::atomic_bool& a_pendingDisable,
		const std::atomic_bool& a_enableRequested,
		std::atomic_bool& a_enabled,
		NativeTargetsRestored&& a_nativeTargetsRestored)
	{
		// Stable frames avoid both ownership and controller status resolution.
		if (!a_pendingDisable.load(std::memory_order_acquire))
			return PendingDisableAction::None;

		const std::scoped_lock authorityLock(a_authorityMutex);
		const bool pendingDisable = a_pendingDisable.load(std::memory_order_acquire);
		if (!pendingDisable)
			return PendingDisableAction::None;

		const bool enableRequested = a_enableRequested.load(std::memory_order_acquire);
		const auto action = ResolvePendingDisable({
			.pendingDisable = pendingDisable,
			.enableRequested = enableRequested,
			.nativeTargetsRestored = !enableRequested && a_nativeTargetsRestored(),
		});
		if (action != PendingDisableAction::None)
			a_pendingDisable.store(false, std::memory_order_release);
		if (action == PendingDisableAction::Complete)
			a_enabled.store(false, std::memory_order_release);
		return action;
	}
}
