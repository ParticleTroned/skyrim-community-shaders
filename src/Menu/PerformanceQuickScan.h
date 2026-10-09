#pragma once

#include "Utils/FeatureProfiling.h"
#include "Utils/ProfilerTiming.h"

#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace PerformanceQuickScan
{
	inline constexpr double kSettleSeconds = 5.0;
	inline constexpr double kMaximumSeconds = 30.0;
	inline constexpr std::uint32_t kCaptureFrames = 300;

	/** Global switches are observed every update, including changes made by hotkeys. */
	struct RenderConfiguration
	{
		bool shadersEnabled = false;
		bool shadersRequested = false;
		std::uint64_t definesGeneration = 0;
		int blockedShaderIndex = -1;
		bool operator==(const RenderConfiguration&) const = default;
	};

	enum class Phase
	{
		Idle,
		AwaitingMenuClose,
		Settling,
		Capturing,
		Complete,
		Failed,
		Cancelled
	};
	enum class Action
	{
		Wait,
		StartCapture,
		TimedOut,
		Interrupted,
		ConfigurationChanged
	};

	/** Require uninterrupted readiness before a bounded, independently owned capture. */
	struct Controller
	{
		Phase phase = Phase::Idle;
		double started = 0.0;
		double readySince = 0.0;
		RenderConfiguration renderConfiguration;

		bool Active() const { return phase == Phase::AwaitingMenuClose || phase == Phase::Settling || phase == Phase::Capturing; }
		void Begin(double now, bool menuOpen, RenderConfiguration configuration = {})
		{
			started = readySince = now;
			renderConfiguration = configuration;
			phase = menuOpen ? Phase::AwaitingMenuClose : Phase::Settling;
		}
		void MenuClosed(double now)
		{
			if (phase == Phase::AwaitingMenuClose) {
				readySince = now;
				phase = Phase::Settling;
			}
		}
		Action Poll(double now, bool ready, RenderConfiguration configuration = {})
		{
			if (!Active())
				return Action::Wait;
			if (configuration != renderConfiguration)
				return Action::ConfigurationChanged;
			if (now - started >= kMaximumSeconds)
				return Action::TimedOut;
			if (phase == Phase::Capturing)
				return ready ? Action::Wait : Action::Interrupted;
			if (phase != Phase::Settling)
				return Action::Wait;
			if (!ready)
				readySince = now;
			else if (now - readySince >= kSettleSeconds)
				return Action::StartCapture;
			return Action::Wait;
		}
	};

	/** Inactive or scene-inapplicable features must not hold up other measurements. */
	template <class Feature>
	bool Ready(const Feature& feature)
	{
		return !feature.IsPerformanceCostMeasurementEnabled() || feature.IsPerformanceCostMeasurementReady();
	}

	/** Release on the owning thread; global teardown cannot access other static objects. */
	template <class Profiler>
	class CaptureOwnership
	{
	public:
		CaptureOwnership() = default;
		CaptureOwnership(const CaptureOwnership&) = delete;
		CaptureOwnership& operator=(const CaptureOwnership&) = delete;
		CaptureOwnership(CaptureOwnership&& other) noexcept { *this = std::move(other); }
		CaptureOwnership& operator=(CaptureOwnership&& other) noexcept
		{
			if (this != &other) {
				Release();
				profiler = std::exchange(other.profiler, nullptr);
				wasEnabled = other.wasEnabled;
				sessionId = other.sessionId;
			}
			return *this;
		}

		/** Start without clearing existing histories; failure restores the preference. */
		bool Start(Profiler& source, std::uint32_t frames)
		{
			if (profiler)
				return false;
			wasEnabled = source.IsUserEnabled();
			source.SetUserEnabled(true);
			try {
				if (source.StartBoundedCapture(frames, false, sessionId, true)) {
					profiler = &source;
					return true;
				}
			} catch (...) {
				source.SetUserEnabled(wasEnabled);
				throw;
			}
			source.SetUserEnabled(wasEnabled);
			return false;
		}

		/** Cancellation never cancels or disables a replacement capture. */
		void Release()
		{
			if (!profiler)
				return;
			const auto progress = profiler->GetBoundedCaptureProgress();
			if (progress.sessionId == sessionId || progress.sessionId == 0) {
				profiler->CancelBoundedCapture(sessionId);
				profiler->SetUserEnabled(wasEnabled);
			}
			profiler = nullptr;
		}
		std::uint64_t SessionId() const { return sessionId; }

	private:
		Profiler* profiler = nullptr;
		bool wasEnabled = false;
		std::uint64_t sessionId = 0;
	};

	/** Sum complete feature-owned self-time histories; missing data is never zero cost. */
	template <class Timer>
	std::optional<double> Mean(const Util::FeatureProfiling::View& view, std::span<const Timer> timers, bool cpuMode, std::uint32_t frames)
	{
		if (!frames || !view.HasOwnedTimings(cpuMode))
			return std::nullopt;
		bool found = false;
		double sum = 0.0;
		for (const auto& timer : timers) {
			if (!Util::FeatureProfiling::MatchesExclusiveOwned(view, timer.name))
				continue;
			const bool hasSamples = cpuMode ? timer.hasCpu : timer.hasGpu;
			if (!hasSamples)
				continue;
			const auto count = cpuMode ? timer.cpuHistoryCount : timer.historyCount;
			if (!timer.valid || count != frames)
				return std::nullopt;
			found = true;
			for (std::uint32_t i = 0; i < count; ++i) {
				const float sample = cpuMode ? timer.GetCpuHistorySample(i) : timer.GetHistorySample(i);
				if (!Util::ProfilerTiming::IsValidSample(sample))
					return std::nullopt;
				sum += sample;
			}
		}
		return found ? std::optional(sum / frames) : std::nullopt;
	}
}
