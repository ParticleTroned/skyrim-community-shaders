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
	bool ShaderCache::TryDeferEviction(const hlslRecord& a_record)
	{
		const auto taskId = ShaderCompilationTask::MakeId(
			a_record.shaderClass,
			a_record.type,
			a_record.descriptor);
		// Capture ownership before Clear(path) resets compilation bookkeeping.
		const bool waitForTaskCompletion = compilationSet.IsInProgress(taskId);

		std::unique_lock lockM{ mapMutex };
		auto shader = shaderMap.find(a_record.key);
		if (shader == shaderMap.end() ||
			shader->second.status != ShaderCompilationTask::Status::Pending) {
			return false;
		}

		auto [eviction, inserted] = deferredEvictions.try_emplace(
			a_record.key,
			DeferredEviction{ a_record, waitForTaskCompletion });
		if (!inserted && !eviction->second.applying) {
			eviction->second.record = a_record;
			eviction->second.waitForTaskCompletion |= waitForTaskCompletion;
		}
		deferredEvictionCount.store(deferredEvictions.size(), std::memory_order_relaxed);
		return true;
	}

	bool ShaderCache::ApplyDeferredEviction(const std::string& a_key, bool a_taskCompleted)
	{
		if (deferredEvictionCount.load(std::memory_order_relaxed) == 0)
			return false;

		std::optional<DeferredEviction> eviction;
		{
			std::unique_lock lockM{ mapMutex };
			auto found = deferredEvictions.find(a_key);
			if (found == deferredEvictions.end() ||
				found->second.applying ||
				(found->second.waitForTaskCompletion && !a_taskCompleted)) {
				return false;
			}
			found->second.applying = true;
			eviction.emplace(found->second);
		}

		const auto& record = eviction->record;
		EvictShader(record.key, record.type, record.descriptor, record.shaderClass);
		DeleteScopedDiskCacheEntries({ record.diskPath });
		compilationSet.Forget({ ShaderCompilationTask::MakeId(
			record.shaderClass,
			record.type,
			record.descriptor) });
		{
			std::unique_lock lockM{ mapMutex };
			if (auto found = deferredEvictions.find(a_key);
				found != deferredEvictions.end() && found->second.applying) {
				deferredEvictions.erase(found);
			}
			deferredEvictionCount.store(deferredEvictions.size(), std::memory_order_relaxed);
		}
		mapCV.notify_all();
		return true;
	}

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

			const size_t taskId = ShaderCompilationTask::MakeId(entry.shaderClass, entry.shaderType, entry.descriptor);
			// AddCompletedShader publishes a completed blob before the worker installs
			// the runtime shader and retires its task. Require both phases to finish so
			// scoped eviction cannot race a late runtime insertion or lose bookkeeping.
			if (GetShaderStatus(entry.key) == ShaderCompilationTask::Status::Pending ||
				compilationSet.IsInProgress(taskId)) {
				continue;
			}

			EvictShader(entry.key, entry.shaderType, entry.descriptor, entry.shaderClass);
			taskIds.insert(taskId);
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
