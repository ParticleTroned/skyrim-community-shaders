#include "MenuDepthCullingDiagnostics.h"

#include <iostream>
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
		std::array records{ Record{ 257, 8, 0, 0 }, Record{ 5, 64, 20, 100 }, Record{ 1537, 12, 4, 9 }, Record{ 8, 5, 0, 0 }, Record{ 2049, 9, 0, 0 } };
		const std::array<std::uint32_t, 5> visibility{ 0, 1, 1, 1, 1 };
		const auto totals = Summarize(records, visibility);
		Require(totals && totals->objects == 5 && totals->depthLoads == 98 &&
					totals->eyeReasons[1] == 4 && totals->eyeReasons[0] == 2 &&
					totals->eyeReasons[5] == 1 && totals->eyeReasons[6] == 1 && totals->eyeReasons[8] == 2,
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
					status.at("notReadyBatches") == 2 && status.at("failedBatches") == 3 && status.at("discardedBatches") == 4 &&
					status.at("submittedBatches") == 10 && status.at("unavailableBatches") == 6 && status.at("availability") == "setup_failed",
			"Traversal status lost measurements or missing-readback evidence");
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
		PreservesInactiveAndFallbackEvidence();
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
