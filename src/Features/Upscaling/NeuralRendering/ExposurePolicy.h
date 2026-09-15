#pragma once
#include "ColorPolicy.h"
#include <limits>

namespace NeuralRendering::Color
{
	inline constexpr std::uint32_t kExposureTexelCount = 4;
	inline constexpr std::uint32_t kExposureSnapshotPixels = 1 + kExposureTexelCount;

	/** Only a single visible mip of a bounded adaptation view can prove a scalar. */
	[[nodiscard]] constexpr bool SupportedExposureView(std::uint32_t width,
		std::uint32_t height, std::uint32_t visibleMips) noexcept
	{
		return visibleMips == 1 && ((width == 1 && height == 1) || (width == 2 && height == 2));
	}
	/** Exact engine-to-replacement selection; unrelated draws cannot inherit it. */
	struct ExposureShaderSelection
	{
		std::uintptr_t context = 0, producer = 0, engineShader = 0, selectedShader = 0;
		std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
		std::uint64_t epoch = 0;

		[[nodiscard]] constexpr std::uintptr_t Match(std::uintptr_t a_context,
			std::uintptr_t a_producer, std::uintptr_t a_engineShader,
			std::uint32_t a_frame, std::uint64_t a_epoch) const noexcept
		{
			return context && producer && engineShader && selectedShader && epoch &&
			               frame != std::numeric_limits<std::uint32_t>::max() &&
			               context == a_context && producer == a_producer && engineShader == a_engineShader &&
			               frame == a_frame && epoch == a_epoch ?
			           selectedShader :
			           0;
		}
	};

	/// Require the live draw to use the selected engine/replacement shader and AvgTex.
	[[nodiscard]] constexpr const char* ExposureDrawRejection(std::uintptr_t expectedShader,
		std::uintptr_t liveShader, std::uintptr_t averageView) noexcept
	{
		if (!liveShader)
			return "HDR draw has no live pixel shader";
		if (!expectedShader || liveShader != expectedShader)
			return "HDR draw pixel shader does not match the selected engine technique";
		if (!averageView)
			return "HDR draw has no live AvgTex t2";
		return nullptr;
	}

	// Float codes are also written by ColorExposureCS. UnitFallback reproduces
	// ISHDR's zero-component branch; it is not a measured unit exposure.
	enum class ExposureValidity : std::uint32_t
	{
		Invalid,
		Ratio,
		UnitFallback,
		NonUniform
	};
	struct ExposureValue
	{
		float average = 0, target = 0, ratio = 1;
		ExposureValidity validity = ExposureValidity::Invalid;
	};
	[[nodiscard]] inline ExposureValue EvaluateHDRExposure(float average, float target) noexcept
	{
		ExposureValue value{ average, target };
		if (!Finite(average) || !Finite(target) || average < 0 || target < 0)
			return value;
		if (average == 0 || target == 0) {
			value.validity = ExposureValidity::UnitFallback;
			return value;
		}
		value.ratio = target / average;
		if (Finite(value.ratio) && value.ratio >= 1.0f / 256.0f && value.ratio <= 256.0f)
			value.validity = ExposureValidity::Ratio;
		return value;
	}
	[[nodiscard]] inline bool ResolveExposureProfile(const Profile& requested, const ExposureValue& captured, Profile& effective) noexcept
	{
		if (!Valid(requested))
			return false;
		effective = requested;
		if (requested.exposureSource == ExposureSource::Manual)
			return true;
		// Reject a zero/uninitialized adaptation texture rather than describing
		// its unit fallback as measured exposure. The shader makes the same test.
		if (captured.validity != ExposureValidity::Ratio || !Finite(captured.average) || !Finite(captured.target) ||
			!Finite(captured.ratio) || captured.average <= 0 || captured.target <= 0 || captured.ratio <= 0)
			return false;
		effective.exposureSource = ExposureSource::Manual;
		effective.exposureMultiplier *= captured.ratio;
		return Valid(effective);
	}
	struct ExposureStamp
	{
		std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
		std::uint64_t epoch = 0, sequence = 0;
		bool ambiguous = false;
	};
	[[nodiscard]] constexpr bool MatchesExposure(const ExposureStamp& stamp, std::uint32_t sourceWorldFrame, std::uint64_t epoch) noexcept
	{
		return epoch != 0 && stamp.sequence != 0 && !stamp.ambiguous && stamp.epoch == epoch &&
		       stamp.frame == sourceWorldFrame && sourceWorldFrame != std::numeric_limits<std::uint32_t>::max();
	}

	struct ExposureTransaction
	{
		std::uint32_t frame = 0, sourceWorldFrame = 0, insertion = 0, route = 0;
		std::uint64_t generation = 0;
		bool operator==(const ExposureTransaction&) const = default;
	};

	enum class ExposureLatchDecision : std::uint32_t
	{
		NewTransaction,
		Reuse,
		Reject
	};

	// Shared by production Bind and the portable lifecycle tests. Capture epoch
	// is intentionally NOT part of this key: a UI toggle or a late HDR callback
	// cannot change an already-latched pair's availability or exposure. Resource
	// reset/retirement clears this policy, while context/device changes fail closed.
	struct ExposureLatchPolicy
	{
		ExposureTransaction key{};
		std::uintptr_t contextIdentity = 0, deviceIdentity = 0;
		bool occupied = false;

		[[nodiscard]] ExposureLatchDecision Begin(const ExposureTransaction& next,
			std::uintptr_t context, std::uintptr_t device) noexcept
		{
			if (!context || !device || next.route >= 2 || next.insertion >= 2 ||
				next.sourceWorldFrame == std::numeric_limits<std::uint32_t>::max())
				return ExposureLatchDecision::Reject;
			// Do not allow any copy through resources belonging to another context
			// generation, even when presentation/source frame numbers are identical.
			if (occupied && (contextIdentity != context || deviceIdentity != device))
				return ExposureLatchDecision::Reject;
			if (occupied && key == next)
				return ExposureLatchDecision::Reuse;
			key = next;
			contextIdentity = context;
			deviceIdentity = device;
			occupied = true;
			return ExposureLatchDecision::NewTransaction;
		}
		void Clear() noexcept { *this = {}; }
	};
}
