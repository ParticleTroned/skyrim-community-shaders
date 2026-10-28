#include "VanityCamera.h"

#include <cmath>
#include <cstddef>
#include <mutex>

namespace
{
	constexpr auto kVanityCameraDelaySetting = "fAutoVanityModeDelay:Camera";
	constexpr float kSuppressedVanityCameraDelaySeconds = 10000.0f;

	RE::Setting* g_vanityCameraDelaySetting = nullptr;
	float g_savedVanityCameraDelay = 0.0f;
	std::size_t g_vanityCameraSuppressionCount = 0;
	bool g_missingSettingWarningLogged = false;
	RE::PlayerCamera* g_suppressedCamera = nullptr;
	std::atomic<bool> g_settingAvailable = false;
	std::mutex g_suppressionMutex;

	RE::Setting* FindVanityCameraDelaySetting()
	{
		auto* setting = RE::GetINISetting(kVanityCameraDelaySetting);
		if (!setting && !g_missingSettingWarningLogged) {
			logger::warn("Unable to suppress the automatic vanity camera: {} was not found", kVanityCameraDelaySetting);
			g_missingSettingWarningLogged = true;
		}
		return setting;
	}

	void RefreshIdleCameraOnGameThread()
	{
		auto* availableSetting = FindVanityCameraDelaySetting();
		g_settingAvailable.store(availableSetting != nullptr, std::memory_order_release);
		bool suppressionRequested;
		{
			std::lock_guard lock(g_suppressionMutex);
			suppressionRequested = g_vanityCameraSuppressionCount != 0;
		}
		if (!suppressionRequested) {
			if (!g_vanityCameraDelaySetting)
				return;
			// Preserve a newer setting written by another owner while the menu was open.
			if (g_vanityCameraDelaySetting->GetFloat() == kSuppressedVanityCameraDelaySeconds)
				g_vanityCameraDelaySetting->data.f = g_savedVanityCameraDelay;
			auto* camera = RE::PlayerCamera::GetSingleton();
			if (camera && camera == g_suppressedCamera) {
				const float delay = g_vanityCameraDelaySetting->GetFloat();
				if (std::isfinite(delay) && delay >= 0.0f)
					camera->GetRuntimeData2().idleTimer = delay;
			}
			logger::info("Automatic vanity camera restored (delay: {})", g_vanityCameraDelaySetting->GetFloat());
			g_vanityCameraDelaySetting = nullptr;
			g_suppressedCamera = nullptr;
			return;
		}

		if (!g_vanityCameraDelaySetting) {
			if (!availableSetting)
				return;
			g_vanityCameraDelaySetting = availableSetting;
			g_savedVanityCameraDelay = availableSetting->GetFloat();
			availableSetting->data.f = kSuppressedVanityCameraDelaySeconds;
			logger::info("Automatic vanity camera suppressed (saved delay: {})", g_savedVanityCameraDelay);
		}

		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!camera)
			return;
		g_suppressedCamera = camera;
		// The camera can already have a nearly expired countdown when the menu opens.
		camera->GetRuntimeData2().idleTimer = kSuppressedVanityCameraDelaySeconds;
	}
}

namespace Util
{
	void UpdateVanityCameraSuppression()
	{
		RefreshIdleCameraOnGameThread();
	}

	VanityCameraSuppressionLease::~VanityCameraSuppressionLease()
	{
		Release();
	}

	bool VanityCameraSuppressionLease::Acquire()
	{
		std::lock_guard lock(g_suppressionMutex);
		if (active)
			return true;

		if (!g_settingAvailable.load(std::memory_order_acquire))
			return false;

		++g_vanityCameraSuppressionCount;
		active = true;
		return true;
	}

	void VanityCameraSuppressionLease::Release()
	{
		{
			std::lock_guard lock(g_suppressionMutex);
			if (!active)
				return;

			active = false;
			if (g_vanityCameraSuppressionCount != 0) {
				--g_vanityCameraSuppressionCount;
				return;
			}
		}
		logger::error("Automatic vanity camera suppression lease count was already zero");
	}
}
