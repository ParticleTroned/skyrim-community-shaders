#pragma once

#include "../DLSSViewportCrop.h"
#include "CharacterComputeSubrect.h"
#include "RegionCapacity.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace NeuralRendering
{
	inline constexpr std::size_t kMaximumPackedCrops = 4;
	inline constexpr std::uint32_t kPackedCropContextPixels = 16;
	inline constexpr std::uint32_t kPackedCropRequiredContextPixels = 8;
	inline constexpr std::uint32_t kPackedCropAlignment = kCharacterComputeRoiAlignment;
	inline constexpr std::uint32_t kPackedCropSupportPixels = kPackedCropRequiredContextPixels + kCharacterComputeRoiGuardPixels;

	/** Source rectangles retain their original single-viewport coordinates. */
	struct PackedCropSources
	{
		std::array<ComputeSubrect, kMaximumPackedCrops> regions{};
		std::uint32_t count = 0;
		bool operator==(const PackedCropSources&) const = default;
	};

	struct StablePackedCropSources
	{
		PackedCropSources sources{};
		std::uint32_t frames = 0;
		std::uint32_t singleRegionFrames = 0;
	};

	/** Fixed mappings are temporal identity, independently of reusable texture sizes. */
	struct PackedCropLayout
	{
		struct Tile
		{
			ComputeSubrect source{}, model{}, packed{};
			bool operator==(const Tile&) const = default;
		};
		std::array<Tile, kMaximumPackedCrops> tiles{};
		UpscalingDLSS::Extent extent{}, unpackedExtent{};
		std::uint32_t count = 0;
		bool operator==(const PackedCropLayout&) const = default;
	};

	/** Evaluated canvas capacity reserves empty space within the single-region backing. */
	struct PackedCropLayoutRequest
	{
		UpscalingDLSS::Extent capacity{}, minimumCapacity{};
		std::uint64_t baselinePixels = std::numeric_limits<std::uint64_t>::max();
		bool reserveCapacity = false;
		const PackedCropLayout* previous = nullptr;
	};
	inline constexpr std::uint32_t kPackedCropCapacityQuantum = 64;
	inline constexpr std::uint32_t kPackedCropAdmissionFrames = 8;
	inline constexpr std::uint32_t kPackedCropRecoveryFrames = 16;

	/** CPU-only admission history; retaining this state retains no GPU allocations. */
	struct PackedCropCapacityState
	{
		UpscalingDLSS::Extent capacity{};
		std::uint32_t stableFrames = 0, requiredFrames = kPackedCropAdmissionFrames;
		std::uint32_t lastFrame = 0;
		bool observed = false, active = false;
		PackedCropLayout layout{};
	};

	/** One source-matched layout is reused by projection, accounting and preparation. */
	struct PackedCropPlan
	{
		PackedCropSources sources{};
		UpscalingDLSS::Extent source{}, model{};
		ComputeSubrect available{};
		std::uint64_t baselinePixels = 0;
		PackedCropLayout layout{};
		bool Matches(const PackedCropSources& crops, UpscalingDLSS::Extent sourceSize, UpscalingDLSS::Extent modelSize,
			const ComputeSubrect& region, std::uint64_t baseline) const noexcept
		{
			return sources == crops && source == sourceSize && model == modelSize && available == region && baselinePixels == baseline;
		}
	};

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** CPU decisions are captured at admission, independently of GPU coverage. */
	enum class PackedCropReason
	{
		NotRequested,
		CropDisabled,
		FullOutputMask,
		EmptySelection,
		UncertainProjection,
		FullViewportFallback,
		NotExamined,
		TooFewRegions,
		InvalidSourceBounds,
		ContextOutsideAvailable,
		MergedToSingle,
		SourcesReady,
		RetainedSources,
		InvalidModelGeometry,
		UnsupportedRoute,
		InvalidCropCount,
		OverlappingSources,
		InvalidModelMapping,
		NoBoundedLayout,
		InsufficientAreaSaving,
		ProviderTypeChanged,
		InvalidProjection,
		CapacitySettling,
		ShadersUnavailable,
		Admitted
	};
	enum class PackedCropStage
	{
		SourcePlanning,
		ModelProjection,
		AtlasLayout,
		AreaAdmission,
		ProviderCompatibility,
		Admitted
	};

	struct PackedCropPlanningDiagnostics
	{
		PackedCropReason reason = PackedCropReason::NotRequested;
		PackedCropReason candidateReason = PackedCropReason::NotExamined;
		std::uint32_t inputRegions = 0;
		std::uint32_t tileSourceFrame = std::numeric_limits<std::uint32_t>::max();
		bool candidateExamined = false;
		PackedCropSources mergedContexts{}, selectedSources{};
		ComputeSubrect available{};
	};

	struct PackedCropCapacityDiagnostics
	{
		PackedCropReason reason = PackedCropReason::NotExamined;
		UpscalingDLSS::Extent capacity{};
		std::uint32_t stableFrames = 0, requiredFrames = 0;
	};

	struct PackedCropAdmissionDiagnostics
	{
		PackedCropPlanningDiagnostics planning{};
		PackedCropCapacityDiagnostics capacity{};
		PackedCropStage stage = PackedCropStage::SourcePlanning;
		PackedCropReason reason = PackedCropReason::NotExamined;
		std::uint32_t frame = std::numeric_limits<std::uint32_t>::max(), sourceFrame = std::numeric_limits<std::uint32_t>::max();
		std::uint64_t generation = 0;
		std::uint32_t modelResolutionPercent = 100;
		std::uint64_t baselineSourcePixels = 0, layoutBaselineModelPixels = 0, effectiveBaselineModelPixels = 0, candidateModelPixels = 0;
		UpscalingDLSS::Extent candidateExtent{};
		bool modelAreasAvailable = false, candidateAreaAvailable = false;
	};
#endif

	/** Context containment owns atlas reuse as well as initial construction. */
	[[nodiscard]] inline ComputeSubrect BuildPackedCropContext(CharacterRect region,
		std::uint32_t width, std::uint32_t height, ComputeSubrect available,
		std::uint32_t contextPixels = kPackedCropContextPixels) noexcept
	{
		if (width > kMaximumNeuralImageDimension || height > kMaximumNeuralImageDimension ||
			contextPixels > kPackedCropContextPixels || !available.Fits(width, height))
			return {};
		const auto support = BuildCharacterComputeSubrect(std::span(&region, 1), width, height);
		if (!ContainsComputeSubrect(available, support))
			return {};
		const auto lower = [contextPixels](std::uint32_t base) {
			return (base > contextPixels ? base - contextPixels : 0u) / kPackedCropAlignment * kPackedCropAlignment;
		};
		const auto upper = [contextPixels](std::uint32_t end, std::uint32_t limit) {
			return std::min(limit, (end + contextPixels + kPackedCropAlignment - 1u) / kPackedCropAlignment * kPackedCropAlignment);
		};
		const auto x = std::max(available.baseX, lower(support.baseX)), y = std::max(available.baseY, lower(support.baseY));
		return { x, y, std::min(available.baseX + available.width, upper(support.baseX + support.width, width)) - x,
			std::min(available.baseY + available.height, upper(support.baseY + support.height, height)) - y };
	}

	/** Merge all selected coverage; capacity limits never discard an actor. */
	[[nodiscard]] inline PackedCropSources BuildPackedCropSources(std::span<const CharacterRect> regions,
		std::uint32_t width, std::uint32_t height, ComputeSubrect available
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		PackedCropPlanningDiagnostics* diagnostics = nullptr
#endif
	)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics) {
			*diagnostics = {};
			diagnostics->candidateExamined = true;
			diagnostics->inputRegions = static_cast<std::uint32_t>(regions.size());
			diagnostics->available = available;
		}
		const auto finish = [&](PackedCropSources sources, PackedCropReason reason) {
			if (diagnostics) {
				diagnostics->reason = diagnostics->candidateReason = reason;
				diagnostics->selectedSources = sources;
			}
			return sources;
		};
