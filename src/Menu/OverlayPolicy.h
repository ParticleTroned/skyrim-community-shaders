#pragma once

namespace OverlayPolicy
{
	struct ShaderCompilationStatusAdmission
	{
		bool hasRenderedWorldFrame = false;
		bool menuSessionOpen = false;
		bool performanceOverlayOpen = false;
	};

	[[nodiscard]] constexpr bool ShouldShowShaderCompilationStatus(
		const ShaderCompilationStatusAdmission& a_admission) noexcept
	{
		return !a_admission.hasRenderedWorldFrame ||
		       a_admission.menuSessionOpen ||
		       a_admission.performanceOverlayOpen;
	}

	[[nodiscard]] constexpr bool ShouldRouteShaderCompilationStatusToHMD(
		bool a_statusAdmitted,
		bool a_showInHMD) noexcept
	{
		return a_statusAdmitted && a_showInHMD;
	}
}
