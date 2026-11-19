#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <utility>

/** Owns application captures from pose acquisition through a complete stereo submit. */
class RenderDocFrameCapture
{
public:
	static constexpr uint32_t kMinFrames = 1, kMaxFrames = 120;
	/** Replaces queued requests; an already active stereo capture completes first. */
	void Queue(uint32_t frames) noexcept { pending.store(std::clamp(frames, kMinFrames, kMaxFrames)); }
	/** Cancels queued work without interrupting an already active stereo pair. */
	void CancelPending() noexcept { pending.store(0); }
	uint32_t Pending() const noexcept { return pending.load(); }
	uint32_t Completed() const noexcept { return completed.load(); }
	uint32_t Failed() const noexcept { return failed.load(); }
	bool IsActive() const noexcept { return activeCycle.load() != 0; }

	/** Called under the presentation lock before rendering a new compositor cycle. */
	template <class API>
	void Begin(API* api, void* device, uint64_t cycle, bool posesReady)
	{
		if (!api)
			return;
		if (activeCycle.load() != 0) {
			activeCycle.store(0);
			api->DiscardFrameCapture(activeDevice, nullptr);
			activeDevice = nullptr;
			++failed;
		}
		auto requested = pending.load();
		if (!requested || !posesReady || !cycle || !device || api->IsFrameCapturing())
			return;
		while (requested && !pending.compare_exchange_weak(requested, requested - 1)) {}
		if (!requested)
			return;
		api->StartFrameCapture(device, nullptr);
		if (!api->IsFrameCapturing()) {
			++failed;
			return;
		}
		activeDevice = device;
		activeCycle.store(cycle);
	}

	/** Discards an invalid owned capture and cancels its remaining queued frames. */
	template <class API>
	void Fail(API* api)
	{
		CancelPending();
		if (api && activeCycle.exchange(0) != 0) {
			api->DiscardFrameCapture(std::exchange(activeDevice, nullptr), nullptr);
			++failed;
		}
	}

	/** Called only after both eye calls return within the same verified native pair. */
	template <class API>
	void Complete(API* api, uint64_t cycle)
	{
		if (!api || !cycle || activeCycle.load() != cycle)
			return;
		activeCycle.store(0);
		if (api->EndFrameCapture(std::exchange(activeDevice, nullptr), nullptr))
			++completed;
		else
			++failed;
	}

private:
	std::atomic_uint32_t pending{ 0 }, completed{ 0 }, failed{ 0 };
	std::atomic_uint64_t activeCycle{ 0 };
	void* activeDevice = nullptr;
};