#endif
		PackedCropSources result{};
		if (regions.size() < 2 || width > kMaximumNeuralImageDimension || height > kMaximumNeuralImageDimension || !available.Fits(width, height)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			return finish({}, regions.size() < 2 ? PackedCropReason::TooFewRegions : PackedCropReason::InvalidSourceBounds);
#else
			return result;
#endif
		}
		std::vector<CharacterRect> contexts;
		contexts.reserve(regions.size());
		for (const auto& region : regions) {
			const auto context = BuildPackedCropContext(region, width, height, available);
			if (!context.IsValid()) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				return finish({}, PackedCropReason::ContextOutsideAvailable);
#else
				return {};
#endif
			}
			contexts.push_back({ context.baseX, context.baseY, context.baseX + context.width, context.baseY + context.height });
		}
		const auto spatialOrder = [](const auto& a, const auto& b) {
			return std::tie(a.minY, a.minX, a.maxY, a.maxX) < std::tie(b.minY, b.minX, b.maxY, b.maxX);
		};
		std::ranges::sort(contexts, spatialOrder);
		CharacterRegionPolicy::CompactToCapacity(contexts, kMaximumPackedCrops);
		// Overlapping contexts share one tile, so reconstruction has one unambiguous owner.
		for (std::size_t i = 0; i < contexts.size();) {
			bool merged = false;
			for (std::size_t j = i + 1; j < contexts.size(); ++j) {
				const auto& a = contexts[i];
				const auto& b = contexts[j];
				if (a.minX < b.maxX && b.minX < a.maxX && a.minY < b.maxY && b.minY < a.maxY) {
					contexts[i] = CharacterRegionPolicy::Union(a, b);
					contexts.erase(contexts.begin() + j);
					merged = true;
					break;
				}
			}
			i = merged ? 0 : i + 1;
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics) {
			for (const auto& context : contexts)
				diagnostics->mergedContexts.regions[diagnostics->mergedContexts.count++] = { context.minX, context.minY, context.maxX - context.minX, context.maxY - context.minY };
		}
