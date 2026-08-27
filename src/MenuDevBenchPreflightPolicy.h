#pragma once

namespace MenuDevBenchPreflightPolicy
{
	constexpr double kFoveatedCenterArea = 0.3;
	constexpr double kPeripheryTAACenterArea = 0.3;
	constexpr double kPeripheryTAAOuterScale = 0.7;
	constexpr double kFloatTolerance = 0.0001;

	struct State
	{
		bool vr = false;
		bool inGame = false;
		bool stabilizerActiveForSession = false;
		bool developerMode = false;
		bool foveatedVendorDispatch = false;
		double foveatedCenterArea = 0.0;
		bool peripheryTAAEnabled = false;
		double peripheryTAACenterArea = 0.0;
		double peripheryTAAOuterScale = 0.0;
	};

	[[nodiscard]] constexpr bool NearlyEqual(double a_left, double a_right) noexcept
	{
		const double difference = a_left - a_right;
		return difference >= -kFloatTolerance && difference <= kFloatTolerance;
	}

	[[nodiscard]] constexpr bool HasRequiredFoveation(const State& a_state) noexcept
	{
		return a_state.foveatedVendorDispatch &&
		       NearlyEqual(a_state.foveatedCenterArea, kFoveatedCenterArea) &&
		       a_state.peripheryTAAEnabled &&
		       NearlyEqual(a_state.peripheryTAACenterArea, kPeripheryTAACenterArea) &&
		       NearlyEqual(a_state.peripheryTAAOuterScale, kPeripheryTAAOuterScale);
	}

	[[nodiscard]] constexpr bool CanApplyRuntimeSettings(const State& a_state) noexcept
	{
		return a_state.vr && a_state.inGame && a_state.stabilizerActiveForSession;
	}

	[[nodiscard]] constexpr bool IsReady(const State& a_state) noexcept
	{
		return CanApplyRuntimeSettings(a_state) &&
		       a_state.developerMode &&
		       HasRequiredFoveation(a_state);
	}
}
