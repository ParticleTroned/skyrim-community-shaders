#include "../src/Features/Upscaling/NeuralRendering/PackedCropPolicy.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "../src/Features/Upscaling/NeuralRendering/PackedCropEvidenceJson.h"
#endif

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace NeuralRendering;

static void Require(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << std::endl;
		throw std::runtime_error(message);
	}
}

static void CheckCapacityRetention()
{
	constexpr UpscalingDLSS::Extent size{ 2048, 1024 };
	constexpr ComputeSubrect full{ 0, 0, size.width, size.height };
	PackedCropSources sources{ { ComputeSubrect{ 32, 40, 137, 185 }, ComputeSubrect{ 1500, 200, 137, 185 } }, 2 };
	PackedCropCapacityState state;
	std::uint32_t frame = 0;
	const auto resolve = [&](std::uint64_t baseline = std::numeric_limits<std::uint64_t>::max()) {
		return ResolvePackedCropLayout(sources, size, size, full, baseline, frame, state);
	};
	for (frame = 1; frame < kPackedCropAdmissionFrames; ++frame) {
		Require(!resolve().count, "Unsettled candidates allocated native atlas models");
		const auto stable = state.stableFrames;
		Require(!resolve().count && state.stableFrames == stable, "Repeated projection advanced the settling window");
	}
	const auto admitted = resolve();
	Require(admitted.count == 2 && state.active, "Stable crops never became eligible");
	Require(admitted.extent.width % kPackedCropCapacityQuantum == 0 && admitted.extent.height % kPackedCropCapacityQuantum == 0,
		"Atlas allocation did not reserve bounded reusable capacity");
	const auto capacity = admitted.extent;
	bool mappingChanged = false;
	for (unsigned i = 0; i < 1000; ++i) {
		++frame;
		sources.regions[0].baseX = 32 + i % 13;
		sources.regions[0].width = 137 + i % 12;
		sources.regions[1].height = 185 - i % 9;
		if (i % 2)
			std::swap(sources.regions[0], sources.regions[1]);
		const auto layout = resolve();
		Require(layout.count == 2 && layout.extent == capacity, "Small movement, resizing or enumeration churned native capacity");
		mappingChanged |= layout != admitted;
		for (unsigned tile = 0; tile < layout.count; ++tile) {
			Require(layout.tiles[tile].source == sources.regions[tile], "Capacity reuse froze stale source coordinates");
			Require(layout.tiles[tile].packed.Fits(capacity.width, capacity.height), "Repacked tile escaped capacity");
		}
		if (i % 2)
			std::swap(sources.regions[0], sources.regions[1]);
	}
	Require(mappingChanged, "Allocation stability incorrectly froze temporal identity");
	// Enumeration and motion cannot swap two actors' model neighbourhoods.
	sources = { { ComputeSubrect{ 32, 40, 137, 185 }, ComputeSubrect{ 1500, 200, 137, 185 } }, 2 };
	++frame;
	const auto before = resolve();
	sources.regions[0].baseX += 8;
	sources.regions[1].baseY += 8;
	std::swap(sources.regions[0], sources.regions[1]);
	++frame;
	const auto after = resolve();
	Require(after.count == 2 && after.tiles[0].packed == before.tiles[1].packed && after.tiles[1].packed == before.tiles[0].packed,
		"Source motion or enumeration moved retained atlas positions");
	std::swap(sources.regions[0], sources.regions[1]);
	++frame;
	Require(!resolve(10000).count && !state.active, "Area gate ignored the full retained canvas");
	const auto fallbackFrame = frame;
	for (unsigned i = 1; i <= kPackedCropRecoveryFrames; ++i) {
		frame = fallbackFrame + i;
		const auto layout = resolve();
		Require(bool(layout.count) == (i == kPackedCropRecoveryFrames), "Failed capacity reentered without sustained stability");
	}
	// A selection that cannot fit must fall back now, never clip or wait on stale source mappings.
	sources.regions[0].width = 500;
	++frame;
	Require(!resolve().count && !state.active, "Growing selection reused an undersized allocation");
	const auto pending = state.capacity;
	Require(pending.width >= 500 || pending.height >= 500, "Pending growth did not account for the new selection");
	for (unsigned i = 0; i < kPackedCropRecoveryFrames / 2; ++i) {
		++frame;
		(void)resolve();
	}
	frame += 10;
	Require(!resolve().count && state.stableFrames == 1, "A frame gap counted as a continuous stable window");
	sources.count = 0;
	++frame;
	Require(!resolve().count && !state.capacity.IsValid() && state.stableFrames == 0, "Empty selection retained a stale admission");
	PackedCropCapacityState wrap;
	for (std::uint32_t i = 0; i < kPackedCropAdmissionFrames; ++i) {
		const auto layout = ResolvePackedCropLayout(admitted.count ? PackedCropSources{ { admitted.tiles[0].source, admitted.tiles[1].source }, 2 } : PackedCropSources{},
			size, size, full, full.Area(), UINT32_MAX - 3u + i, wrap);
		Require(bool(layout.count) == (i + 1 == kPackedCropAdmissionFrames), "Frame wrap broke bounded admission");
	}
}