#endif
		if (contexts.size() < 2) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			return finish({}, PackedCropReason::MergedToSingle);
#else
			return {};
#endif
		}
		std::ranges::sort(contexts, spatialOrder);
		for (const auto& rect : contexts)
			result.regions[result.count++] = { rect.minX, rect.minY, rect.maxX - rect.minX, rect.maxY - rect.minY };
#ifdef DEVBENCH_BRIDGE_ENABLED
		return finish(result, PackedCropReason::SourcesReady);
#else
		return result;
#endif
	}

	/** Keep a contained layout stable; growth is immediate, contraction requires residence. */
	[[nodiscard]] inline PackedCropSources ResolvePackedCropSources(std::span<const CharacterRect> regions,
		std::uint32_t width, std::uint32_t height, ComputeSubrect available, StablePackedCropSources& state
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		PackedCropPlanningDiagnostics* diagnostics = nullptr
#endif
	)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics) {
			*diagnostics = {};
			diagnostics->inputRegions = static_cast<std::uint32_t>(regions.size());
			diagnostics->available = available;
		}
#endif
		if (regions.empty() || width > kMaximumNeuralImageDimension || height > kMaximumNeuralImageDimension || !available.Fits(width, height)) {
			state = {};
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (diagnostics)
				diagnostics->reason = regions.empty() ? PackedCropReason::EmptySelection : PackedCropReason::InvalidSourceBounds;
#endif
			return {};
		}
		// A transient selection loss must not retire and rebuild the atlas on adjacent frames.
		state.singleRegionFrames = regions.size() == 1 ? std::min(state.singleRegionFrames + 1u, kCharacterProviderRoiHistoryFrames) : 0u;
		if (state.singleRegionFrames == kCharacterProviderRoiHistoryFrames) {
			state = {};
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (diagnostics)
				diagnostics->reason = PackedCropReason::TooFewRegions;
#endif
			return {};
		}
		bool covered = state.sources.count >= 2 && state.sources.count <= kMaximumPackedCrops;
		for (std::uint32_t i = 0; covered && i < state.sources.count; ++i)
			covered = ContainsComputeSubrect(available, state.sources.regions[i]);
		for (const auto& region : regions) {
			// Keep motion slack outside the minimum context required by the GPU coverage guard.
			const auto support = BuildPackedCropContext(region, width, height, available, kPackedCropRequiredContextPixels);
			bool found = false;
			for (std::uint32_t i = 0; covered && i < state.sources.count; ++i)
				found = found || ContainsComputeSubrect(state.sources.regions[i], support);
			covered = covered && found;
		}
		if (covered && ++state.frames < kCharacterProviderRoiHistoryFrames) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (diagnostics) {
				diagnostics->reason = PackedCropReason::RetainedSources;
				diagnostics->selectedSources = state.sources;
			}
