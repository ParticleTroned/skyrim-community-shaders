// Scene-scoped shader cache clearing: bounded capture, scoped eviction, and
// compilation bookkeeping. Disk mutation remains in ShaderCache.cpp so it can
// share that file's manifest and generation synchronization.

#include "ShaderCache.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <unordered_set>

#include "Deferred.h"
#include "Feature.h"
#include "Globals.h"
#include "State.h"

namespace SIE
{
	bool ShaderCache::IsTrackingActiveShaders() const
	{
		return (globals::state && globals::state->IsDeveloperMode()) ||
		       activeShaderCaptureFramesRemaining.load(std::memory_order_relaxed) > 0;
	}

	void ShaderCache::BeginActiveShaderCapture()
	{
		if (activeShaderCaptureStage != ActiveShaderCaptureStage::Idle)
			return;

		activeShaderCaptureMenuWasVisible = false;
		clearedThisCaptureCycle.clear();
		StartActiveShaderCaptureWindow(ActiveShaderCaptureStage::FirstWindow);
	}

	void ShaderCache::StartActiveShaderCaptureWindow(ActiveShaderCaptureStage a_stage)
	{
		activeShaderCaptureStage = a_stage;
		activeShaderCaptureThread.store(std::this_thread::get_id(), std::memory_order_relaxed);
		activeShaderCaptureDeadline = std::chrono::steady_clock::now() + kActiveShaderCaptureTimeout;
		{
			std::lock_guard lock(activeShadersMutex);
			capturedShaders.clear();
		}
		// Publish this last so TrackActiveShader sees a fully initialized window.
		activeShaderCaptureFramesRemaining.store(kActiveShaderCaptureFrames, std::memory_order_release);
	}

	bool ShaderCache::IsCapturingActiveShaders() const
	{
		return activeShaderCaptureStage == ActiveShaderCaptureStage::FirstWindow ||
		       activeShaderCaptureStage == ActiveShaderCaptureStage::SecondWindow;
	}

	uint32_t ShaderCache::GetActiveShaderCaptureFramesRemaining() const
	{
		return activeShaderCaptureFramesRemaining.load(std::memory_order_relaxed);
	}

	bool ShaderCache::IsAwaitingMenuCloseCapture() const
	{
		return activeShaderCaptureStage == ActiveShaderCaptureStage::AwaitingMenuClose;
	}

	void ShaderCache::TickActiveShaderCapture(bool a_menuVisible)
	{
		if (activeShaderCaptureStage == ActiveShaderCaptureStage::Idle)
			return;

		const auto currentThread = std::this_thread::get_id();
		if (currentThread != activeShaderCaptureThread.load(std::memory_order_relaxed)) {
			// Some VR overlay paths may submit the click on a different thread from the
			// frame reset. Rebind to the actual render thread and restart the window;
			// this also keeps bulk off-thread shader loading out of the scene capture.
			activeShaderCaptureFramesRemaining.store(0, std::memory_order_release);
			{
				std::lock_guard lock(activeShadersMutex);
				capturedShaders.clear();
			}
			activeShaderCaptureThread.store(currentThread, std::memory_order_relaxed);
			activeShaderCaptureDeadline = std::chrono::steady_clock::now() + kActiveShaderCaptureTimeout;
			activeShaderCaptureFramesRemaining.store(kActiveShaderCaptureFrames, std::memory_order_release);
			logger::debug("Rebound smart shader capture to the render thread");
			return;
		}
		assert(currentThread == activeShaderCaptureThread.load(std::memory_order_relaxed));

		switch (activeShaderCaptureStage) {
		case ActiveShaderCaptureStage::Idle:
			return;

		case ActiveShaderCaptureStage::FirstWindow:
		case ActiveShaderCaptureStage::SecondWindow:
			{
				uint32_t remaining = 0;
				if (const auto previous = activeShaderCaptureFramesRemaining.load(std::memory_order_relaxed);
					previous > 0) {
					remaining = activeShaderCaptureFramesRemaining.fetch_sub(1, std::memory_order_acq_rel) - 1;
				}

				const bool expired =
					remaining == 0 || std::chrono::steady_clock::now() >= activeShaderCaptureDeadline;
				if (!expired)
					return;

				// Stop capture writes before moving and evicting the collected entries.
				activeShaderCaptureFramesRemaining.store(0, std::memory_order_release);
				const bool secondWindow = activeShaderCaptureStage == ActiveShaderCaptureStage::SecondWindow;
				ClearActive(!secondWindow);

				if (secondWindow) {
					activeShaderCaptureStage = ActiveShaderCaptureStage::Idle;
				} else if (!a_menuVisible) {
					StartActiveShaderCaptureWindow(ActiveShaderCaptureStage::SecondWindow);
				} else {
					activeShaderCaptureStage = ActiveShaderCaptureStage::AwaitingMenuClose;
					activeShaderCaptureMenuWasVisible = true;
				}
				return;
			}

		case ActiveShaderCaptureStage::AwaitingMenuClose:
			if (activeShaderCaptureMenuWasVisible && !a_menuVisible)
				StartActiveShaderCaptureWindow(ActiveShaderCaptureStage::SecondWindow);
			activeShaderCaptureMenuWasVisible = a_menuVisible;
			return;
		}
	}

	size_t ShaderCache::ClearActive(bool a_clearFeatures)
	{
		std::vector<ActiveShaderInfo> entries;
		{
			std::lock_guard lock(activeShadersMutex);
			entries.reserve(capturedShaders.size());
			for (auto& [key, info] : capturedShaders)
				entries.push_back(std::move(info));
			capturedShaders.clear();
		}

		const auto start = std::chrono::steady_clock::now();
		std::unordered_set<size_t> taskIds;
		std::vector<std::wstring> diskPaths;
		taskIds.reserve(entries.size());
		diskPaths.reserve(entries.size());

		size_t evictedCount = 0;
		for (const auto& entry : entries) {
			if (clearedThisCaptureCycle.contains(entry.key))
				continue;

			// Do not race an in-flight compile or allow a duplicate task to write the
			// same blob. A later clear can pick it up once it has completed.
			if (GetShaderStatus(entry.key) == ShaderCompilationTask::Status::Pending)
				continue;

			EvictShader(entry.key, entry.shaderType, entry.descriptor, entry.shaderClass);
			taskIds.insert(ShaderCompilationTask::MakeId(entry.shaderClass, entry.shaderType, entry.descriptor));
			diskPaths.push_back(entry.diskPath);
			clearedThisCaptureCycle.insert(entry.key);
			++evictedCount;
		}

		// A scene can encounter the same blob through more than one tracked key.
		std::sort(diskPaths.begin(), diskPaths.end());
		diskPaths.erase(std::unique(diskPaths.begin(), diskPaths.end()), diskPaths.end());
		DeleteScopedDiskCacheEntries(diskPaths);
		compilationSet.Forget(taskIds);

		if (a_clearFeatures) {
			globals::deferred->ClearShaderCache();
			for (auto* feature : Feature::GetFeatureList()) {
				if (feature->loaded)
					feature->ClearShaderCacheScoped();
			}
		}

		const auto elapsed = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - start);
		lastScopedClearCount = evictedCount;
		lastScopedClearMs = elapsed.count();
		logger::info(
			"Smart shader cache clear: evicted {} shader(s) in {:.2f} ms",
			lastScopedClearCount,
			lastScopedClearMs);

		return lastScopedClearCount;
	}
}
