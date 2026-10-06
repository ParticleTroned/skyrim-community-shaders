#pragma once

#include "ComputeSubrect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace NeuralRendering
{
	/** Stereo calibration covers the complete pair; a changed eye invalidates both. */
	[[nodiscard]] inline std::uint64_t CharacterCropPairKey(std::span<const std::uint64_t> keys) noexcept
	{
		if (keys.empty() || keys.size() > 2 || !keys[0] || (keys.size() == 2 && !keys[1]))
			return 0;
		return keys.size() == 1 ? keys[0] : (keys[0] ^ (keys[1] + 0x9e3779b97f4a7c15ull + (keys[0] << 6) + (keys[0] >> 2)));
	}
	/** The output selection mask is identical in every inference-domain mode. */
	enum class CharacterCropMode : std::uint32_t
	{
		Automatic,
		Cropped,
		Uncropped,
		Count
	};

	/** Matched whole-frame, CPU and GPU windows; durations must not be added together. */
	struct CharacterCropMeasurement
	{
		double frameMs = 0, cpuMs = 0, gpuMs = 0;
		double noiseMs = 0;
		std::uint32_t frames = 0;
	};

	struct CharacterCropCalibration
	{
		std::array<CharacterCropMeasurement, 3> cropped{}, uncropped{};
		double switchCostMs = 0;
		bool qualityQualified = false;
	};

	/** Require a win in every repeat, accounting for uncertainty and transition cost. */
	[[nodiscard]] inline bool IsWinningCharacterCropCalibration(const CharacterCropCalibration& calibration) noexcept
	{
		if (!calibration.qualityQualified || !std::isfinite(calibration.switchCostMs) ||
			calibration.switchCostMs < 0 || calibration.switchCostMs > 10000)
			return false;
		const auto valid = [](const CharacterCropMeasurement& sample) {
			return sample.frames >= 120 && sample.frames <= 100000 &&
			       std::isfinite(sample.frameMs) && sample.frameMs > 0 && sample.frameMs <= 10000 &&
			       std::isfinite(sample.cpuMs) && sample.cpuMs >= 0 && sample.cpuMs <= 10000 &&
			       std::isfinite(sample.gpuMs) && sample.gpuMs > 0 && sample.gpuMs <= 10000 &&
			       std::isfinite(sample.noiseMs) && sample.noiseMs >= 0 && sample.noiseMs <= 10000;
		};
		for (std::size_t i = 0; i < calibration.cropped.size(); ++i) {
			const auto& crop = calibration.cropped[i];
			const auto& full = calibration.uncropped[i];
			if (!valid(crop) || !valid(full))
				return false;
			const auto noise = 3 * (crop.noiseMs + full.noiseMs);
			const auto threshold = std::max({ 0.1, full.frameMs * 0.02, noise }) + calibration.switchCostMs / 120;
			if (full.frameMs - crop.frameMs <= threshold ||
				crop.cpuMs > full.cpuMs + noise || crop.gpuMs > full.gpuMs + noise)
				return false;
		}
		return true;
	}

	/** Bounded session admission; keys identify exact measured contexts, never area interpolation. */
	class CharacterCropAdmission
	{
	public:
		static constexpr std::uint32_t kResidenceFrames = 120;
		static constexpr std::uint32_t kLifetimeFrames = 36000;

		bool Qualify(std::uint64_t key, std::uint32_t frame, const CharacterCropCalibration& calibration) noexcept
		{
			if (!key || !IsWinningCharacterCropCalibration(calibration))
				return false;
			for (auto& entry : entries_) {
				if (entry.key == key) {
					entry.frame = frame;
					return true;
				}
			}
			entries_[next_] = { key, frame };
			next_ = (next_ + 1) % entries_.size();
			return true;
		}

		[[nodiscard]] bool Qualified(std::uint64_t key, std::uint32_t frame) const noexcept
		{
			for (const auto& entry : entries_)
				if (entry.key && entry.key == key && frame - entry.frame <= kLifetimeFrames)
					return true;
			return false;
		}

	private:
		struct Entry
		{
			std::uint64_t key = 0;
			std::uint32_t frame = 0;
		};
		std::array<Entry, 16> entries_{};
		std::size_t next_ = 0;
	};

	struct CharacterCropResidence
	{
		std::uint64_t key = 0;
		std::uint32_t since = 0;
	};

	/** Missing qualification immediately expands; contraction needs sustained matching evidence. */
	[[nodiscard]] inline bool AdmitCharacterCrop(std::uint64_t key, std::uint32_t frame,
		bool qualified, CharacterCropResidence& residence) noexcept
	{
		if (!qualified) {
			residence = {};
			return false;
		}
		if (residence.key != key)
			residence = { key, frame };
		return frame - residence.since >= CharacterCropAdmission::kResidenceFrames;
	}
}