#endif
			return state.sources;
		}
		auto candidate = BuildPackedCropSources(regions, width, height, available
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			diagnostics
#endif
		);
		std::uint64_t previousArea = 0, candidateArea = 0;
		for (std::uint32_t i = 0; i < state.sources.count && i < kMaximumPackedCrops; ++i)
			previousArea += state.sources.regions[i].Area();
		for (std::uint32_t i = 0; i < candidate.count; ++i)
			candidateArea += candidate.regions[i].Area();
		if (!covered || (candidate.count >= 2 && candidateArea * 4u <= previousArea * 3u))
			state.sources = candidate;
		state.frames = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics) {
			diagnostics->selectedSources = state.sources;
			if (state.sources != candidate)
				diagnostics->reason = PackedCropReason::RetainedSources;
		}
#endif
		return state.sources;
	}

	/** Preserve spatially matched atlas positions when every updated tile still fits. */
	[[nodiscard]] inline PackedCropLayout RetainPackedCropPlacement(
		const std::array<PackedCropLayout::Tile, kMaximumPackedCrops>& tiles, std::uint32_t count,
		UpscalingDLSS::Extent modelSize, UpscalingDLSS::Extent capacity, const PackedCropLayout& previous) noexcept
	{
		if (count < 2 || count > kMaximumPackedCrops || previous.count != count || previous.extent != capacity ||
			previous.unpackedExtent != modelSize || !capacity.IsValid())
			return {};
		bool unchanged = true;
		for (std::uint32_t i = 0; i < count; ++i) {
			const auto& old = previous.tiles[i];
			unchanged = unchanged && old.source == tiles[i].source && old.model == tiles[i].model &&
			            old.packed.width == tiles[i].model.width && old.packed.height == tiles[i].model.height &&
			            old.packed.Fits(capacity.width, capacity.height);
			for (std::uint32_t j = 0; unchanged && j < i; ++j)
				unchanged = !IntersectComputeSubrect(old.packed, previous.tiles[j].packed).IsValid();
		}
		if (unchanged)
			return previous;
		std::array<std::uint32_t, kMaximumPackedCrops> order{ 0, 1, 2, 3 };
		PackedCropLayout best{};
		std::uint64_t bestOverlap = 0;
		do {
			PackedCropLayout candidate{ tiles, capacity, modelSize, count };
			std::uint64_t overlap = 0;
			bool valid = true;
			for (std::uint32_t i = 0; valid && i < count; ++i) {
				const auto& old = previous.tiles[order[i]];
				const auto shared = IntersectComputeSubrect(tiles[i].source, old.source).Area();
				auto& packed = candidate.tiles[i].packed;
				packed = { old.packed.baseX, old.packed.baseY, tiles[i].model.width, tiles[i].model.height };
				valid = shared != 0 && packed.Fits(capacity.width, capacity.height);
				for (std::uint32_t j = 0; valid && j < i; ++j)
					valid = !IntersectComputeSubrect(packed, candidate.tiles[j].packed).IsValid();
				overlap += shared;
			}
			if (valid && overlap > bestOverlap) {
				best = candidate;
				bestOverlap = overlap;
			}
		} while (std::next_permutation(order.begin(), order.begin() + count));
		return best;
	}

	/** Search bounded shelf layouts, admitting only at least 25% less evaluated area. */
	[[nodiscard]] inline PackedCropLayout BuildPackedCropLayout(const PackedCropSources& sources,
		UpscalingDLSS::Extent sourceSize, UpscalingDLSS::Extent modelSize, ComputeSubrect available
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		PackedCropAdmissionDiagnostics* diagnostics = nullptr
#endif
		,
		PackedCropLayoutRequest request = {}) noexcept
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics) {
			diagnostics->stage = PackedCropStage::AtlasLayout;
			diagnostics->reason = PackedCropReason::NoBoundedLayout;
			diagnostics->candidateAreaAvailable = false;
			diagnostics->candidateExtent = {};
			diagnostics->candidateModelPixels = 0;
		}
		const auto reject = [&](PackedCropReason reason) {
			if (diagnostics)
				diagnostics->reason = reason;
			return PackedCropLayout{};
		};
#endif
		PackedCropLayout best{};
		if (sources.count < 2 || sources.count > kMaximumPackedCrops ||
			!available.Fits(sourceSize.width, sourceSize.height) || sourceSize.width > kMaximumNeuralImageDimension ||
			sourceSize.height > kMaximumNeuralImageDimension || !modelSize.IsValid() ||
			modelSize.width > kMaximumNeuralImageDimension || modelSize.height > kMaximumNeuralImageDimension ||
			request.capacity.width > kMaximumNeuralImageDimension || request.capacity.height > kMaximumNeuralImageDimension ||
			request.minimumCapacity.width > kMaximumNeuralImageDimension || request.minimumCapacity.height > kMaximumNeuralImageDimension ||
			((request.capacity.width == 0) != (request.capacity.height == 0))) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			return reject(sources.count < 2 || sources.count > kMaximumPackedCrops ? PackedCropReason::InvalidCropCount : PackedCropReason::InvalidSourceBounds);
