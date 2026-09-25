#pragma once

#include <cstdint>

namespace UpscalingProviderSelectionPolicy
{
	constexpr std::uint32_t kNvidiaVendorID = 0x10DEu;

	enum class Route : std::uint8_t
	{
		Primary,
		Fallback,
	};

	struct Selection
	{
		Route route = Route::Primary;
		bool awaitingDLSSCapability = false;
	};

	struct Inputs
	{
		bool primaryRequestsDLSS = false;
		bool adapterKnown = false;
		std::uint32_t adapterVendorID = 0;
		bool providerCheckComplete = false;
		bool dlssAvailable = false;
	};

	/** Resolves a portable DLSS-preferred setting before runtime publication. */
	constexpr Selection Select(const Inputs& a_inputs) noexcept
	{
		if (!a_inputs.primaryRequestsDLSS || a_inputs.dlssAvailable)
			return {};

		if (a_inputs.adapterKnown &&
			a_inputs.adapterVendorID != kNvidiaVendorID) {
			return { .route = Route::Fallback };
		}

		if (a_inputs.providerCheckComplete)
			return { .route = Route::Fallback };

		return {
			.route = Route::Primary,
			.awaitingDLSSCapability = true,
		};
	}

	/** Queries adapter identity only while the provider capability is unresolved. */
	template <class Query>
	Selection SelectWithAdapterQuery(Inputs a_inputs, Query&& a_query)
	{
		if (a_inputs.primaryRequestsDLSS && !a_inputs.dlssAvailable &&
			!a_inputs.providerCheckComplete && !a_inputs.adapterKnown) {
			const auto vendorID = a_query();
			a_inputs.adapterKnown = vendorID.has_value();
			a_inputs.adapterVendorID = vendorID.value_or(0);
		}
		return Select(a_inputs);
	}

	/** Restricts automatic fallback to the zero-ID profile bound from saved settings. */
	constexpr bool ShouldNormalizePortableBootProfile(
		const Selection& a_selection,
		std::uint64_t a_requestID,
		bool a_profileRequestsDLSS) noexcept
	{
		return a_profileRequestsDLSS &&
		       a_requestID == 0 &&
		       !a_selection.awaitingDLSSCapability &&
		       a_selection.route == Route::Fallback;
	}
}
