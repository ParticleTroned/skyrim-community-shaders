#include "Features/Upscaling/VRRenderScaleModePolicy.h"

namespace
{
	constexpr bool CheckAllPolicyInputs()
	{
		for (unsigned int inputs = 0; inputs < 16; ++inputs) {
			const bool methodEligible = (inputs & 1u) != 0;
			const bool qualityEligible = (inputs & 2u) != 0;
			const bool preference = (inputs & 4u) != 0;
			const bool linked = (inputs & 8u) != 0;
			const auto resolved = VRRenderScaleModePolicy::Resolve(
				methodEligible, qualityEligible, preference);
			if (resolved.preference != (methodEligible && preference) ||
				resolved.enabled != (methodEligible && qualityEligible && preference)) {
				return false;
			}
			const bool selected = VRRenderScaleModePolicy::ResolveSelectionPreference(
				methodEligible, linked, preference);
			if (selected != (methodEligible && (linked || preference)))
				return false;
		}
		return true;
	}

	static_assert(CheckAllPolicyInputs());
}

int main()
{
	using VRRenderScaleModePolicy::Resolve;
	using VRRenderScaleModePolicy::ResolveSelectionPreference;

	constexpr auto scaled = Resolve(true, true, true);
	static_assert(scaled.preference);
	static_assert(scaled.enabled);

	// DLSS -> DLAA suspends physical scaling without erasing user intent.
	constexpr auto native = Resolve(true, false, scaled.preference);
	static_assert(native.preference);
	static_assert(!native.enabled);

	// DLAA -> DLSS restores the prior physical mode automatically.
	constexpr auto restored = Resolve(true, true, native.preference);
	static_assert(restored.preference);
	static_assert(restored.enabled);

	constexpr auto userDisabled = Resolve(true, false, false);
	static_assert(!userDisabled.preference);
	static_assert(!userDisabled.enabled);
	constexpr auto stillDisabled = Resolve(true, true, userDisabled.preference);
	static_assert(!stillDisabled.preference);
	static_assert(!stillDisabled.enabled);

	constexpr auto ineligibleMethod = Resolve(false, true, true);
	static_assert(!ineligibleMethod.preference);
	static_assert(!ineligibleMethod.enabled);

	// Selecting a scaled DLSS or FSR mode while linked enables physical
	// scaling even if the previous manual preference was disabled.
	constexpr auto linkedSelection = ResolveSelectionPreference(true, true, false);
	static_assert(linkedSelection);
	constexpr auto linkedScaled = Resolve(true, true, linkedSelection);
	static_assert(linkedScaled.preference && linkedScaled.enabled);

	// DLAA and FSR Native AA share the native-quality gate. Linking must not
	// force reduced-resolution rendering, but the return to scaling works.
	constexpr auto linkedNative = Resolve(
		true, false, ResolveSelectionPreference(true, true, linkedScaled.preference));
	static_assert(linkedNative.preference && !linkedNative.enabled);
	constexpr auto linkedRestored = Resolve(
		true, true, ResolveSelectionPreference(true, true, linkedNative.preference));
	static_assert(linkedRestored.preference && linkedRestored.enabled);

	// Disabling linking does not erase a prior manual enabled preference.
	constexpr auto unlinkedNative = Resolve(
		true, false, ResolveSelectionPreference(true, false, scaled.preference));
	static_assert(unlinkedNative.preference && !unlinkedNative.enabled);
	constexpr auto unlinkedRestored = Resolve(
		true, true, ResolveSelectionPreference(true, false, unlinkedNative.preference));
	static_assert(unlinkedRestored.preference && unlinkedRestored.enabled);

	// Manual Render Scale off also disables linking at the UI boundary.
	// Subsequent native/scaled selections must then preserve the off choice.
	constexpr auto manuallyDisabledNative = Resolve(
		true, false, ResolveSelectionPreference(true, false, false));
	static_assert(!manuallyDisabledNative.preference && !manuallyDisabledNative.enabled);
	constexpr auto manuallyDisabledRestored = Resolve(
		true, true, ResolveSelectionPreference(true, false, manuallyDisabledNative.preference));
	static_assert(!manuallyDisabledRestored.preference && !manuallyDisabledRestored.enabled);

	// None/TAA remain ineligible regardless of the link. Selecting an eligible
	// DLSS/FSR backend again can enable scaling through the still-enabled link.
	constexpr auto linkedNonePreference = ResolveSelectionPreference(false, true, true);
	static_assert(!linkedNonePreference);
	constexpr auto linkedNone = Resolve(false, true, linkedNonePreference);
	static_assert(!linkedNone.preference && !linkedNone.enabled);
	constexpr auto linkedFsr = Resolve(
		true, true, ResolveSelectionPreference(true, true, linkedNone.preference));
	static_assert(linkedFsr.preference && linkedFsr.enabled);
	static_assert(!ResolveSelectionPreference(false, false, true));
	static_assert(!ResolveSelectionPreference(false, true, false));

	// The link is deliberately absent from explicit full-profile resolution:
	// API/profiler/DevBench false requests remain false in scaled and native AA.
	constexpr auto explicitScaledOff = Resolve(true, true, false);
	constexpr auto explicitNativeOff = Resolve(true, false, false);
	static_assert(!explicitScaledOff.preference && !explicitScaledOff.enabled);
	static_assert(!explicitNativeOff.preference && !explicitNativeOff.enabled);

	return 0;
}