#else
			return best;
#endif
		}
		std::array<PackedCropLayout::Tile, kMaximumPackedCrops> tiles{};
		for (std::uint32_t i = 0; i < sources.count; ++i) {
			if (!ContainsComputeSubrect(available, sources.regions[i])) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				return reject(PackedCropReason::ContextOutsideAvailable);
#else
				return {};
#endif
			}
			for (std::uint32_t j = 0; j < i; ++j) {
				if (IntersectComputeSubrect(sources.regions[i], sources.regions[j]).IsValid()) {
#ifdef DEVBENCH_BRIDGE_ENABLED
					return reject(PackedCropReason::OverlappingSources);
#else
					return {};
#endif
				}
			}
			tiles[i].source = sources.regions[i];
			tiles[i].model = MapComputeSubrect(sources.regions[i], sourceSize.width, sourceSize.height, modelSize.width, modelSize.height);
			if (!tiles[i].model.IsValid()) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				return reject(PackedCropReason::InvalidModelMapping);
#else
				return {};
#endif
			}
		}
		const auto layoutBaseline = MapComputeSubrect(available, sourceSize.width, sourceSize.height, modelSize.width, modelSize.height).Area();
		const auto baseline = std::min(request.baselinePixels, layoutBaseline);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics)
			diagnostics->layoutBaselineModelPixels = layoutBaseline;
#endif
		const auto retainedArea = static_cast<std::uint64_t>(request.capacity.width) * request.capacity.height;
		if (request.previous && request.capacity.IsValid() &&
			request.capacity.width >= kMinimumExperimentalContextExtent && request.capacity.height >= kMinimumExperimentalContextExtent &&
			request.capacity.width <= modelSize.width && request.capacity.height <= modelSize.height && retainedArea * 4u <= baseline * 3u) {
			best = RetainPackedCropPlacement(tiles, sources.count, modelSize, request.capacity, *request.previous);
			if (best.count) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (diagnostics) {
					diagnostics->stage = PackedCropStage::AreaAdmission;
					diagnostics->reason = PackedCropReason::Admitted;
					diagnostics->candidateAreaAvailable = true;
					diagnostics->candidateModelPixels = retainedArea;
					diagnostics->candidateExtent = best.extent;
				}
#endif
				return best;
			}
		}
		const auto align = [](std::uint32_t value) { return (value + kPackedCropAlignment - 1u) / kPackedCropAlignment * kPackedCropAlignment; };
		for (bool vertical : { false, true }) {
			for (std::uint32_t breaks = 0; breaks < (1u << (sources.count - 1)); ++breaks) {
				PackedCropLayout layout{ tiles, {}, modelSize, sources.count };
				std::uint32_t x = 0, y = 0, row = 0, right = 0;
				for (std::uint32_t i = 0; i < sources.count; ++i) {
					const auto w = vertical ? tiles[i].model.height : tiles[i].model.width;
					const auto h = vertical ? tiles[i].model.width : tiles[i].model.height;
					layout.tiles[i].packed = { vertical ? y : x, vertical ? x : y, tiles[i].model.width, tiles[i].model.height };
					x += align(w);
					row = std::max(row, align(h));
					right = std::max(right, x);
					if (i + 1 == sources.count || (breaks & (1u << i))) {
						y += row;
						x = row = 0;
					}
				}
				layout.extent = vertical ? UpscalingDLSS::Extent{ y, right } : UpscalingDLSS::Extent{ right, y };
				// Native minimum dimensions do not require a halo around every source crop.
				layout.extent.width = std::max(layout.extent.width, kMinimumExperimentalContextExtent);
				layout.extent.height = std::max(layout.extent.height, kMinimumExperimentalContextExtent);
				if (request.capacity.IsValid()) {
					if (layout.extent.width > request.capacity.width || layout.extent.height > request.capacity.height)
						continue;
					layout.extent = request.capacity;
				} else if (request.reserveCapacity) {
					const auto reserve = [](std::uint32_t size, std::uint32_t minimum) {
						return (std::max(size, minimum) + kPackedCropCapacityQuantum - 1u) / kPackedCropCapacityQuantum * kPackedCropCapacityQuantum;
					};
					layout.extent = { reserve(layout.extent.width, request.minimumCapacity.width), reserve(layout.extent.height, request.minimumCapacity.height) };
				}
				const auto area = static_cast<std::uint64_t>(layout.extent.width) * layout.extent.height;
				const auto bestArea = static_cast<std::uint64_t>(best.extent.width) * best.extent.height;
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (diagnostics && layout.extent.width <= modelSize.width && layout.extent.height <= modelSize.height &&
					(!diagnostics->candidateAreaAvailable || area < diagnostics->candidateModelPixels)) {
					diagnostics->candidateAreaAvailable = true;
					diagnostics->candidateModelPixels = area;
					diagnostics->candidateExtent = layout.extent;
				}
#endif
				if (layout.extent.width <= modelSize.width && layout.extent.height <= modelSize.height &&
					area * 4u <= baseline * 3u && (!best.count || area < bestArea))
					best = layout;
			}
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics) {
			diagnostics->stage = best.count || diagnostics->candidateAreaAvailable ? PackedCropStage::AreaAdmission : PackedCropStage::AtlasLayout;
			diagnostics->reason = best.count ? PackedCropReason::Admitted : diagnostics->candidateAreaAvailable ? PackedCropReason::InsufficientAreaSaving :
			                                                                                                      PackedCropReason::NoBoundedLayout;
		}
