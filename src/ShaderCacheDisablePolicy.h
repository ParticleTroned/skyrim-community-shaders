#pragma once

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
}
