#pragma once

#include "PerformanceTuningStatistics.h"
#include "Utils/FeatureProfiling.h"
#include "Utils/ProfilerTiming.h"

#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace PerformanceQuickScan
{
	inline constexpr double kSettleSeconds = 3.0;
	inline constexpr double kMaximumSeconds = 30.0;
	inline constexpr std::uint32_t kCaptureFrames = 300;
	inline constexpr std::uint32_t kBatchScans = 3;
	inline constexpr double kBatchGapSeconds = 3.0;
	/** Collect the live panel's unique completed samples only within an active capture. */
	struct FrameTimingCapture
	{
		struct Channel
		{
			PerformanceTuningStatistics::Moments moments;
			std::uint64_t lastId = 0;
			std::uint64_t missingSamples = 0;
			void Push(std::uint64_t id, double value, bool available)
			{
				if (id <= lastId)
					return;
				if (lastId != 0)
					missingSamples += id - lastId - 1;
				if (!available || !PerformanceTuningStatistics::AddMoment(moments, value, 1.0))
					++missingSamples;
				lastId = id;
			}
		};
		Channel frame, cpu, gpu;
		std::uint64_t epoch = 0;
		bool vr = false, valid = false;
		template <class Summary>
		void Begin(const Summary& summary)
		{
			*this = {};
			vr = summary.vrTiming;
			epoch = vr ? summary.vrTimingEpoch : summary.flatTimingEpoch;
			const auto baselineFrameId = vr ? (summary.vrSamples.empty() ? summary.vrLatestFrameIndex : summary.vrSamples.back().frameIndex) : summary.flatPresentId;
			frame.lastId = cpu.lastId = gpu.lastId = baselineFrameId;
			valid = true;
		}
		template <class Summary>
		void Push(const Summary& summary)
		{
			if (!valid || vr != summary.vrTiming || epoch != (vr ? summary.vrTimingEpoch : summary.flatTimingEpoch)) {
				valid = false;
				return;
			}
			if (vr) {
				for (const auto& sample : summary.vrSamples) {
					frame.Push(sample.frameIndex, sample.frameMs, sample.hasFrame);
					cpu.Push(sample.frameIndex, sample.cpuMs, sample.hasCpu);
					gpu.Push(sample.frameIndex, sample.gpuMs, sample.hasGpu);
				}
			} else {
				for (const auto& sample : summary.flatSamples) {
					if (!sample.resolved)
						break;
					frame.Push(sample.presentId, sample.frameMs, sample.hasFrame);
					cpu.Push(sample.presentId, sample.cpuMs, sample.hasCpu);
					gpu.Push(sample.presentId, sample.gpuMs, sample.hasGpu);
				}
			}
		}
		std::optional<double> Mean(const Channel& channel) const
		{
			return valid && channel.missingSamples == 0 && channel.moments.sampleWeight > 0 ? std::optional(PerformanceTuningStatistics::GetMean(channel.moments)) : std::nullopt;
		}
	};

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
		BetweenScans,
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
		std::uint32_t scanIndex = 1;
		std::uint32_t scanCount = 1;
		RenderConfiguration renderConfiguration;

		bool Active() const { return phase == Phase::AwaitingMenuClose || phase == Phase::Settling || phase == Phase::BetweenScans || phase == Phase::Capturing; }
		void Begin(double now, bool menuOpen, RenderConfiguration configuration = {}, bool batch = false)
		{
			started = readySince = now;
			scanIndex = 1;
			scanCount = batch ? kBatchScans : 1;
			renderConfiguration = configuration;
			phase = menuOpen ? Phase::AwaitingMenuClose : Phase::Settling;
		}
		/** Retain measurement ownership while the next scan settles inside its gap. */
		bool Advance(double now)
		{
			if (phase != Phase::Complete || scanIndex >= scanCount)
				return false;
			++scanIndex;
			started = readySince = now;
			phase = Phase::BetweenScans;
			return true;
		}
		void MenuClosed(double now)
		{
			if (phase == Phase::AwaitingMenuClose) {
				readySince = now;
				phase = Phase::Settling;
			}
		}
		/** Recorder preparation may overlap settling but must finish before capture. */
		Action Poll(double now, bool ready, RenderConfiguration configuration = {}, bool recorderReady = true)
		{
			if (!Active())
				return Action::Wait;
			if (configuration != renderConfiguration)
				return Action::ConfigurationChanged;
			if (now - started >= kMaximumSeconds)
				return Action::TimedOut;
			if (phase == Phase::Capturing)
				return ready ? Action::Wait : Action::Interrupted;
			if (phase != Phase::Settling && phase != Phase::BetweenScans)
				return Action::Wait;
			if (!ready)
				readySince = now;
			else if (recorderReady && now - readySince >= (phase == Phase::BetweenScans ? kBatchGapSeconds : kSettleSeconds))
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