#endif
		return best;
	}

	/** Repack within retained capacity; unstable layouts use the ordinary single-region path. */
	inline PackedCropLayout ResolvePackedCropLayout(const PackedCropSources& sources, UpscalingDLSS::Extent sourceSize,
		UpscalingDLSS::Extent modelSize, const ComputeSubrect& available, std::uint64_t baselinePixels,
		std::uint32_t frame, PackedCropCapacityState& state
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		PackedCropAdmissionDiagnostics* diagnostics = nullptr,
		std::optional<PackedCropAdmissionDiagnostics>* retainedRejection = nullptr
#endif
		) noexcept
	{
		const bool newFrame = !state.observed || frame != state.lastFrame;
		if (state.observed && newFrame && frame - state.lastFrame != 1u)
			state.stableFrames = 0;
		state.lastFrame = frame;
		state.observed = true;
		const auto build = [&](PackedCropLayoutRequest request) {
			request.baselinePixels = baselinePixels;
			request.previous = &state.layout;
			return BuildPackedCropLayout(sources, sourceSize, modelSize, available
#ifdef DEVBENCH_BRIDGE_ENABLED
				,
				diagnostics
#endif
				,
				request);
		};
		auto layout = state.capacity.IsValid() ? build({ .capacity = state.capacity }) : PackedCropLayout{};
		if (state.active) {
			if (layout.count) {
				state.layout = layout;
				return layout;
			}
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (retainedRejection && diagnostics) {
				*retainedRejection = *diagnostics;
				(*retainedRejection)->capacity = { diagnostics->reason, state.capacity, state.stableFrames, state.requiredFrames };
				(*retainedRejection)->modelAreasAvailable = true;
				(*retainedRejection)->effectiveBaselineModelPixels = diagnostics->layoutBaselineModelPixels ?
				                                                         std::min(baselinePixels, diagnostics->layoutBaselineModelPixels) :
				                                                         baselinePixels;
			}
#endif
			state.active = false;
			state.stableFrames = 0;
			state.requiredFrames = kPackedCropRecoveryFrames;
		}
		if (!layout.count) {
			layout = build({ .minimumCapacity = state.capacity, .reserveCapacity = true });
			// A smaller viable envelope can be considered only through the full settling window.
			if (!layout.count && state.capacity.IsValid())
				layout = build({ .reserveCapacity = true });
			state.capacity = layout.extent;
			state.stableFrames = 0;
		}
		if (!layout.count)
			return {};
		if (newFrame && state.stableFrames < state.requiredFrames)
			++state.stableFrames;
		if (state.stableFrames >= state.requiredFrames) {
			state.active = true;
			state.layout = layout;
			return layout;
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics) {
			diagnostics->stage = PackedCropStage::AreaAdmission;
			diagnostics->reason = PackedCropReason::CapacitySettling;
		}
#endif
		return {};
	}
}
