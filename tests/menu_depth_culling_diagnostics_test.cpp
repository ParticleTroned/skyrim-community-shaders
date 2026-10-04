#include "MenuDepthCullingDiagnostics.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	void PreservesInactiveAndFallbackEvidence()
	{
		VRDepthCullingTemporal::Status temporal{};
		VRHybridCulling::Status hybrid{};
		temporal.telemetryEnabled = false;
		temporal.telemetryFrozen = true;
		const auto empty = MenuDepthCullingDiagnostics::BuildStatus(temporal, hybrid);
		Require(!empty.at("telemetryEnabled").get<bool>(), "Disabled telemetry was lost");
		Require(empty.at("telemetryFrozen").get<bool>(), "Drained telemetry admission was lost");
		Require(empty.at("hybrid").at("submittedBatches") == 0, "An empty window acquired a submission");
		Require(empty.at("policy") == "balanced", "Advanced policy identity changed");
		Require(empty.at("measurementWindow").at("id") == 0 &&
					!empty.at("measurementWindow").at("current").get<bool>(),
			"An unreset window was reported as comparable");

		temporal.mode = VRDepthCullingTemporal::Mode::Hybrid;
		temporal.cullingEpoch = 42;
		temporal.measurementWindowId = 2;
		temporal.measurementStartEpoch = 41;
		temporal.measurementStartFrame = 123;
		temporal.measurementWindowCurrent = false;
		hybrid.effectiveBackend = "native";
		hybrid.fallbackReason = "resource_publication_changed";
		hybrid.fallbackBatches = 3;
		hybrid.invalidatedBatches = 2;
		hybrid.promotedObjects = 17;
		const auto fallback = MenuDepthCullingDiagnostics::BuildStatus(temporal, hybrid);
		Require(fallback.at("policy") == "hybrid" && fallback.at("hybrid").at("effectiveBackend") == "native",
			"Requested and effective backends were conflated");
		Require(fallback.at("hybrid").at("fallbackBatches") == 3 &&
					fallback.at("hybrid").at("invalidatedBatches") == 2 &&
					fallback.at("hybrid").at("promotedObjects") == 17,
			"Fallback or recovery evidence was discarded");
		Require(fallback.at("hybrid").at("fallbackReason") == "resource_publication_changed",
			"Resource rejection reason was lost while counters were disabled");
		Require(fallback.at("measurementWindow").at("id") == 2 &&
					fallback.at("measurementWindow").at("startEpoch") == 41 &&
					fallback.at("measurementWindow").at("startFrame") == 123 &&
					!fallback.at("measurementWindow").at("current").get<bool>(),
			"A mixed-epoch measurement window lost its identity");
	}

	void DistinguishesMissingSourceAndUnmeasuredTiming()
	{
		VRDepthCullingTemporal::Status temporal{};
		VRHybridCulling::Status hybrid{};
		auto status = MenuDepthCullingDiagnostics::BuildStatus(temporal, hybrid).at("hybrid");
		Require(status.at("sourceSnapshot").at("source").is_null(), "Missing source was fabricated");
		Require(status.at("cpuTimings").at("readback").at("meanNanoseconds").is_null(),
			"Unmeasured timing was reported as zero cost");
		hybrid.snapshot.available = true;
		hybrid.snapshot.current = false;
		hybrid.snapshot.validity = "stale_epoch";
		hybrid.snapshot.source.frame = 90;
		hybrid.snapshot.source.resourceGeneration = 12;
		hybrid.snapshot.source.phase = VRHybridCullingSnapshot::Phase::NativeDownscale;
		hybrid.snapshot.eyes = { VRHybridCullingPolicy::EyeRect{ 0, 0, 100, 70 },
			VRHybridCullingPolicy::EyeRect{ 100, 0, 100, 70 } };
		hybrid.snapshot.pyramid = { 32, 32, 5, 4 };
		hybrid.snapshot.logicalPyramidBytes = 10912;
		hybrid.snapshot.viewProjection[1][2][3] = 5.0f;
		hybrid.snapshot.unjitteredProjection[0][3][2] = 7.0f;
		hybrid.snapshot.cameraAdjust[1][0] = 9.0f;
		hybrid.readback.samples = 2;
		hybrid.readback.totalNanoseconds = 128'000'000;
		hybrid.readback.maximumNanoseconds = 100'000'000;
		hybrid.readback.durationHistogram.back() = 1;
		hybrid.fallbackReasonCounts.back() = 7;
		status = MenuDepthCullingDiagnostics::BuildStatus(temporal, hybrid).at("hybrid");
		const auto& snapshot = status.at("sourceSnapshot");
		Require(!snapshot.at("current").get<bool>() && snapshot.at("validity") == "stale_epoch",
			"Retained source metadata was promoted to current");
		Require(snapshot.at("source").at("frame") == 90 && snapshot.at("source").at("resourceGeneration") == 12 &&
					!snapshot.at("source").at("contentFreshnessProven").get<bool>(),
			"Observation identity was lost or overstated");
		Require(snapshot.at("eyes").at(1).at("x") == 100 && snapshot.at("pyramid").at("logicalBytes") == 10912,
			"Stereo layout or logical memory accounting was lost");
		Require(snapshot.at("observedCamera").at("viewProjection").at(1).at(2).at(3) == 5.0f &&
					snapshot.at("observedCamera").at("unjitteredProjection").at(0).at(3).at(2) == 7.0f &&
					snapshot.at("observedCamera").at("cameraAdjust").at(1).at(0) == 9.0f,
			"Observed camera data changed layout during serialization");
		const auto& timing = status.at("cpuTimings").at("readback");
		Require(timing.at("meanNanoseconds") == 64'000'000.0 &&
					timing.at("durationHistogramNanoseconds").at("upperBounds").back().is_null() &&
					timing.at("durationHistogramNanoseconds").at("counts").back() == 1,
			"A long validation sample lost its timing or overflow bucket");
		Require(status.at("fallbackReasonCounts").at(VRHybridCulling::FallbackReasons.back()) == 7 &&
					status.at("fallbackReasonCounts").at(VRHybridCulling::FallbackReasons.front()) == 0,
			"Reason history lost measured or zero values");
	}

	void DistinguishesObservedEngineStateFromDesiredPolicy()
	{
		VRDepthCullingTemporal::Status temporal{};
		VRHybridCulling::Status hybrid{};
		temporal.cullingEnabled = true;
		const auto snapshot = [&]() { return MenuDepthCullingDiagnostics::BuildStatus(temporal, hybrid); };
		auto status = snapshot();
		const auto& missing = status.at("engine");
		Require(status.at("cullingEnabled").get<bool>() && missing.at("depthBufferCulling").is_null() &&
					!missing.at("depthBufferCullingAvailable").get<bool>() && missing.at("minimumOccludeeBoxExtent").is_null() &&
					!missing.at("minimumOccludeeBoxExtentAvailable").get<bool>(),
			"Desired culling policy fabricated unavailable engine observations");
		temporal.engineCullingEnabled = false;
		temporal.engineMinimumExtent = 500.0f;
		status = snapshot();
		const auto& observed = status.at("engine");
		Require(status.at("cullingEnabled").get<bool>() && observed.at("depthBufferCullingAvailable").get<bool>() &&
					!observed.at("depthBufferCulling").get<bool>() && observed.at("minimumOccludeeBoxExtentAvailable").get<bool>() &&
					observed.at("minimumOccludeeBoxExtent") == 500.0f,
			"Engine observations were replaced by desired state or lost a false engine gate");
		temporal.engineCullingEnabled = true;
		temporal.engineMinimumExtent = 0.0f;
		const auto zero = snapshot().at("engine");
		Require(zero.at("depthBufferCulling").get<bool>() && zero.at("minimumOccludeeBoxExtent") == 0.0f &&
					zero.at("minimumOccludeeBoxExtentAvailable").get<bool>(),
			"Measured zero extent was treated as unavailable");
	}

	void PreservesGuardedConfigurationAndEffectiveReduction()
	{
		VRDepthCullingTemporal::Status temporal{};
		VRHybridCulling::Status hybrid{};
		const auto snapshot = [&]() { return MenuDepthCullingDiagnostics::BuildStatus(temporal, hybrid).at("hybrid").at("configuration"); };
		auto configuration = snapshot();
		Require(configuration.at("proof") == "guarded" && configuration.at("preferredSourceReduction") == 2 &&
					configuration.at("activeSourceReduction") == 0 && !configuration.at("largeSourceFallback").get<bool>(),
			"Inactive Hybrid fabricated an effective reduction or lost its guarded preference");
		hybrid.sourceReductionActive = 2;
		configuration = snapshot();
		Require(configuration.at("activeSourceReduction") == 2 && !configuration.at("largeSourceFallback").get<bool>(),
			"Submitted fine depth was reported as resource-limit fallback");
		hybrid.sourceReductionActive = 4;
		configuration = snapshot();
		Require(configuration.at("preferredSourceReduction") == 2 && configuration.at("activeSourceReduction") == 4 &&
					configuration.at("largeSourceFallback").get<bool>(),
			"Large-source fallback hid the fixed fine preference");
		hybrid.sourceReductionActive = 0;
		Require(!snapshot().at("largeSourceFallback").get<bool>(), "Inactive Hybrid retained an effective fallback claim");
	}

	void SeparatesNativeReadbackFromValidation()
	{
		VRDepthCullingTemporal::Status temporal{};
		VRHybridCulling::Status hybrid{};
		temporal.nativeReadback.samples = 1;
		temporal.nativeReadback.totalNanoseconds = 80'000'000;
		temporal.nativeReadback.durationHistogram.back() = 1;
		hybrid.readback.samples = 1;
		hybrid.readback.totalNanoseconds = 2'000;
		const auto status = MenuDepthCullingDiagnostics::BuildStatus(temporal, hybrid);
		Require(status.at("cpuTimings").at("nativeReadback").at("meanNanoseconds") == 80'000'000.0 &&
					status.at("hybrid").at("cpuTimings").at("readback").at("meanNanoseconds") == 2'000.0,
			"Native blocking work was conflated with Hybrid validation");
		Require(status.at("cpuTimings").at("replayDownscale").at("meanNanoseconds").is_null(),
			"An unexecuted replay was reported as zero cost");
	}

	void ValidatesTraversalRecords()
	{
		using VRHybridCullingDiagnostics::Record;
		using VRHybridCullingDiagnostics::Summarize;
		std::array records{ Record{ 257, 8, 0, 0 }, Record{ 5, 64, 20, 100, 20, 15, 3, 4 },
			Record{ 1537, 12, 4, 9, 1, 2, 1, 2 }, Record{ 8, 5, 0, 0 }, Record{ 2049, 9, 0, 0 } };
		const std::array<std::uint32_t, 5> visibility{ 0, 1, 1, 1, 1 };
		const auto totals = Summarize(records, visibility);
		Require(totals && totals->objects == 5 && totals->depthLoads == 98 &&
					totals->eyeReasons[1] == 4 && totals->eyeReasons[0] == 2 &&
					totals->eyeReasons[5] == 1 && totals->eyeReasons[6] == 1 && totals->eyeReasons[8] == 2 &&
					totals->planeProofs == 21 && totals->polygonClips == 17 && totals->faceBiasOnlyProofs == 4 && totals->triangleBiasOnlyProofs == 6,
			"Traversal records lost skipped eyes, terminal reasons or work");
		for (const auto invalid : { Record{ 8, 3, 0, 0 }, Record{ 8, 6, 0, 0 }, Record{ 8, 5, 1, 0 }, Record{ 2049, 70, 0, 0 } }) {
			records[3] = invalid;
			Require(!Summarize(records, visibility), "Malformed nearest-vertex work was published");
		}
		records[3] = Record{ 8, 5, 0, 0 };
		for (const auto invalid : { Record{ 0, 0, 0, 0 }, Record{ 256, 8, 0, 0 }, Record{ 258, 8, 0, 0 },
				 Record{ 257, 129, 0, 0 }, Record{ 257, 8, 9, 0 }, Record{ 257, 8, 1, 13 }, Record{ 0x10001, 8, 0, 0 }, Record{ 5, 63, 0, 0 }, Record{ 6, 65, 0, 0 }, Record{ 1281, 67, 0, 0 }, Record{ 9, 5, 0, 0 } }) {
			records[0] = invalid;
			Require(!Summarize(records, visibility), "Malformed traversal records were partially published");
		}
		records[0] = Record{ 5, 64, 20, 100 };
		Require(!Summarize(records, visibility), "Traversal records disagreed with native-indexed visibility");
		VRDepthCullingTemporal::Status temporal{};
		VRHybridCulling::Status hybrid{};
		hybrid.traversalDiagnosticsEnabled = true;
		hybrid.traversalDiagnosticsAvailable = true;
		hybrid.traversal = *totals;
		hybrid.traversalBatches = 1;
		hybrid.traversalDiagnosticsAvailability = "setup_failed";
		hybrid.traversalSubmittedBatches = 10;
		hybrid.traversalUnavailableBatches = 6;
		hybrid.traversalNotReadyBatches = 2;
		hybrid.traversalFailedBatches = 3;
		hybrid.traversalDiscardedBatches = 4;
		const auto status = MenuDepthCullingDiagnostics::BuildStatus(temporal, hybrid).at("hybrid").at("traversalDiagnostics");
		Require(status.at("objects") == 5 && status.at("depthLoads") == 98 && status.at("eyeReasons").at("depth_budget") == 1 &&
					status.at("eyeReasons").at("nearest_unresolved") == 2 &&
					status.at("planeProofs") == 21 && status.at("polygonClips") == 17 &&
					status.at("faceBiasOnlyProofs") == 4 && status.at("triangleBiasOnlyProofs") == 6 &&
					status.at("notReadyBatches") == 2 && status.at("failedBatches") == 3 && status.at("discardedBatches") == 4 &&
					status.at("submittedBatches") == 10 && status.at("unavailableBatches") == 6 && status.at("availability") == "setup_failed",
			"Traversal status lost measurements or missing-readback evidence");
	}

	void ValidatesProofCountersAndViewportReasons()
	{
		using VRHybridCullingDiagnostics::Record;
		using VRHybridCullingDiagnostics::Summarize;
		const std::array records{ Record{ 3, 0, 0, 0 }, Record{ 9, 0, 0, 0 }, Record{ 10, 0, 0, 0 },
			Record{ 2305, 8, 1, 2, 1, 1, 0, 0 }, Record{ 2561, 8, 1, 2, 0, 1, 1, 1 } };
		const std::array<std::uint32_t, 5> visible{ 1, 1, 1, 1, 1 };
		const auto totals = Summarize(records, visible);
		Require(totals && totals->eyeReasons[3] == 1 && totals->eyeReasons[9] == 2 && totals->eyeReasons[10] == 2,
			"Guard-only, wholly offscreen and partially clipped reasons were conflated");
		VRHybridCulling::Status hybrid{};
		hybrid.traversal = *totals;
		const auto status = MenuDepthCullingDiagnostics::BuildStatus({}, hybrid).at("hybrid").at("traversalDiagnostics");
		Require(status.at("eyeReasons").at("viewport_guard") == 1 && status.at("eyeReasons").at("viewport_offscreen") == 2 &&
					status.at("eyeReasons").at("viewport_partial") == 2 && status.at("planeProofs") == 1 && status.at("polygonClips") == 2 &&
					status.at("faceBiasOnlyProofs") == 1 && status.at("triangleBiasOnlyProofs") == 1,
			"Viewport reasons or mutually exclusive proof counters were lost");
		const auto empty = MenuDepthCullingDiagnostics::BuildStatus({}, {}).at("hybrid").at("traversalDiagnostics");
		Require(empty.at("planeProofs") == 0 && empty.at("polygonClips") == 0 &&
					empty.at("faceBiasOnlyProofs") == 0 && empty.at("triangleBiasOnlyProofs") == 0 &&
					empty.at("eyeReasons").at("viewport_offscreen") == 0 && empty.at("eyeReasons").at("viewport_partial") == 0,
			"An unmeasured window lost explicit zero proof or viewport counters");

		const std::array<std::uint32_t, 1> occluded{ 0 };
		for (const auto boundary : { Record{ 257, 128, 128, 1536, 1536, 0, 0, 0 },
				 Record{ 257, 128, 128, 1536, 0, 1536, 0, 0 }, Record{ 257, 128, 128, 0, 0, 0, 768, 0 },
				 Record{ 257, 8, 1, 2, 0, 1, 5, 1 } })
			Require(Summarize(std::array{ boundary }, occluded).has_value(), "Valid proof-counter boundary was rejected");
		const auto maximum = std::numeric_limits<std::uint32_t>::max();
		for (const auto malformed : { Record{ 257, 8, 1, 2, 1, 1, 0, 1 }, Record{ 257, 8, 1, 0, 0, 0, 7, 0 },
				 Record{ 257, 8, 1, 3, 0, 0, 5, 0 }, Record{ 257, 8, 1, 2, maximum, 1, 0, 0 },
				 Record{ 257, 8, 1, 2, 0, maximum, 0, 1 }, Record{ 257, 8, 1, 2, 1, 0, 0, maximum },
				 Record{ 257, 8, 1, 2, 0, 0, maximum, 0 } })
			Require(!Summarize(std::array{ malformed }, occluded), "Invalid or overflowing proof counters were published");
		const std::array<std::uint32_t, 1> retained{ 1 };
		for (const auto malformed : { Record{ 2, 1, 0, 0 }, Record{ 3, 1, 0, 0 }, Record{ 9, 1, 0, 0 },
				 Record{ 10, 1, 0, 0 }, Record{ 11, 0, 0, 0 }, Record{ 2817, 8, 0, 0 } })
			Require(!Summarize(std::array{ malformed }, retained), "Invalid viewport work or unknown reasons were published");
	}

	void PreservesNativeVisibilityBeforeAndAfterRecovery()
	{
		VRDepthCullingTemporal::Status temporal{};
		VRHybridCulling::Status hybrid{};
		const auto empty = MenuDepthCullingDiagnostics::BuildStatus(temporal, hybrid).at("nativeVisibility");
		Require(empty.at("batches") == 0 && empty.at("testedObjects") == 0 && empty.at("unreadableBatches") == 0,
			"Native visibility fabricated an unmeasured batch");
		temporal.nativeVisibility = { 2, 1, 3, 8, 5, 3, 4, 4 };
		hybrid.acceptedOccludedObjects = 100;
		const auto status = MenuDepthCullingDiagnostics::BuildStatus(temporal, hybrid);
		const auto& native = status.at("nativeVisibility");
		Require(native.at("batches") == 2 && native.at("emptyBatches") == 1 && native.at("unreadableBatches") == 3 && native.at("testedObjects") == 8 &&
					native.at("occludedBeforeRecovery") == 5 && native.at("visibleBeforeRecovery") == 3 &&
					native.at("occludedAfterRecovery") == 4 && native.at("visibleAfterRecovery") == 4 &&
					status.at("hybrid").at("acceptedOccludedObjects") == 100,
			"Native recovery or Hybrid result ownership was lost in serialization");
	}
}

int main()
{
	try {
		ValidatesTraversalRecords();
		ValidatesProofCountersAndViewportReasons();
		PreservesInactiveAndFallbackEvidence();
		DistinguishesObservedEngineStateFromDesiredPolicy();
		PreservesGuardedConfigurationAndEffectiveReduction();
		DistinguishesMissingSourceAndUnmeasuredTiming();
		SeparatesNativeReadbackFromValidation();
		PreservesNativeVisibilityBeforeAndAfterRecovery();
		std::cout << "Depth-culling diagnostic serialization passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