#ifdef DEVBENCH_BRIDGE_ENABLED
static void CheckTransitionCapture()
{
	PackedCropDiagnostics capture;
	PackedCropObservation active;
	active.frame = active.sourceFrame = 100;
	active.generation = UINT64_MAX - 1;
	active.slot = 2;
	active.source = active.model = { 2048, 1024 };
	active.available = { 0, 0, 2048, 1024 };
	active.baselinePixels = active.available.Area();
	active.sources = { { ComputeSubrect{ 32, 40, 137, 185 }, ComputeSubrect{ 1500, 200, 137, 185 } }, 2 };
	active.state = { .capacity = { 384, 256 }, .stableFrames = 120, .requiredFrames = 120, .active = true };
	active.reason = PackedCropReason::Admitted;
	capture.Observe(active, std::nullopt);
	Require(capture.Total() == 0, "Disabled capture retained per-frame evidence");
	capture.Configure(true, true);
	capture.Observe(active, std::nullopt);
	for (unsigned i = 0; i < 1000; ++i) {
		++active.frame;
		++active.sourceFrame;
		active.sources.regions[0].baseX += i % 2;
		capture.Observe(active, std::nullopt);
	}
	Require(capture.Total() == 1, "Stable capacity recorded every mapping change");
	auto rejected = active;
	++rejected.frame;
	++rejected.sourceFrame;
	rejected.state.active = false;
	rejected.state.requiredFrames = kPackedCropRecoveryFrames;
	rejected.reason = PackedCropReason::CapacitySettling;
	PackedCropAdmissionDiagnostics failed;
	failed.reason = PackedCropReason::InsufficientAreaSaving;
	failed.candidateAreaAvailable = failed.modelAreasAvailable = true;
	failed.candidateExtent = active.state.capacity;
	failed.candidateModelPixels = 98304;
	failed.effectiveBaselineModelPixels = 100000;
	capture.Observe(rejected, failed);
	Require(capture.Total() == 2 && capture.FirstRejection(), "First active-capacity rejection was not pinned");
	const auto sequence = capture.Pending(rejected.slot, rejected.frame, rejected.sourceFrame, rejected.generation);
	Require(sequence == 2 && capture.Pending(3, rejected.frame, rejected.sourceFrame, rejected.generation) == 0 &&
				capture.Pending(rejected.slot, rejected.frame + 1, rejected.sourceFrame, rejected.generation) == 0 &&
				capture.Pending(rejected.slot, rejected.frame, rejected.sourceFrame + 1, rejected.generation) == 0 &&
				capture.Pending(rejected.slot, rejected.frame, rejected.sourceFrame, 0) == 0,
		"Timing correlation accepted a different eye, frame, source or generation");
	capture.Update(sequence, [](auto& timings) { timings.gpuDrainMicroseconds = 0; timings.nativeCreateMicroseconds = 90000; timings.nativeCreateSucceeded = false; });
	auto json = PackedCropDiagnosticsJson(capture);
	Require(json["firstRejection"]["retainedCapacityTest"]["reason"] == "less_than_25_percent_area_saving" &&
				json["firstRejection"]["current"]["reason"] == "atlas_capacity_settling",
		"Fallback overwrote the actual failed test");
	Require(json["firstRejection"]["cpuMicroseconds"]["gpuDrain"] == 0 &&
				json["firstRejection"]["cpuMicroseconds"]["nativeRelease"].is_null() &&
				json["firstRejection"]["outcomes"]["nativeCreateSucceeded"] == false,
		"Zero, unknown timing or failed outcome lost");
	Require(json["firstRejection"]["current"]["generation"] == "18446744073709551614", "Generation precision lost");
	for (unsigned i = 0; i < 70; ++i) {
		++rejected.frame;
		++rejected.sourceFrame;
		rejected.state.active = !rejected.state.active;
		capture.Observe(rejected, std::nullopt);
	}
	capture.Update(sequence, [](auto& timings) { timings.nativeCreateMicroseconds = 1; });
	json = PackedCropDiagnosticsJson(capture);
	Require(json["records"].size() == 32 && json["overwrittenRecords"] == 40 &&
				json["firstRejection"]["sequence"] == 2 && json["firstRejection"]["cpuMicroseconds"]["nativeCreate"] == 90000,
		"Ring overwrite or stale completion changed the first rejection");
	capture.Configure(false, false);
	capture.Observe(active, std::nullopt);
	Require(capture.Total() == 72 && capture.FirstRejection() && capture.Pending(rejected.slot, rejected.frame, rejected.sourceFrame, rejected.generation) == 0,
		"Stopping capture lost evidence or retained a live timer");
	capture.Configure(std::nullopt, true);
	Require(!capture.Enabled() && capture.Total() == 0 && !capture.FirstRejection(), "Clear changed enable state or retained old events");

	capture.Configure(true, true);
	auto inactive = active;
	inactive.state.active = false;
	inactive.requested = true;
	inactive.reason = PackedCropReason::TooFewRegions;
	inactive.sourceTransaction = 123;
	capture.Observe(inactive, std::nullopt);
	Require(capture.FirstRejection() && capture.Total() == 1, "Never-admitted rejection was not retained");
	Require(!capture.Pending(inactive.slot, inactive.frame, inactive.sourceFrame, inactive.generation, 456),
		"Timing correlation accepted a different source transaction");
	++inactive.frame;
	inactive.model.width -= 128;
	inactive.geometryChanged = true;
	capture.Observe(inactive, std::nullopt);
	Require(capture.Total() == 2, "Inactive resolution rebuild was not recorded");
	inactive.geometryChanged = false;
	++inactive.frame;
	capture.Observe(inactive, std::nullopt);
	Require(capture.Total() == 2, "Stable inactive frames flooded the ring");
	const auto allocation = capture.CostEvent(inactive.slot, inactive.frame, inactive.sourceFrame, inactive.generation, 123);
	Require(allocation == 3 && capture.CostEvent(inactive.slot, inactive.frame, inactive.sourceFrame, inactive.generation, 123) == allocation,
		"Allocation-only evidence was missing or duplicated");
	capture.RecordEvaluation(inactive.slot, true, true, 100);
	capture.RecordEvaluation(inactive.slot, false, true, 400);
	capture.RecordEvaluation(inactive.slot, true, false, 100);
	const auto counts = capture.Evaluations()[inactive.slot];
	Require(counts.packed == 1 && counts.single == 1 && counts.failed == 1 && counts.packedPixels == 100 && counts.singlePixels == 400,
		"Native execution counts confused planned packing, failed calls and successful evaluations");
	capture.Configure(false, false);
	capture.RecordEvaluation(inactive.slot, true, true, 100);
	Require(capture.Evaluations()[inactive.slot].packed == 1, "Disabled diagnostics recorded evaluations");
	capture.Configure(std::nullopt, true);
	Require(capture.Evaluations()[inactive.slot].packed == 0, "Clear retained old inference counts");

	auto state = active.state;
	PackedCropAdmissionDiagnostics finalDecision;
	std::optional<PackedCropAdmissionDiagnostics> retainedFailure;
	const auto result = ResolvePackedCropLayout(active.sources, active.source, active.model, active.available, 100000,
		10000, state, &finalDecision, &retainedFailure);
	Require(!result.count && retainedFailure && retainedFailure->reason == PackedCropReason::InsufficientAreaSaving &&
				retainedFailure->capacity.capacity == active.state.capacity && retainedFailure->candidateModelPixels == 98304,
		"Actual policy discarded the failed retained capacity before searching replacements");
}
#endif

