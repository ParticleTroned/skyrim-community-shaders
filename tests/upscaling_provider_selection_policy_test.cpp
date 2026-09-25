#include "Features/Upscaling/UpscalingProviderSelectionPolicy.h"

#include <initializer_list>
#include <optional>

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

	bool CoversLazyAdapterSelection()
	{
		for (const bool primary : { false, true }) {
			for (const bool complete : { false, true }) {
				for (const bool available : { false, true }) {
					for (const auto vendor : { std::optional<std::uint32_t>{},
							 std::optional<std::uint32_t>{ 0x1002u },
							 std::optional<std::uint32_t>{ kNvidiaVendorID } }) {
						unsigned queries = 0;
						const auto selected = SelectWithAdapterQuery(
							{ .primaryRequestsDLSS = primary,
								.providerCheckComplete = complete,
								.dlssAvailable = available },
							[&] {
								++queries;
								return vendor;
							});
						const auto expected = Select({
							.primaryRequestsDLSS = primary,
							.adapterKnown = vendor.has_value(),
							.adapterVendorID = vendor.value_or(0),
							.providerCheckComplete = complete,
							.dlssAvailable = available,
						});
						if (selected.route != expected.route ||
							selected.awaitingDLSSCapability != expected.awaitingDLSSCapability ||
							queries != (primary && !complete && !available ? 1u : 0u))
							return false;
					}
				}
			}
		}
		return true;
	}

	bool CoversLateCapabilities()
	{
		unsigned queries = 0;
		std::optional<std::uint32_t> vendor;
		auto query = [&] {
			++queries;
			return vendor;
		};
		const auto unresolved = SelectWithAdapterQuery({ .primaryRequestsDLSS = true }, query);
		vendor = 0x1002u;
		const auto lateAmd = SelectWithAdapterQuery({ .primaryRequestsDLSS = true }, query);
		vendor = kNvidiaVendorID;
		const auto pendingNvidia = SelectWithAdapterQuery({ .primaryRequestsDLSS = true }, query);
		const auto readyNvidia = SelectWithAdapterQuery(
			{ .primaryRequestsDLSS = true, .providerCheckComplete = true, .dlssAvailable = true }, query);
		const auto unavailableNvidia = SelectWithAdapterQuery(
			{ .primaryRequestsDLSS = true, .providerCheckComplete = true }, query);
		const auto knownAmd = SelectWithAdapterQuery(
			{ .primaryRequestsDLSS = true, .adapterKnown = true, .adapterVendorID = 0x1002u }, query);
		return unresolved.awaitingDLSSCapability &&
		       lateAmd.route == Route::Fallback && !lateAmd.awaitingDLSSCapability &&
		       pendingNvidia.awaitingDLSSCapability &&
		       readyNvidia.route == Route::Primary && !readyNvidia.awaitingDLSSCapability &&
		       unavailableNvidia.route == Route::Fallback && !unavailableNvidia.awaitingDLSSCapability &&
		       knownAmd.route == Route::Fallback && queries == 3;
	}
}

int main()
{
	return CoversProviderSelection() && CoversLazyAdapterSelection() && CoversLateCapabilities() ? 0 : 1;
}
