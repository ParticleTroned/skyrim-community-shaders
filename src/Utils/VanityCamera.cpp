#include "VanityCamera.h"

#include <cstddef>

namespace
{
	constexpr auto kVanityCameraDelaySetting = "fAutoVanityModeDelay:Camera";
	constexpr float kSuppressedVanityCameraDelaySeconds = 10000.0f;

	RE::Setting* g_vanityCameraDelaySetting = nullptr;
	float g_savedVanityCameraDelay = 0.0f;
	std::size_t g_vanityCameraSuppressionCount = 0;
	bool g_missingSettingWarningLogged = false;
}

namespace Util
{
	VanityCameraSuppressionLease::~VanityCameraSuppressionLease()
	{
		Release();
	}

	bool VanityCameraSuppressionLease::Acquire()
	{
		if (active)
			return true;

		if (g_vanityCameraSuppressionCount == 0) {
			auto* setting = RE::GetINISetting(kVanityCameraDelaySetting);
			if (!setting) {
				if (!g_missingSettingWarningLogged) {
					logger::warn("Unable to suppress the automatic vanity camera: {} was not found", kVanityCameraDelaySetting);
					g_missingSettingWarningLogged = true;
				}
				return false;
			}

			g_vanityCameraDelaySetting = setting;
			g_savedVanityCameraDelay = setting->GetFloat();
			setting->data.f = kSuppressedVanityCameraDelaySeconds;
			logger::info("Automatic vanity camera suppressed (saved delay: {})", g_savedVanityCameraDelay);
		}

		++g_vanityCameraSuppressionCount;
		active = true;
		return true;
	}

	void VanityCameraSuppressionLease::Release()
	{
		if (!active)
			return;

		active = false;
		if (g_vanityCameraSuppressionCount == 0) {
			logger::error("Automatic vanity camera suppression lease count was already zero");
			return;
		}

		--g_vanityCameraSuppressionCount;
		if (g_vanityCameraSuppressionCount != 0)
			return;

		if (g_vanityCameraDelaySetting) {
			g_vanityCameraDelaySetting->data.f = g_savedVanityCameraDelay;
			logger::info("Automatic vanity camera restored (delay: {})", g_savedVanityCameraDelay);
		}
		g_vanityCameraDelaySetting = nullptr;
	}
}
