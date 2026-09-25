#pragma once
#include "Features/PerformanceOverlay/DrawCallRow.h"
#include <array>
#include <chrono>
#include <memory>
#include <vector>

// A/B Testing constants
constexpr size_t kFrameHistoryBaseline = 30;
constexpr size_t kMinimumFramesForAnalysis = 10;
constexpr float kOutlierMultiplier = 3.0f;
constexpr float kMaxOutlierFrameTime = 100.0f;

// Sample coverage thresholds apply independently to both variants.
constexpr int kMinimumSamplesForValidity = 100;
constexpr float kMinimumTestDuration = 10.0f;        // At least 10 seconds
constexpr float kMinimumValidFramesPercent = 80.0f;  // At least 80% valid frames
constexpr int kMinimumSamplesForMarginal = 30;       // Minimum for marginal validity
constexpr float kMinimumDurationForMarginal = 5.0f;  // Minimum duration for marginal validity

// Only define ABVariant here
enum class ABVariant
{
	A,
	B
};

struct AggregatedDrawCallStats
{
	std::string label;
	int shaderType;
	float meanA = 0.0f, meanB = 0.0f, delta = 0.0f;
	float medianA = 0.0f, medianB = 0.0f;
	int frameCountA = 0, frameCountB = 0;
	float totalTimeA = 0.0f, totalTimeB = 0.0f;
	/** Comparisons require actual measurements from both configurations. */
	bool HasBothVariants() const { return frameCountA > 0 && frameCountB > 0; }
};

struct ABVariantStatistics
{
	int frames = 0;
	int excludedFrames = 0;
	float duration = 0.0f;

	float ValidPercent() const { return frames + excludedFrames > 0 ? 100.0f * frames / (frames + excludedFrames) : 0.0f; }
};

enum class ABTestCoverage
{
	Insufficient,
	Marginal,
	Sufficient
};

struct ABInterval
{
	ABVariant variant;
	std::vector<std::vector<DrawCallRow>> frameRows;
	std::chrono::steady_clock::time_point startTime;
	std::chrono::steady_clock::time_point endTime;
	bool warmup = false;
	int excludedFrames = 0;  // Frames excluded due to outliers or shader compilation
};

class ABTestAggregator
{
public:
	using Clock = std::chrono::steady_clock;

	/** Start an interval; initial B intervals are unmeasured until A starts. */
	void OnABSwitch(ABVariant variant, Clock::time_point now = Clock::now());
	void OnFrame(const std::vector<DrawCallRow>& rows);
	/** Finalize the measured interval, or discard an unfinished warm-up. */
	void OnTestEnd(Clock::time_point now = Clock::now());
	std::vector<AggregatedDrawCallStats> GetAggregatedResults() const;
	bool HasResults() const { return !intervals.empty(); }
	/** True while the initial unmeasured Variant B interval is active. */
	bool IsWarmingUp() const { return currentInterval && currentInterval->warmup; }
	void Clear();

	// Test statistics
	/** Completed measured intervals for one variant, excluding warm-up. */
	ABVariantStatistics GetVariantStatistics(ABVariant variant) const;
	/** Sample coverage requires both variants to meet the same thresholds. */
	ABTestCoverage GetCoverage() const;
	float GetTotalTestDuration() const;
	int GetTotalFrameCount() const;
	std::chrono::steady_clock::time_point GetTestStartTime() const { return testStartTime; }
	std::chrono::steady_clock::time_point GetTestEndTime() const { return testEndTime; }
	const std::vector<ABInterval>& GetIntervals() const { return intervals; }

private:
	void FinishInterval(Clock::time_point now);
	std::vector<ABInterval> intervals;
	std::unique_ptr<ABInterval> currentInterval;

	// Test timing
	std::chrono::steady_clock::time_point testStartTime;
	std::chrono::steady_clock::time_point testEndTime;

	// Frame history for outlier detection
	std::array<std::vector<float>, 2> recentFrameTimes;
	bool initialBWarmupPending = true;
};
