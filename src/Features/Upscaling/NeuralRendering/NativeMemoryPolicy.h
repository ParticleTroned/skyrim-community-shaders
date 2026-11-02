#pragma once

#include "MemoryRecoveryPolicy.h"
#include "NativeEvaluationLayout.h"
#include "RegionCapacity.h"
#include "SubmissionFenceSnapshot.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace NeuralRendering
{
	/** Creation capacity, not the actor rectangle evaluated inside that capacity. */
	struct NativeMemoryKey
	{
		std::uint64_t runtimeVersion = 0;
		std::uint32_t network = 0;
		NativeCreationExtents creation{};
		UpscalingDLSS::Extent color{};
		std::uint32_t colorFormat = 0, outputFormat = 0, motionFormat = 0;
		bool upscaling = false, providerBlending = false, controlMask = false;

		bool operator==(const NativeMemoryKey&) const = default;
	};

	/** Bounded session estimates; process-wide DXGI deltas are never exact provider bytes. */
	class NativeMemoryPolicy
	{
	public:
		static constexpr std::uint64_t kMiB = MemoryRecoveryPolicy::kMiB;
		static constexpr std::size_t kCapacity = 64;
		static constexpr std::uint64_t kObservationMargin = 64 * kMiB;
		static constexpr std::uint64_t kMaximumObservationMs = 30000;
		static constexpr std::uint64_t kIdleReservationMs = 1000;
		static constexpr std::uint64_t kColdFixedBytes = 512 * kMiB;
		static constexpr std::uint64_t kColdGuideBytesPerPixel = 128;
		static constexpr std::uint64_t kColdOutputBytesPerPixel = 128, kColdUpscaledOutputBytesPerPixel = 192;
		static constexpr std::uint64_t kColdColorBytesPerPixel = 32, kColdBlendedColorBytesPerPixel = 64;
		static constexpr std::uint64_t kColdMaskBytesPerPixel = 16;

		struct Estimate
		{
			std::uint64_t bytes = 0, observedProcessGrowth = 0;
			bool observed = false;
			std::optional<std::uint64_t> observedProviderGrowth;
			std::optional<std::uint64_t> scratchBytes;
		};

		/** Independent lower bounds overlap; adding provider bytes to process growth double counts. */
		struct Observation
		{
			std::uint64_t processGrowth = 0;
			std::optional<std::uint64_t> providerGrowth;
			std::optional<std::uint64_t> scratchBytes;

			[[nodiscard]] Observation WithCreation(const Observation& creation) const noexcept
			{
				// Missing statistics cannot erase a known lower bound from the other phase.
				return { Add(processGrowth, creation.processGrowth),
					providerGrowth || creation.providerGrowth ? std::optional{ Add(providerGrowth.value_or(0), creation.providerGrowth.value_or(0)) } : std::nullopt,
					std::max(scratchBytes, creation.scratchBytes) };
			}
		};

		/** Uncalibrated cold allowance scales with every creation grid and allocation mode. */
		[[nodiscard]] static std::optional<std::uint64_t> ColdAllowance(const NativeMemoryKey& key) noexcept
		{
			std::uint64_t bytes = kColdFixedBytes;
			const auto append = [&](UpscalingDLSS::Extent extent, std::uint64_t perPixel) {
				if (!extent.width || !extent.height || extent.width > kMaximumNeuralImageDimension || extent.height > kMaximumNeuralImageDimension)
					return false;
				bytes += std::uint64_t(extent.width) * extent.height * perPixel;
				return true;
			};
			// These allowances are deliberately not claims about the provider's tensor layout.
			if (!append(key.creation.input, kColdGuideBytesPerPixel) ||
				!append(key.creation.output, key.upscaling ? kColdUpscaledOutputBytesPerPixel : kColdOutputBytesPerPixel) ||
				!append(key.color, key.providerBlending ? kColdBlendedColorBytesPerPixel : kColdColorBytesPerPixel) ||
				(key.controlMask && !append(key.creation.output, kColdMaskBytesPerPixel)))
				return std::nullopt;
			return bytes;
		}

		[[nodiscard]] std::optional<Estimate> Project(const NativeMemoryKey& key) const noexcept
		{
			const auto cold = ColdAllowance(key);
			if (!cold)
				return std::nullopt;
			for (const auto& entry : entries_)
				if (entry.valid && entry.key == key)
					return Estimate{ std::max(*cold, entry.bytes), entry.growth.processGrowth, true, entry.growth.providerGrowth, entry.growth.scratchBytes };
			return Estimate{ *cold };
		}

		/** Retain only conservative growth, with a margin for concurrent process allocations. */
		void Observe(const NativeMemoryKey& key, std::uint64_t growth) noexcept
		{
			Observe(key, Observation{ growth });
		}

		/** Provider and scratch observations can raise admission demand, never lower its cold floor. */
		void Observe(const NativeMemoryKey& key, const Observation& growth) noexcept
		{
			if (!ColdAllowance(key))
				return;
			auto* selected = &entries_[next_];
			for (auto& entry : entries_)
				if (entry.valid && entry.key == key) {
					selected = &entry;
					break;
				}
			if (!selected->valid || selected->key != key) {
				*selected = { .key = key, .valid = true };
				next_ = (next_ + 1) % entries_.size();
			}
			selected->growth.processGrowth = std::max(selected->growth.processGrowth, growth.processGrowth);
			if (growth.providerGrowth)
				selected->growth.providerGrowth = std::max(selected->growth.providerGrowth.value_or(0), *growth.providerGrowth);
			selected->growth.scratchBytes = std::max(selected->growth.scratchBytes, growth.scratchBytes);
			const auto lowerBound = std::max({ growth.processGrowth, growth.providerGrowth.value_or(0), growth.scratchBytes.value_or(0) });
			const auto margin = std::max(kObservationMargin, lowerBound / 4);
			selected->bytes = std::max(selected->bytes, Add(lowerBound, margin));
		}

	private:
		static constexpr std::uint64_t Add(std::uint64_t left, std::uint64_t right) noexcept
		{
			return right > UINT64_MAX - left ? UINT64_MAX : left + right;
		}
		struct Entry
		{
			NativeMemoryKey key{};
			std::uint64_t bytes = 0;
			Observation growth{};
			bool valid = false;
		};
		std::array<Entry, kCapacity> entries_{};
		std::size_t next_ = 0;
	};

	/** One native startup submission in flight; completion is polled without a CPU wait. */
	struct NativeStartupObservation
	{
		NativeMemoryKey key{};
		std::uint32_t slot = UINT32_MAX;
		std::uint64_t startedMs = 0, baseline = 0, peak = 0;
		std::uint64_t fence = 0, issued = 0;
		std::uint64_t reservedBytes = 0;
		bool active = false, valid = false, evaluated = false;
		std::optional<std::uint64_t> providerBaseline;
		std::uint32_t providerResidentMask = 0;

		void Begin(const NativeMemoryKey& value, std::uint32_t index, MemoryBudgetSample sample,
			std::uint64_t now, bool evaluation) noexcept
		{
			*this = { .key = value, .slot = index, .startedMs = now, .baseline = sample.usageBytes, .peak = sample.usageBytes, .active = true, .valid = sample.IsFresh(now) && !sample.simulated, .evaluated = evaluation };
		}

		void Sample(MemoryBudgetSample sample, std::uint64_t now) noexcept
		{
			if (!active)
				return;
			valid &= sample.IsFresh(now) && !sample.simulated && now >= startedMs &&
			         now - startedMs <= NativeMemoryPolicy::kMaximumObservationMs;
			if (valid)
				peak = std::max(peak, sample.usageBytes);
		}

		/** A different resident set cannot establish an incremental provider cost for this startup. */
		[[nodiscard]] std::optional<NativeMemoryPolicy::Observation> Growth(std::optional<std::uint64_t> provider,
			std::uint32_t residentMask, std::optional<std::uint64_t> scratchBytes) const noexcept
		{
			const auto slotBit = FeatureSlotBit(slot);
			if (!valid || !slotBit)
				return std::nullopt;
			const bool sameOtherFeatures = ((providerResidentMask ^ residentMask) & ~slotBit) == 0;
			const auto providerGrowth = provider && providerBaseline && *provider >= *providerBaseline &&
			                                    sameOtherFeatures && (residentMask & slotBit) ?
			                                std::optional{ *provider - *providerBaseline } :
			                                std::nullopt;
			return NativeMemoryPolicy::Observation{ peak - baseline, providerGrowth, scratchBytes };
		}

		[[nodiscard]] bool Completed(const LifetimeFenceSnapshot& current) const noexcept
		{
			return active && issued && fence == current.fence && current.completedKnown &&
			       !current.recording && !current.deviceRemoved && current.completed >= issued;
		}

		/** Fresh usage replaces only this completed eye's allowance, preserving the rest of the pair. */
		[[nodiscard]] std::uint64_t RemainingReservation(std::uint64_t bytes, const LifetimeFenceSnapshot& current,
			MemoryBudgetSample sample, std::uint64_t now) const noexcept
		{
			return evaluated && Completed(current) && sample.IsFresh(now) && reservedBytes <= bytes ? bytes - reservedBytes : bytes;
		}
	};
}
