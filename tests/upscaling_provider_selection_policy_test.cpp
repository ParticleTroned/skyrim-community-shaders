#include "Features/Upscaling/UpscalingProviderSelectionPolicy.h"

namespace
{
	using namespace UpscalingProviderSelectionPolicy;

	constexpr bool CoversProviderSelection()
	{
		const auto directPrimary = Select({ .primaryRequestsDLSS = false });
		const auto unknownAdapter = Select({ .primaryRequestsDLSS = true });
		const auto amdAdapter = Select({
			.primaryRequestsDLSS = true,
			.adapterKnown = true,
			.adapterVendorID = 0x1002u,
		});
		const auto pendingNvidia = Select({
			.primaryRequestsDLSS = true,
			.adapterKnown = true,
			.adapterVendorID = kNvidiaVendorID,
		});
		const auto availableNvidia = Select({
			.primaryRequestsDLSS = true,
			.adapterKnown = true,
			.adapterVendorID = kNvidiaVendorID,
			.providerCheckComplete = true,
			.dlssAvailable = true,
		});
		const auto unavailableNvidia = Select({
			.primaryRequestsDLSS = true,
			.adapterKnown = true,
			.adapterVendorID = kNvidiaVendorID,
			.providerCheckComplete = true,
		});
		const bool portableBootFallback =
			ShouldNormalizePortableBootProfile(amdAdapter, 0, true);
		const bool explicitDLSSPreserved =
			!ShouldNormalizePortableBootProfile(amdAdapter, 17, true);
		const bool nativeBootPreserved =
			!ShouldNormalizePortableBootProfile(amdAdapter, 0, false);

		return directPrimary.route == Route::Primary &&
		       !directPrimary.awaitingDLSSCapability &&
		       unknownAdapter.route == Route::Primary &&
		       unknownAdapter.awaitingDLSSCapability &&
		       amdAdapter.route == Route::Fallback &&
		       !amdAdapter.awaitingDLSSCapability &&
		       pendingNvidia.route == Route::Primary &&
		       pendingNvidia.awaitingDLSSCapability &&
		       availableNvidia.route == Route::Primary &&
		       !availableNvidia.awaitingDLSSCapability &&
		       unavailableNvidia.route == Route::Fallback &&
		       !unavailableNvidia.awaitingDLSSCapability &&
		       portableBootFallback &&
		       explicitDLSSPreserved &&
		       nativeBootPreserved;
	}

	static_assert(CoversProviderSelection());
}

int main()
{
	return CoversProviderSelection() ? 0 : 1;
}
