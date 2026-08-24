#pragma once

namespace VRDepthCullingCacheRefreshPolicy
{
	enum class Action
	{
		None,
		WaitForCompiler,
		Apply,
		ConsumeWithoutRefresh
	};

	struct State
	{
		bool requested = false;
		bool diskCacheActive = false;
		bool shaderCompilationActive = false;
	};

	constexpr bool ShouldRequest(
		bool a_effectiveDepthCulling,
		bool a_refreshCompleted,
		bool a_refreshPending) noexcept
	{
		return a_effectiveDepthCulling && !a_refreshCompleted && !a_refreshPending;
	}

	constexpr Action SelectAction(const State& a_state) noexcept
	{
		if (!a_state.requested)
			return Action::None;
		if (!a_state.diskCacheActive)
			return Action::ConsumeWithoutRefresh;
		if (a_state.shaderCompilationActive)
			return Action::WaitForCompiler;
		return Action::Apply;
	}
}