int main()
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	CheckTransitionCapture();
#endif
	CheckCapacityRetention();
	constexpr ComputeSubrect full{ 0, 0, 2048, 1024 };
	std::array actors{ CharacterRect{ 200, 300, 280, 400 }, CharacterRect{ 1500, 300, 1580, 400 } };
	const std::array closeActors{ CharacterRect{ 96, 160, 176, 320 }, CharacterRect{ 240, 160, 320, 320 },
		CharacterRect{ 384, 160, 464, 320 }, CharacterRect{ 528, 160, 608, 320 } };
	const auto tight = BuildPackedCropSources(closeActors, 2048, 1024, full);
	Require(tight.count == 4, "Neighbouring separated masks were merged by unnecessary context or alignment");
	for (unsigned i = 0; i < tight.count; ++i)
		Require(tight.regions[i] == BuildPackedCropContext(closeActors[i], 2048, 1024, full),
			"Crop context differs from its bounded source policy");
	StablePackedCropSources state;
	const auto initial = ResolvePackedCropSources(actors, 2048, 1024, full, state);
	Require(initial.count == 2, "Separated actors were not packed");
	for (const auto& actor : actors) {
		const auto support = BuildCharacterComputeSubrect(std::span(&actor, 1), 2048, 1024);
		Require(std::ranges::any_of(initial.regions, [&](auto rect) { return ContainsComputeSubrect(rect, support); }), "Selected actor coverage was discarded");
	}
	std::ranges::reverse(actors);
	Require(BuildPackedCropSources(actors, 2048, 1024, full) == initial, "Input enumeration changed the initial mapping");
	actors[0].minX += 8;
	actors[0].maxX += 8;
	for (unsigned i = 0; i < 120; ++i)
		Require(ResolvePackedCropSources(actors, 2048, 1024, full, state) == initial, "Contained motion churned the atlas");
	StablePackedCropSources contextState{ initial };
	auto contextMotion = actors;
	contextMotion[1].minX += 17;
	contextMotion[1].maxX += 17;
	const auto& movedActor = contextMotion[1];
	const ComputeSubrect movedPixels{ movedActor.minX, movedActor.minY, movedActor.maxX - movedActor.minX, movedActor.maxY - movedActor.minY };
	Require(std::ranges::any_of(initial.regions, [&](auto r) { return ContainsComputeSubrect(r, movedPixels); }), "Guard-motion fixture left the selected-pixel crop");
	const auto grown = ResolvePackedCropSources(contextMotion, 2048, 1024, full, contextState);
	Require(grown != initial && grown.count == 2, "Selected pixels inside the atlas concealed a sampling-guard escape");
	for (const auto& actor : contextMotion) {
		const auto required = BuildPackedCropContext(actor, 2048, 1024, full);
		Require(std::ranges::any_of(grown.regions, [&](auto r) { return ContainsComputeSubrect(r, required); }), "Atlas growth discarded required context");
	}
	StablePackedCropSources singleState{ initial };
	for (unsigned i = 1; i < kCharacterProviderRoiHistoryFrames; ++i)
		Require(ResolvePackedCropSources(std::span(actors).first(1), 2048, 1024, full, singleState) == initial, "A transient single actor churned atlas ownership");
	Require(!ResolvePackedCropSources(std::span(actors).first(1), 2048, 1024, full, singleState).count, "A departed actor retained an unused tile indefinitely");
	StablePackedCropSources transientState{ initial };
	for (unsigned i = 1; i < kCharacterProviderRoiHistoryFrames; ++i)
		(void)ResolvePackedCropSources(actors, 2048, 1024, full, transientState);
	Require(ResolvePackedCropSources(std::span(actors).first(1), 2048, 1024, full, transientState) == initial, "A one-frame disappearance at the periodic check retired the atlas");
	for (unsigned i = 0; i < 2 * kCharacterProviderRoiHistoryFrames; ++i)
		Require(ResolvePackedCropSources(i % 2 ? std::span(actors) : std::span(actors).first(1), 2048, 1024, full, transientState) == initial, "Intermittent selection churned the retained atlas");
	actors[0] = { 1800, 700, 1900, 800 };
	Require(ResolvePackedCropSources(actors, 2048, 1024, full, state) != initial, "Escaped selection reused stale history coordinates");
	Require(!ResolvePackedCropSources({}, 2048, 1024, full, state).count && !state.sources.count, "Empty selection retained an atlas");
	for (unsigned percent : { 30u, 75u, 100u }) {
		const UpscalingDLSS::Extent model{ (2048 * percent + 99) / 100, (1024 * percent + 99) / 100 };
		const auto layout = BuildPackedCropLayout(initial, { 2048, 1024 }, model, full);
		Require(layout.count == 2, "Admissible sparse layout was rejected");
		Require(std::uint64_t(layout.extent.width) * layout.extent.height * 4 <= std::uint64_t(model.width) * model.height * 3, "Padding removed the required area saving");
		Require(!IntersectComputeSubrect(layout.tiles[0].packed, layout.tiles[1].packed).IsValid(), "Atlas tiles overlap");
		for (unsigned i = 0; i < layout.count; ++i)
			Require(layout.tiles[i].packed.Fits(layout.extent.width, layout.extent.height), "Tile exceeds admitted texture size");
	}
	// The best unconstrained packing is vertical; the persistent backing requires a horizontal layout.
	const PackedCropSources wideSources{ { ComputeSubrect{ 16, 16, 304, 144 }, ComputeSubrect{ 640, 16, 304, 144 } }, 2 };
	const auto bounded = BuildPackedCropLayout(wideSources, { 1024, 256 }, { 1024, 256 }, { 0, 0, 1024, 256 });
	Require(bounded.count == 2 && bounded.extent.width <= 1024 && bounded.extent.height <= 256,
		"Atlas selected a canvas outside persistent single-ROI backing");
	const auto rejectCapacity = [&](UpscalingDLSS::Extent capacity) {
		return BuildPackedCropLayout(wideSources, { 1024, 256 }, { 1024, 256 }, { 0, 0, 1024, 256 }
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			nullptr
#endif
			,
			{ .capacity = capacity });
	};
	Require(!rejectCapacity({ 512, 512 }).count && !rejectCapacity({ 2048, 128 }).count,
		"Retained atlas capacity escaped single-ROI backing bounds");
	PackedCropSources dense{ { ComputeSubrect{ 0, 0, 1024, 1024 }, ComputeSubrect{ 1024, 0, 1024, 1024 } }, 2 };
	Require(!BuildPackedCropLayout(dense, { 2048, 1024 }, { 2048, 1024 }, full).count, "Dense contexts were packed without a saving");
	auto malformed = initial;
	malformed.count = 5;
	Require(!BuildPackedCropLayout(malformed, { 2048, 1024 }, { 2048, 1024 }, full).count, "Unbounded tile count was admitted");
	malformed = initial;
	malformed.regions[1] = malformed.regions[0];
	Require(!BuildPackedCropLayout(malformed, { 2048, 1024 }, { 2048, 1024 }, full).count, "Ambiguous source ownership was admitted");
	Require(!BuildPackedCropSources(actors, UINT32_MAX, 1024, { 0, 0, UINT32_MAX, 1024 }).count, "Oversize source escaped validation");
	Require(!BuildPackedCropLayout(initial, { UINT32_MAX, 1024 }, { 2048, 1024 }, full).count, "Invalid source dimension reached layout arithmetic");
	std::vector<CharacterRect> crowded;
	for (unsigned x = 100; x < 500; x += 32)
		crowded.push_back({ x, 300, x + 40, 400 });
	Require(!BuildPackedCropSources(crowded, 2048, 1024, full).count, "Overlapping context chain did not merge to one enclosing crop");
	const std::array edge{ CharacterRect{ 0, 0, 16, 16 }, CharacterRect{ 2030, 1000, 2048, 1024 } };
	const auto clipped = BuildPackedCropSources(edge, 2048, 1024, full);
	Require(clipped.count == 2, "Image-edge actors were dropped");
	for (unsigned i = 0; i < clipped.count; ++i)
		Require(clipped.regions[i].Fits(2048, 1024), "Context padding exceeded image bounds");
	const auto smallAtlas = BuildPackedCropLayout(clipped, { 2048, 1024 }, { 2048, 1024 }, full);
	Require(smallAtlas.count == 2 && QualifiedExperimentalContextGeometry(smallAtlas.extent),
		"Tight source crops bypassed the native minimum-dimension safety floor");
	const ComputeSubrect smallBaseline{ 0, 0, 64, 64 };
	const PackedCropSources smallSources{ { ComputeSubrect{ 0, 0, 16, 16 }, ComputeSubrect{ 32, 0, 16, 16 } }, 2 };
	Require(!BuildPackedCropLayout(smallSources, { 64, 64 }, { 64, 64 }, smallBaseline).count,
		"Native minimum-dimension allocation was omitted from the saving gate");
