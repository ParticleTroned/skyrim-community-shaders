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
	bool g_missingTasksWarningLogged = false;
	RE::PlayerCamera* g_suppressedCamera = nullptr;
	const SKSE::TaskInterface* g_taskInterface = nullptr;
	bool g_refreshPending = false;
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
		std::lock_guard lock(g_suppressionMutex);
		g_refreshPending = false;
		if (g_vanityCameraSuppressionCount == 0) {
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
			auto* setting = FindVanityCameraDelaySetting();
			if (!setting)
				return;
			g_vanityCameraDelaySetting = setting;
			g_savedVanityCameraDelay = setting->GetFloat();
			setting->data.f = kSuppressedVanityCameraDelaySeconds;
			logger::info("Automatic vanity camera suppressed (saved delay: {})", g_savedVanityCameraDelay);
		}

		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!camera)
			return;
		g_suppressedCamera = camera;
		// The camera can already have a nearly expired countdown when the menu opens.
		camera->GetRuntimeData2().idleTimer = kSuppressedVanityCameraDelaySeconds;
	}

	void QueueIdleCameraRefresh()
	{
		// One pending task observes the latest ownership, including close-before-dispatch.
		if (!g_refreshPending) {
			g_refreshPending = true;
			g_taskInterface->AddTask(RefreshIdleCameraOnGameThread);
		}
	}
}

namespace Util
{
	VanityCameraSuppressionLease::~VanityCameraSuppressionLease()
	{
		Release();
	}

	bool VanityCameraSuppressionLease::Acquire()
	{
		std::lock_guard lock(g_suppressionMutex);
		if (active) {
			QueueIdleCameraRefresh();
			return true;
		}

		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			if (!g_missingTasksWarningLogged) {
				logger::warn("Unable to suppress the automatic vanity camera: SKSE task interface was not found");
				g_missingTasksWarningLogged = true;
			}
			return false;
		}

		if (g_vanityCameraSuppressionCount == 0 && !FindVanityCameraDelaySetting())
			return false;

		g_taskInterface = tasks;
		++g_vanityCameraSuppressionCount;
		active = true;
		QueueIdleCameraRefresh();
		return true;
	}

	void VanityCameraSuppressionLease::Release()
	{
		std::lock_guard lock(g_suppressionMutex);
		if (!active)
			return;

		active = false;
		if (g_vanityCameraSuppressionCount == 0) {
			logger::error("Automatic vanity camera suppression lease count was already zero");
			return;
		}

		--g_vanityCameraSuppressionCount;
		if (g_vanityCameraSuppressionCount == 0)
			QueueIdleCameraRefresh();
	}
}
