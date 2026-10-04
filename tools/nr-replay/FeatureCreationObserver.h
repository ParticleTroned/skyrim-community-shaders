#pragma once

#include <Windows.h>
#include <functional>
#include <stdexcept>
#include <utility>

struct NVSDK_NGX_Parameter;

namespace NrReplay
{
	/** Standalone observer of the exact NGX parameters passed to feature creation. */
	class FeatureCreationObserver
	{
	public:
		using Callback = std::function<void(const NVSDK_NGX_Parameter*)>;
		explicit FeatureCreationObserver(Callback callback) : callback_(std::move(callback)), thread_(GetCurrentThreadId())
		{
			if (owner_ || !callback_)
				throw std::runtime_error("feature creation observer ownership unavailable");
			owner_ = this;
		}
		FeatureCreationObserver(const FeatureCreationObserver&) = delete;
		FeatureCreationObserver& operator=(const FeatureCreationObserver&) = delete;
		~FeatureCreationObserver() { owner_ = nullptr; }
		static void Observe(const NVSDK_NGX_Parameter* parameters)
		{
			if (!owner_)
				return;
			if (GetCurrentThreadId() != owner_->thread_ || !parameters)
				throw std::runtime_error("feature creation escaped the owned observation thread");
			owner_->callback_(parameters);
		}

	private:
		Callback callback_;
		DWORD thread_;
		inline static FeatureCreationObserver* owner_ = nullptr;
	};
}
