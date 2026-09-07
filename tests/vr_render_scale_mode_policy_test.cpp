#include "Features/Upscaling/VRRenderScaleModePolicy.h"

int main()
{
	using VRRenderScaleModePolicy::Resolve;

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

	return 0;
}
