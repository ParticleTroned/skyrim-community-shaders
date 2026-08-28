#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace VRRenderScaleMemoryTracePolicy
{
	enum PeakMask : std::uint32_t
	{
		kPeakNone = 0,
		kPeakDXGIUsage = 1u << 0,
		kPeakSystemCommit = 1u << 1,
		kPeakProcessPrivate = 1u << 2,
		kPeakEstimatedAdditional = 1u << 3,
		kPeakProjectedAdditional = 1u << 4,
		kPeakProjectedSystemCommitAdditional = 1u << 5,
	};

	struct ScalarPeak
	{
		bool valid = false;
		std::uint64_t value = 0;
		std::uint64_t tick = 0;
		std::uint32_t frame = 0;
		std::uint64_t transitionEpoch = 0;
		std::uint32_t contractGeneration = 0;
	};

	struct Sample
	{
		std::uint64_t tick = 0;
		std::uint32_t frame = 0;
		std::uint64_t transitionEpoch = 0;
		std::uint32_t contractGeneration = 0;
		bool dxgiValid = false;
		std::uint64_t dxgiUsageBytes = 0;
		bool systemCommitValid = false;
		std::uint64_t systemCommitBytes = 0;
		bool processPrivateValid = false;
		std::uint64_t processPrivateBytes = 0;
		std::uint64_t estimatedAdditionalBytes = 0;
		std::uint64_t projectedAdditionalBytes = 0;
		std::uint64_t projectedSystemCommitAdditionalBytes = 0;
		std::uint64_t residencyAdmissionLimitBytes = 0;
		std::uint64_t systemCommitAdmissionLimitBytes = 0;
		bool planValid = false;
		bool admissionDeferred = false;
		bool physicalMutationActive = false;
		bool previousPresentationRetained = false;
	};

	constexpr std::uint64_t SaturatingDelta(
		std::uint64_t a_value,
		std::uint64_t a_baseline) noexcept
	{
		return a_value >= a_baseline ? a_value - a_baseline : 0;
	}

	constexpr double SafeRatio(
		std::uint64_t a_numerator,
		std::uint64_t a_denominator) noexcept
	{
		return a_denominator == 0 ? 0.0 :
			static_cast<double>(a_numerator) /
			static_cast<double>(a_denominator);
	}

	constexpr bool UpdatePeak(
		ScalarPeak& a_peak,
		bool a_valid,
		std::uint64_t a_value,
		const Sample& a_sample) noexcept
	{
		if (!a_valid || (a_peak.valid && a_value <= a_peak.value))
			return false;
		a_peak = {
			.valid = true,
			.value = a_value,
			.tick = a_sample.tick,
			.frame = a_sample.frame,
			.transitionEpoch = a_sample.transitionEpoch,
			.contractGeneration = a_sample.contractGeneration,
		};
		return true;
	}

	struct Summary
	{
		bool baselineCaptured = false;
		Sample baseline{};
		std::uint64_t samplesObserved = 0;
		std::uint64_t invalidDXGISamples = 0;
		std::uint64_t invalidSystemCommitSamples = 0;
		std::uint64_t invalidProcessPrivateSamples = 0;
		ScalarPeak peakDXGIUsage{};
		ScalarPeak peakSystemCommit{};
		ScalarPeak peakProcessPrivate{};
		ScalarPeak peakEstimatedAdditional{};
		ScalarPeak peakProjectedAdditional{};
		ScalarPeak peakProjectedSystemCommitAdditional{};
		double maximumDeferredResidencyAdmissionRatio = 0.0;
		double maximumDeferredSystemCommitAdmissionRatio = 0.0;
		bool deferredResidencyAdmissionRatioObserved = false;
		bool deferredSystemCommitAdmissionRatioObserved = false;
		bool admissionDeferredObserved = false;
		bool preMutationAdmissionDeferredObserved = false;
		bool presentationRetainedWhileDeferredObserved = false;
		bool physicalMutationObserved = false;

		constexpr std::uint32_t Observe(const Sample& a_sample) noexcept
		{
			if (!baselineCaptured) {
				baselineCaptured = true;
				baseline = a_sample;
			}
			if (samplesObserved != std::numeric_limits<std::uint64_t>::max())
				++samplesObserved;
			if (!a_sample.dxgiValid &&
				invalidDXGISamples != std::numeric_limits<std::uint64_t>::max()) {
				++invalidDXGISamples;
			}
			if (!a_sample.systemCommitValid &&
				invalidSystemCommitSamples != std::numeric_limits<std::uint64_t>::max()) {
				++invalidSystemCommitSamples;
			}
			if (!a_sample.processPrivateValid &&
				invalidProcessPrivateSamples != std::numeric_limits<std::uint64_t>::max()) {
				++invalidProcessPrivateSamples;
			}

			std::uint32_t peaks = kPeakNone;
			if (UpdatePeak(
					peakDXGIUsage,
					a_sample.dxgiValid,
					a_sample.dxgiUsageBytes,
					a_sample)) {
				peaks |= kPeakDXGIUsage;
			}
			if (UpdatePeak(
					peakSystemCommit,
					a_sample.systemCommitValid,
					a_sample.systemCommitBytes,
					a_sample)) {
				peaks |= kPeakSystemCommit;
			}
			if (UpdatePeak(
					peakProcessPrivate,
					a_sample.processPrivateValid,
					a_sample.processPrivateBytes,
					a_sample)) {
				peaks |= kPeakProcessPrivate;
			}
			if (UpdatePeak(
					peakEstimatedAdditional,
					a_sample.planValid &&
						a_sample.estimatedAdditionalBytes != 0,
					a_sample.estimatedAdditionalBytes,
					a_sample)) {
				peaks |= kPeakEstimatedAdditional;
			}
			if (UpdatePeak(
					peakProjectedAdditional,
					a_sample.planValid &&
						a_sample.projectedAdditionalBytes != 0,
					a_sample.projectedAdditionalBytes,
					a_sample)) {
				peaks |= kPeakProjectedAdditional;
			}
			if (UpdatePeak(
					peakProjectedSystemCommitAdditional,
					a_sample.planValid &&
						a_sample.projectedSystemCommitAdditionalBytes != 0,
					a_sample.projectedSystemCommitAdditionalBytes,
					a_sample)) {
				peaks |= kPeakProjectedSystemCommitAdditional;
			}

			physicalMutationObserved =
				physicalMutationObserved || a_sample.physicalMutationActive;
			if (a_sample.planValid && a_sample.admissionDeferred) {
				admissionDeferredObserved = true;
				preMutationAdmissionDeferredObserved =
					preMutationAdmissionDeferredObserved ||
					!a_sample.physicalMutationActive;
				presentationRetainedWhileDeferredObserved =
					presentationRetainedWhileDeferredObserved ||
					a_sample.previousPresentationRetained;
				if (a_sample.planValid && a_sample.dxgiValid &&
					a_sample.residencyAdmissionLimitBytes != 0) {
					deferredResidencyAdmissionRatioObserved = true;
					maximumDeferredResidencyAdmissionRatio = std::max(
						maximumDeferredResidencyAdmissionRatio,
						SafeRatio(
							a_sample.dxgiUsageBytes,
							a_sample.residencyAdmissionLimitBytes));
				}
				if (a_sample.planValid && a_sample.systemCommitValid &&
					a_sample.systemCommitAdmissionLimitBytes != 0) {
					deferredSystemCommitAdmissionRatioObserved = true;
					maximumDeferredSystemCommitAdmissionRatio = std::max(
						maximumDeferredSystemCommitAdmissionRatio,
						SafeRatio(
							a_sample.systemCommitBytes,
							a_sample.systemCommitAdmissionLimitBytes));
				}
			}
			return peaks;
		}
	};
}
