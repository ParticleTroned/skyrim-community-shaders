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
}
