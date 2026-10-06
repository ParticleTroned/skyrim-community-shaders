#pragma once

#include <cstdint>

namespace NeuralRendering
{
	/** Bound to the owning prepared slot's source, capture, policy and content serial. */
	enum class CharacterEmptyProofKind : std::uint8_t
	{
		None,
		CpuSelection,
		GpuCategorySuperset,
		DiagnosticZero
	};

	[[nodiscard]] constexpr bool IsCharacterEmptyProof(CharacterEmptyProofKind kind) noexcept
	{
		return kind == CharacterEmptyProofKind::CpuSelection ||
		       kind == CharacterEmptyProofKind::GpuCategorySuperset ||
		       kind == CharacterEmptyProofKind::DiagnosticZero;
	}

	[[nodiscard]] constexpr const char* GetCharacterEmptyProofName(CharacterEmptyProofKind kind) noexcept
	{
		switch (kind) {
		case CharacterEmptyProofKind::CpuSelection:
			return "cpu_selection";
		case CharacterEmptyProofKind::GpuCategorySuperset:
			return "gpu_category_superset";
		case CharacterEmptyProofKind::DiagnosticZero:
			return "diagnostic_zero";
		default:
			return "none";
		}
	}
}
