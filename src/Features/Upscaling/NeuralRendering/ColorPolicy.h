#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace NeuralRendering::Color
{
	// CSX processing choices, not NVIDIA NGX/Streamline parameters.
	enum class Mode : std::uint32_t { LegacyRaw, Managed, PreserveSource, Count };
	enum class Domain : std::uint32_t { Unknown, Linear, SRGB, Count };
	enum class Transform : std::uint32_t { Identity, LinearToSRGB, ReversibleProxy, Count };
	enum class ExposureSource : std::uint32_t { Manual, CapturedHDR, Count };

	[[nodiscard]] inline bool Finite(float value) noexcept
	{
		// Also valid with the plugin's /fp:fast compilation options.
		return (std::bit_cast<std::uint32_t>(value) & 0x7f800000u) != 0x7f800000u;
	}

	struct Settings
	{
		Mode mode = Mode::LegacyRaw;
		float detailStrength = 1.0f;
		float appearanceMix = 0.0f;
		float maximumDetailStops = 1.0f;
		// Disable colour processing without forgetting the selected mode/sliders.
		bool enabled = true;
		bool operator==(const Settings&) const = default;
	};

	struct Profile
	{
		Domain domain = Domain::Unknown;
		Transform transform = Transform::Identity;
		// A calibration factor, not a measurement. CapturedHDR multiplies it by
		// the frame-matched GPU snapshot of ISHDR's AvgTex.y / AvgTex.x.
		float exposureMultiplier = 1.0f;
		ExposureSource exposureSource = ExposureSource::Manual;
		bool operator==(const Profile&) const = default;
	};

	struct Experiments
	{
		// Index 0: Upscaled Centre. Index 1: Final LDR pre-UI.
		std::array<Profile, 2> profiles{};
		bool transportBypass = false;
		bool diagnostics = false;
		bool captureEngineExposure = false;
		// Unlike transportBypass, this keeps real inference running. It is a
		// display comparison only and must not change the input-history epoch.
		bool applyModelEdit = true;
		bool operator==(const Experiments&) const = default;
	};

	struct Configuration
	{
		Settings settings{};
		Experiments experiments{};
		std::uint64_t revision = 1;
		std::array<std::uint64_t, 2> inputEpoch{ 1, 1 };
		[[nodiscard]] Mode EffectiveMode() const noexcept
		{
			return settings.enabled ? settings.mode : Mode::LegacyRaw;
		}
		[[nodiscard]] bool Enabled() const noexcept
		{
			return EffectiveMode() != Mode::LegacyRaw || experiments.transportBypass ||
			       experiments.diagnostics || experiments.captureEngineExposure || !experiments.applyModelEdit;
		}
	};

	[[nodiscard]] inline bool Valid(const Settings& value) noexcept
	{
		return value.mode < Mode::Count &&
		       Finite(value.detailStrength) && value.detailStrength >= 0.0f && value.detailStrength <= 2.0f &&
		       Finite(value.appearanceMix) && value.appearanceMix >= 0.0f && value.appearanceMix <= 1.0f &&
		       Finite(value.maximumDetailStops) && value.maximumDetailStops >= 0.0f && value.maximumDetailStops <= 2.0f;
	}

	[[nodiscard]] inline bool Valid(const Profile& value) noexcept
	{
		if (value.domain >= Domain::Count || value.transform >= Transform::Count || value.exposureSource >= ExposureSource::Count ||
			!Finite(value.exposureMultiplier) || value.exposureMultiplier < 1.0f / 256.0f || value.exposureMultiplier > 256.0f)
			return false;
		// Capturing an engine exposure does NOT prove a linear source domain.
		return value.transform == Transform::Identity ?
		           value.exposureMultiplier == 1.0f && value.exposureSource == ExposureSource::Manual :
		           value.domain == Domain::Linear;
	}

	[[nodiscard]] inline bool Valid(const Experiments& value) noexcept
	{
		return Valid(value.profiles[0]) && Valid(value.profiles[1]);
	}

	[[nodiscard]] inline Profile EffectiveProfile(const Configuration& value, std::uint32_t insertion) noexcept
	{
		if (value.EffectiveMode() == Mode::LegacyRaw || insertion >= 2)
			return {};
		return value.experiments.profiles[insertion];
	}

	[[nodiscard]] inline bool NeedsExposureCapture(const Configuration& value) noexcept
	{
		return value.experiments.captureEngineExposure ||
		       EffectiveProfile(value, 0).exposureSource == ExposureSource::CapturedHDR ||
		       EffectiveProfile(value, 1).exposureSource == ExposureSource::CapturedHDR;
	}

	[[nodiscard]] inline bool ChangesInput(const Configuration& oldValue, const Configuration& newValue, std::uint32_t insertion) noexcept
	{
		return EffectiveProfile(oldValue, insertion) != EffectiveProfile(newValue, insertion) ||
		       oldValue.experiments.transportBypass != newValue.experiments.transportBypass;
	}

	// Storage flags occupy previously unused ControlFlags bits; the CB stays 48 bytes.
	// Keep values aligned with ColorCommon.hlsli (covered by contract tests).
	enum class Storage : std::uint32_t { Float32 = 0, R11G11B10 = 0x100, Float16 = 0x200, UNorm = 0x300 };
	using RGB = std::array<float, 3>;
	[[nodiscard]] inline bool Finite(const RGB& value) noexcept
	{
		return Finite(value[0]) && Finite(value[1]) && Finite(value[2]);
	}
	[[nodiscard]] inline bool Representable(const RGB& value, Storage storage) noexcept
	{
		if (!Finite(value)) return false;
		for (std::size_t i = 0; i < 3; ++i) {
			if (storage == Storage::R11G11B10 && (value[i] < 0 || value[i] > (i == 2 ? 64512.0f : 65024.0f))) return false;
			if (storage == Storage::Float16 && std::abs(value[i]) > 65504.0f) return false;
			if (storage == Storage::UNorm && (value[i] < 0 || value[i] > 1)) return false;
		}
		return storage == Storage::Float32 || storage == Storage::R11G11B10 || storage == Storage::Float16 || storage == Storage::UNorm;
	}
	[[nodiscard]] inline float Decode(float x) noexcept
	{
		return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
	}
	[[nodiscard]] inline float Encode(float x) noexcept
	{
		return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
	}

	// CPU reference. Resolve a captured exposure with ResolveExposureProfile
	// before calling this; an unbound GPU-dependent profile must fail closed.
	[[nodiscard]] inline bool Forward(RGB input, const Profile& profile, RGB& output) noexcept
	{
		if (!Valid(profile) || profile.exposureSource != ExposureSource::Manual || !Finite(input))
			return false;
		output = input;
		if (profile.transform == Transform::Identity)
			return true;
		if (*std::min_element(input.begin(), input.end()) < 0.0f)
			return false;
		for (auto& channel : output)
			channel *= profile.exposureMultiplier;
		if (!Finite(output))
			return false;
		const auto maximum = *std::max_element(output.begin(), output.end());
		if (maximum > 32.0f)
			return false;
		if (profile.transform == Transform::ReversibleProxy) {
			for (auto& channel : output)
				channel /= 1.0f + maximum;
		}
		for (auto& channel : output)
			channel = Encode(channel);
		return Finite(output);
	}

	[[nodiscard]] inline bool Inverse(RGB input, const Profile& profile, RGB& output) noexcept
	{
		if (!Valid(profile) || profile.exposureSource != ExposureSource::Manual || !Finite(input))
			return false;
		output = input;
		if (profile.transform == Transform::Identity)
			return true;
		if (*std::min_element(input.begin(), input.end()) < 0.0f)
			return false;
		for (auto& channel : output)
			channel = Decode(channel);
		if (!Finite(output))
			return false;
		if (profile.transform == Transform::ReversibleProxy) {
			const auto denominator = 1.0f - *std::max_element(output.begin(), output.end());
			if (denominator < 1.0f / 64.0f)
				return false;
			for (auto& channel : output)
				channel /= denominator;
		}
		for (auto& channel : output)
			channel /= profile.exposureMultiplier;
		return Finite(output);
	}

	[[nodiscard]] inline RGB Reconstruct(const RGB& baseline, const RGB& prepared, const RGB& neural, const Profile& profile) noexcept
	{
		RGB check{}, inverseInput{}, inverseOutput{}, result{};
		if (!Finite(baseline)) return {};
		if (!Forward(baseline, profile, check) || !Inverse(prepared, profile, inverseInput) || !Inverse(neural, profile, inverseOutput))
			return baseline;
		for (std::size_t i = 0; i < result.size(); ++i)
			result[i] = baseline[i] + (inverseOutput[i] - inverseInput[i]);
		return Finite(result) ? result : baseline;
	}

	[[nodiscard]] inline float DetailGain(float logResidual, float lowFrequencyResidual, float edgeWeight, const Settings& settings) noexcept
	{
		if (!Finite(logResidual) || !Finite(lowFrequencyResidual) || !Finite(edgeWeight) || !Valid(settings))
			return 1.0f;
		const auto stops = std::clamp((logResidual - lowFrequencyResidual) * settings.detailStrength *
			std::clamp(edgeWeight, 0.0f, 1.0f), -settings.maximumDetailStops, settings.maximumDetailStops);
		return std::exp2(stops);
	}
}
