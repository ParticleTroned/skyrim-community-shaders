#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ShaderCompilationSchedulingPolicy
{
	inline constexpr std::int32_t kStartupCompilationCpuSharePercent = 75;

	[[nodiscard]] constexpr std::int32_t CalculateDefaultCompilationThreadCount(
		std::int32_t a_logicalThreadCount) noexcept
	{
		const auto logicalThreadCount = std::max(a_logicalThreadCount, 1);
		// Split the percentage calculation so a corrupt or synthetic large input
		// cannot overflow before the clamp. Real hardware counts are much smaller,
		// but this keeps the policy total over its complete integer input domain.
		const auto wholeHundreds = logicalThreadCount / 100;
		const auto remainder = logicalThreadCount % 100;
		const auto workerCount =
			wholeHundreds * kStartupCompilationCpuSharePercent +
			(remainder * kStartupCompilationCpuSharePercent) / 100;
		return std::max(
			workerCount,
			1);
	}

	enum class ProcessPriorityBand
	{
		Standard,
		AboveNormal,
		High,
		Realtime,
	};

	enum class CooperativeThreadPriority
	{
		BelowNormal,
		Lowest,
		Idle,
	};

	enum class CompilationPhase
	{
		Startup,
		InGame,
	};

	/** Cumulative preparation windows; resident optimisations are always usable. */
	enum class PreparationMode : std::uint8_t
	{
		StartupOnly,
		Transitions,
		Gameplay,
	};

	/** Immutable identity captured before optional compiler inputs are assembled. */
	struct PreparationIdentity
	{
		std::uint64_t revision = 0, inputs = 0, owner = 0;
		bool operator==(const PreparationIdentity&) const = default;
	};

	[[nodiscard]] constexpr PreparationMode ValidatePreparationMode(std::int64_t value) noexcept
	{
		return value >= 0 && value <= 2 ? static_cast<PreparationMode>(value) : PreparationMode::StartupOnly;
	}

	[[nodiscard]] constexpr bool CanPrepareOptional(PreparationMode mode, bool blockingStartup,
		bool runtimeReady, bool transition) noexcept
	{
		return blockingStartup || (runtimeReady && (mode == PreparationMode::Gameplay ||
													   (mode == PreparationMode::Transitions && transition)));
	}

	/** The configured limit never grants more physical pool slots. */
	[[nodiscard]] constexpr std::size_t DispatchLimit(std::int32_t normal, std::int32_t background,
		bool backgroundMode, std::size_t poolCapacity) noexcept
	{
		return std::min(static_cast<std::size_t>(std::max(backgroundMode ? background : normal, 1)), poolCapacity);
	}

	enum class WorkerThreadPriorityMode
	{
		CooperativeBackground,
		ProcessNormal,
	};

	[[nodiscard]] constexpr CompilationPhase SelectCompilationPhase(
		bool a_startupCompilationComplete) noexcept
	{
		return a_startupCompilationComplete ? CompilationPhase::InGame : CompilationPhase::Startup;
	}

	[[nodiscard]] constexpr bool ShouldCompleteStartupCompilation(
		bool a_dataLoaded,
		bool a_compilationActive) noexcept
	{
		return a_dataLoaded && !a_compilationActive;
	}

	/** Background boot releases gameplay before the initial compilation batch completes. */
	[[nodiscard]] constexpr bool UseRuntimeSpecializationPolicy(bool startupComplete, bool dataLoaded, bool backgroundMode) noexcept
	{
		return startupComplete || (dataLoaded && backgroundMode);
	}

	/** Background boot is distinct from the normal blocking startup phase. */
	[[nodiscard]] constexpr bool IsBlockingStartup(bool startupComplete, bool dataLoaded, bool backgroundMode) noexcept
	{
		return !startupComplete && !dataLoaded && !backgroundMode;
	}

	[[nodiscard]] constexpr WorkerThreadPriorityMode SelectWorkerThreadPriorityMode(
		CompilationPhase a_phase) noexcept
	{
		return a_phase == CompilationPhase::Startup ?
		           WorkerThreadPriorityMode::CooperativeBackground :
		           WorkerThreadPriorityMode::ProcessNormal;
	}

	[[nodiscard]] constexpr CooperativeThreadPriority SelectCooperativeThreadPriority(
		ProcessPriorityBand a_processPriorityBand) noexcept
	{
		switch (a_processPriorityBand) {
		case ProcessPriorityBand::High:
		case ProcessPriorityBand::Realtime:
			return CooperativeThreadPriority::Idle;
		case ProcessPriorityBand::AboveNormal:
			return CooperativeThreadPriority::Lowest;
		default:
			return CooperativeThreadPriority::BelowNormal;
		}
	}
}
