#pragma once

#include <cstdint>

namespace VRSubmitColorContract
{
	enum class SourceColorSpace : std::uint8_t
	{
		Automatic,
		Gamma,
		Linear,
		Unsupported,
	};

	enum class Transfer : std::uint8_t
	{
		Unknown,
		Gamma,
		Linear,
	};

	enum class DynamicRange : std::uint8_t
	{
		LDR,
		HDR,
	};

	/** Describes source pixels independently of their storage and vendor processing mode. */
	struct Contract
	{
		Transfer transfer = Transfer::Unknown;
		DynamicRange range = DynamicRange::LDR;

		bool operator==(const Contract&) const = default;
	};

	/** Resolves the supported RGBA8 presentation boundary; OpenVR Auto means gamma for this format. */
	[[nodiscard]] constexpr Contract Resolve(bool a_rgba8Unorm, SourceColorSpace a_colorSpace) noexcept
	{
		if (!a_rgba8Unorm)
			return {};

		switch (a_colorSpace) {
		case SourceColorSpace::Automatic:
		case SourceColorSpace::Gamma:
			return { Transfer::Gamma, DynamicRange::LDR };
		case SourceColorSpace::Linear:
			return { Transfer::Linear, DynamicRange::LDR };
		default:
			return {};
		}
	}

	/** Spatial presentation preserves either supported transfer without converting pixel values. */
	[[nodiscard]] constexpr bool IsPresentationSupported(const Contract& a_contract) noexcept
	{
		return a_contract.range == DynamicRange::LDR &&
		       (a_contract.transfer == Transfer::Gamma || a_contract.transfer == Transfer::Linear);
	}

	/** The established vendor path consumes gamma LDR; linear input requires a separate integration. */
	[[nodiscard]] constexpr bool IsVendorSupported(const Contract& a_contract) noexcept
	{
		return a_contract.transfer == Transfer::Gamma && a_contract.range == DynamicRange::LDR;
	}

	/** Selects DLSS processing from declared dynamic range, never texture storage or transfer. */
	[[nodiscard]] constexpr bool DLSSUsesHDR(const Contract& a_contract) noexcept
	{
		return a_contract.range == DynamicRange::HDR;
	}

	// FSR's established processing mode is retained independently of source range.
	inline constexpr bool kLegacyFsrHighDynamicRange = true;
}
