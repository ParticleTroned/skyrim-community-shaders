#pragma once

namespace VolumetricLightingCacheRefreshPolicy
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

	constexpr Action SelectAction(const State& a_state)
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
