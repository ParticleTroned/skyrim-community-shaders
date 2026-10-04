#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "CharacterMultiRoi.h"
#	include "PipelinePolicy.h"

#	include <algorithm>
#	include <cstdint>
#	include <optional>
#	include <span>

namespace NeuralRendering::SharedContext
{
	enum class Mode : std::uint32_t
	{
		Off,
		Enclosing,
		FullEye,
		Count,
	};

	/** Original-coordinate context experiments retain independent output ownership. */
	struct Settings
	{
		Mode mode = Mode::Off;
		std::uint32_t halo = 256;
		bool operator==(const Settings&) const = default;
	};

	[[nodiscard]] inline constexpr bool Valid(const Settings& a_settings) noexcept
	{
		return a_settings.mode < Mode::Count &&
		       (a_settings.halo == 0 || a_settings.halo == 64 ||
				   a_settings.halo == 128 || a_settings.halo == 256);
	}

	struct Plan
	{
		ComputeSubrect inferenceContext{};
		CharacterOutputPlan output{};
		/** Union of the original proven sampling supports, never inferred from padding. */
		ComputeSubrect samplingSupport{};
	};

	struct CopyRegion
	{
		/** Original resource coordinates for the destination and full-coordinate source. */
		ComputeSubrect output{};
		/** Source coordinates when processing produced a context-sized intermediate. */
		ComputeSubrect contextLocal{};
	};

	[[nodiscard]] inline constexpr std::uint32_t OutputCount(const Plan& a_plan) noexcept
	{
		return a_plan.inferenceContext.IsValid() && a_plan.output.regions.count <= kEnabledRegionsPerEye ?
		           std::max(1u, a_plan.output.regions.count) :
		           0u;
	}

	/** Keeps raw and context-local scatter copies on the same exclusive output domains. */
	[[nodiscard]] inline constexpr std::optional<CopyRegion> CopyDomain(
		const Plan& a_plan, std::uint32_t a_index) noexcept
	{
		if (a_index >= OutputCount(a_plan))
			return std::nullopt;
		const auto& owned = a_plan.output.regions.count ?
		                        a_plan.output.regions.regions[a_index] :
		                        a_plan.output.enclosure;
		if (!ContainsComputeSubrect(a_plan.output.enclosure, owned) ||
			!ContainsComputeSubrect(a_plan.inferenceContext, owned))
			return std::nullopt;
		return CopyRegion{ owned,
			{ owned.baseX - a_plan.inferenceContext.baseX,
				owned.baseY - a_plan.inferenceContext.baseY, owned.width, owned.height } };
	}

	/** Forms one stateless C evaluation while preserving every original output rectangle. */
	[[nodiscard]] inline constexpr std::optional<Plan> Build(
		const CharacterOutputPlan& a_output, std::span<const RoiDescriptor> a_descriptors,
		UpscalingDLSS::Extent a_capacity, const Settings& a_settings,
		RenderingMode a_renderingMode, bool a_featureUpscaling, bool a_reset) noexcept
	{
		// D3D11/D3D12 texture bounds keep arithmetic and graphics admission congruent.
		constexpr std::uint32_t maximumTextureExtent = 16384;
		if (!Valid(a_settings) || a_settings.mode == Mode::Off || !a_reset ||
			a_renderingMode != RenderingMode::ReducedResolution || a_featureUpscaling ||
			!a_capacity.width || !a_capacity.height ||
			a_capacity.width > maximumTextureExtent || a_capacity.height > maximumTextureExtent ||
			a_descriptors.size() != std::max(1u, a_output.regions.count) ||
			!CharacterRegionOutputPixels(a_output.regions, a_output.enclosure,
				a_capacity.width, a_capacity.height))
			return std::nullopt;

		ComputeSubrect context{}, samplingSupport{};
		for (std::size_t index = 0; index < a_descriptors.size(); ++index) {
			const auto& descriptor = a_descriptors[index];
			const auto& owned = a_output.regions.count ? a_output.regions.regions[index] : a_output.enclosure;
			if (!descriptor.samplingSupport || descriptor.compactSource ||
				!GetRoiDescriptorViolation(descriptor, owned, a_capacity).empty())
				return std::nullopt;
			context = UnionCharacterComputeSubrect(context, descriptor.inferenceContext);
			samplingSupport = UnionCharacterComputeSubrect(samplingSupport, *descriptor.samplingSupport);
		}

		if (a_settings.mode == Mode::FullEye) {
			context = { 0, 0, a_capacity.width, a_capacity.height };
		} else if (a_descriptors.size() > 1) {
			context = ExpandCharacterWorkRect(context, a_capacity.width, a_capacity.height, a_settings.halo);
			constexpr auto alignment = kCharacterProviderRoiAlignment;
			const auto alignedEnd = [](std::uint32_t base, std::uint32_t extent, std::uint32_t limit) {
				const auto end = static_cast<std::uint64_t>(base) + extent;
				return static_cast<std::uint32_t>(std::min<std::uint64_t>(limit,
					(end + alignment - 1u) / alignment * alignment));
			};
			const auto left = context.baseX / alignment * alignment;
			const auto top = context.baseY / alignment * alignment;
			context = { left, top,
				alignedEnd(context.baseX, context.width, a_capacity.width) - left,
				alignedEnd(context.baseY, context.height, a_capacity.height) - top };
		}
		if (!context.Fits(a_capacity.width, a_capacity.height) || !QualifiedExperimentalContextGeometry(context))
			return std::nullopt;
		return Plan{ context, a_output, samplingSupport };
	}
}

#endif
