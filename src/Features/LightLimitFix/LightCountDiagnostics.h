#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace LightLimitFixDetail
{
	/** On-demand counts of admitted engine point lights, excluding spot and particle lights. */
	class LightCountDiagnostics
	{
	public:
		static constexpr float ColourTolerance = 0.01f;
		struct Counts
		{
			std::uint32_t pointLights = 0;
			std::uint32_t shadowCastingPointLights = 0;
			std::uint32_t colouredPointLights = 0;

			/** Classify the submitted CPU colour; shadow and colour categories overlap. */
			void RecordPoint(bool shadow, float red, float green, float blue)
			{
				++pointLights;
				shadowCastingPointLights += shadow;
				if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue))
					return;
				const auto minimum = std::min({ red, green, blue });
				const auto maximum = std::max({ red, green, blue });
				colouredPointLights += minimum >= 0 && maximum > 0 && maximum - minimum > maximum * ColourTolerance;
			}
		};

		struct Sample
		{
			Counts counts;
			std::uint32_t frame = 0;
			bool available = false;
		};

		/** Renew visible-view demand without replacing a newer concurrent request. */
		void Request(std::uint32_t frame)
		{
			auto requested = requestedFrame.load(std::memory_order_relaxed);
			while (requested == NoRequest || std::uint32_t(frame - std::uint32_t(requested)) < (1u << 31)) {
				if (requestedFrame.compare_exchange_weak(requested, frame, std::memory_order_release, std::memory_order_relaxed))
					return;
			}
		}

		/** Consume bounded demand before light processing; inactive calls only read the request. */
		bool ConsumeRequest(std::uint32_t frame)
		{
			auto requested = requestedFrame.load(std::memory_order_acquire);
			if (requested == NoRequest)
				return false;
			if (!requestedFrame.compare_exchange_strong(requested, NoRequest, std::memory_order_acq_rel))
				return false;
			return std::uint32_t(frame - std::uint32_t(requested)) <= 1;
		}

		/** Visit the prepared CPU light list only on demand; inactive calls do not invoke the collector. */
		template <class Collector>
		void Collect(std::uint32_t frame, Collector&& collector)
		{
			if (!ConsumeRequest(frame))
				return;
			std::uint64_t captureId = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
			{
				const std::scoped_lock lock(mutex);
				if (capture.state == CaptureState::Pending && std::uint32_t(frame - capture.requestedFrame) <= 1)
					captureId = capture.id;
			}
#endif
			Sample collected{ .frame = frame };
			try {
				collected.counts = std::forward<Collector>(collector)();
				collected.available = true;
			} catch (...) {
				Publish(collected, captureId);
				throw;
			}
			Publish(collected, captureId);
		}

		/** Read without requesting collection; old samples are never presented as current. */
		Sample Read(std::uint32_t frame) const
		{
			const std::scoped_lock lock(mutex);
			return std::uint32_t(frame - sample.frame) <= 1 ? sample : Sample{};
		}

#ifdef DEVBENCH_BRIDGE_ENABLED
		enum class CaptureState
		{
			Idle,
			Pending,
			Complete,
			Expired,
			Failed
		};
		struct Capture
		{
			std::uint64_t id = 0;
			std::uint32_t requestedFrame = 0;
			CaptureState state = CaptureState::Idle;
			Sample sample;
		};

		/** Request one correlated API sample; UI renewals cannot erase its completed result. */
		std::uint64_t RequestCapture(std::uint32_t frame)
		{
			std::uint64_t id;
			{
				const std::scoped_lock lock(mutex);
				if (capture.id == std::numeric_limits<std::uint64_t>::max())
					throw std::overflow_error("Light diagnostics capture identifiers exhausted");
				id = capture.id + 1;
				capture = { id, frame, CaptureState::Pending };
			}
			Request(frame);
			return id;
		}

		/** Retain completed results for asynchronous polling; expired requests never imply zero lights. */
		Capture ReadCapture(std::uint32_t frame) const
		{
			const std::scoped_lock lock(mutex);
			auto result = capture;
			if (result.state == CaptureState::Pending && std::uint32_t(frame - result.requestedFrame) > 1)
				result.state = CaptureState::Expired;
			return result;
		}
#endif

	private:
		void Publish(const Sample& value, [[maybe_unused]] std::uint64_t captureId)
		{
			const std::scoped_lock lock(mutex);
			sample = value;
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (captureId && capture.id == captureId && capture.state == CaptureState::Pending) {
				capture.sample = value;
				capture.state = value.available ? CaptureState::Complete : CaptureState::Failed;
			}
#endif
		}
		static constexpr auto NoRequest = std::numeric_limits<std::uint64_t>::max();
		std::atomic_uint64_t requestedFrame{ NoRequest };
		mutable std::mutex mutex;
		Sample sample;
#ifdef DEVBENCH_BRIDGE_ENABLED
		Capture capture;
#endif
	};
}