#ifdef DEVBENCH_BRIDGE_ENABLED
	PackedCropPlanningDiagnostics planning;
	const auto sparse = BuildPackedCropSources(edge, 2048, 1024, full, &planning);
	Require(sparse == clipped && planning.reason == PackedCropReason::SourcesReady && planning.mergedContexts.count == 2,
		"Diagnostic collection changed sparse admission or lost its contexts");
	Require(!BuildPackedCropSources(crowded, 2048, 1024, full, &planning).count &&
				planning.reason == PackedCropReason::MergedToSingle && planning.inputRegions == crowded.size() && planning.mergedContexts.count == 1,
		"Context merging lost its actual rejection reason");
	Require(!BuildPackedCropSources(std::span(edge).first(1), 2048, 1024, full, &planning).count &&
				planning.reason == PackedCropReason::TooFewRegions,
		"Single-region rejection is ambiguous");
	Require(!BuildPackedCropSources(edge, 2048, 1024, { 0, 0, 1024, 1024 }, &planning).count &&
				planning.reason == PackedCropReason::ContextOutsideAvailable,
		"Available-region rejection is ambiguous");
	StablePackedCropSources retained{ clipped };
	Require(ResolvePackedCropSources(edge, 2048, 1024, full, retained, &planning) == clipped &&
				planning.reason == PackedCropReason::RetainedSources && !planning.candidateExamined && planning.selectedSources == clipped,
		"Retained contexts were falsely reported as a fresh candidate");
	const auto planningJson = PackedCropPlanningJson(planning);
	Require(planningJson["contextPixels"] == 16 && planningJson["requiredContextPixels"] == 8 && planningJson["samplingGuardPixels"] == 2 && planningJson["alignmentPixels"] == 8,
		"Tight crop diagnostics do not distinguish context, sampling support and alignment");
	Require(planningJson["mergedRegionCount"].is_null() && planningJson["selectedRegionCount"] == 2 && planningJson["tileSourceFrame"].is_null(),
		"Unexamined source diagnostics invented a count or tile frame");
	PackedCropAdmissionDiagnostics admission;
	Require(!BuildPackedCropLayout(dense, { 2048, 1024 }, { 2048, 1024 }, full, &admission).count &&
				admission.reason == PackedCropReason::InsufficientAreaSaving && admission.candidateAreaAvailable &&
				admission.candidateModelPixels == full.Area() && admission.layoutBaselineModelPixels == full.Area(),
		"Rejected dense atlas discarded its smallest candidate area");
	admission = {};
	Require(BuildPackedCropLayout(clipped, { 2048, 1024 }, { 2048, 1024 }, full, &admission).count == 2 &&
				admission.reason == PackedCropReason::Admitted && admission.candidateAreaAvailable,
		"Admitted sparse atlas lost its decision");
	admission = {};
	Require(!BuildPackedCropLayout(malformed, { 2048, 1024 }, { 2048, 1024 }, full, &admission).count &&
				admission.reason == PackedCropReason::OverlappingSources && !admission.candidateAreaAvailable,
		"Invalid source ownership was confused with an area rejection");
	admission.frame = 11;
	admission.sourceFrame = 10;
	const auto admissionJson = PackedCropAdmissionJson(admission, 2);
	Require(admissionJson["frame"] == 11 && admissionJson["sourceWorldFrame"] == 10 && admissionJson["featureSlot"] == 2 &&
				admissionJson["candidateModelPixels"].is_null() && admissionJson["effectiveBaselineModelPixels"].is_null(),
		"Admission JSON lost frame identity or replaced unmeasured areas with zero");
#endif
}
