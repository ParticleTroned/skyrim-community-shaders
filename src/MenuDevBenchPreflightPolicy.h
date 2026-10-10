#pragma once

namespace MenuDevBenchPreflightPolicy
{
	enum class Preparation
	{
		Coc,
		Tuning,
	};

	constexpr double kPeripheryTAACenterArea = 0.3;
	constexpr double kPeripheryTAAOuterScale = 0.7;
	constexpr double kFloatTolerance = 0.0001;

	struct State
	{
		bool vr = false;
		bool inGame = false;
		bool stabilizerActiveForSession = false;
		bool developerMode = false;
		bool neuralRenderingEnabled = false;
		bool foveatedVendorDispatch = false;
		bool fovOnlyCalibrated = false;
		double foveatedCenterArea = 0.0;
		double foveatedOuterFeather = 0.05;
		bool peripheryTAAEnabled = false;
		double peripheryTAACenterArea = 0.0;
		double peripheryTAAOuterScale = 0.0;
	};

	[[nodiscard]] constexpr bool NearlyEqual(double a_left, double a_right) noexcept
	{
		const double difference = a_left - a_right;
		return difference >= -kFloatTolerance && difference <= kFloatTolerance;
	}

	/** Preserve the fixed TAA fixture outline through FOV-only feather ownership. */
	[[nodiscard]] constexpr double FovOnlyFixtureScale(double feather) noexcept
	{
		return kPeripheryTAAOuterScale - 2.0 * (feather < 0.0001 ? 0.0001 : feather);
	}

	[[nodiscard]] constexpr bool HasRequiredFoveation(const State& a_state) noexcept
	{
		return a_state.foveatedVendorDispatch &&
		       a_state.fovOnlyCalibrated &&
		       NearlyEqual(a_state.foveatedCenterArea, FovOnlyFixtureScale(a_state.foveatedOuterFeather)) &&
		       a_state.peripheryTAAEnabled &&
		       NearlyEqual(a_state.peripheryTAACenterArea, kPeripheryTAACenterArea) &&
		       NearlyEqual(a_state.peripheryTAAOuterScale, kPeripheryTAAOuterScale);
	}

	[[nodiscard]] constexpr bool CanApplyRuntimeSettings(const State& a_state, Preparation a_preparation = Preparation::Coc) noexcept
	{
		return a_state.vr && a_state.inGame && !a_state.neuralRenderingEnabled &&
		       (a_preparation == Preparation::Tuning || a_state.stabilizerActiveForSession);
	}

	[[nodiscard]] constexpr bool IsReady(const State& a_state, Preparation a_preparation = Preparation::Coc) noexcept
	{
		return CanApplyRuntimeSettings(a_state, a_preparation) &&
		       a_state.developerMode &&
		       HasRequiredFoveation(a_state);
	}
}
