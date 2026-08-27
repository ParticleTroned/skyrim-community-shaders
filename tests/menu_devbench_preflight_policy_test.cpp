#include "MenuDevBenchPreflightPolicy.h"

using namespace MenuDevBenchPreflightPolicy;

namespace
{
	constexpr State ReadyState()
	{
		return {
			.vr = true,
			.inGame = true,
			.stabilizerActiveForSession = true,
			.developerMode = true,
			.foveatedVendorDispatch = true,
			.foveatedCenterArea = kFoveatedCenterArea,
			.peripheryTAAEnabled = true,
			.peripheryTAACenterArea = kPeripheryTAACenterArea,
			.peripheryTAAOuterScale = kPeripheryTAAOuterScale,
		};
	}

	constexpr bool CoversPreflightPolicy()
	{
		const auto ready = ReadyState();
		if (!CanApplyRuntimeSettings(ready) || !HasRequiredFoveation(ready) || !IsReady(ready))
			return false;

		auto missingStabilizer = ready;
		missingStabilizer.stabilizerActiveForSession = false;
		if (CanApplyRuntimeSettings(missingStabilizer) || IsReady(missingStabilizer))
			return false;

		auto notInGame = ready;
		notInGame.inGame = false;
		if (CanApplyRuntimeSettings(notInGame) || IsReady(notInGame))
			return false;

		auto notVR = ready;
		notVR.vr = false;
		if (CanApplyRuntimeSettings(notVR) || IsReady(notVR))
			return false;

		auto developerModeOff = ready;
		developerModeOff.developerMode = false;
		if (!CanApplyRuntimeSettings(developerModeOff) || IsReady(developerModeOff))
			return false;

		auto foveationOff = ready;
		foveationOff.foveatedVendorDispatch = false;
		if (HasRequiredFoveation(foveationOff) || IsReady(foveationOff))
			return false;

		auto wrongOuterScale = ready;
		wrongOuterScale.peripheryTAAOuterScale = 0.71;
		return !HasRequiredFoveation(wrongOuterScale) && !IsReady(wrongOuterScale);
	}

	static_assert(CoversPreflightPolicy());
}

int main()
{
	return CoversPreflightPolicy() ? 0 : 1;
}
