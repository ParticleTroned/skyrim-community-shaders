#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <chrono>
#	include <cmath>
#	include <cstdint>
#	include <imgui.h>
#	include <mutex>
#	include <nlohmann/json.hpp>
#	include <optional>
#	include <string>
#	include <string_view>

namespace MenuUI
{
	/** Observes the innermost settings viewport; scroll requests expire after navigation. */
	class DevBenchViewport
	{
		using Clock = std::chrono::steady_clock;
		struct Observation
		{
			std::string page, tab;
			int frame = -1;
			unsigned depth = 0;
			float y = 0, maximum = 0, height = 0;
			Clock::time_point at{};
		};
		struct Request
		{
			std::string page, tab;
			float ratio;
			int frame;
			std::uint64_t generation;
			Clock::time_point expires;
		};
		static inline std::mutex mutex;
		static Observation observed;
		static inline std::optional<Request> pending;
		static inline unsigned depth = 0;
		static inline std::uint64_t requested = 0, applied = 0;
		std::string page, tab;
		unsigned level;

	public:
		DevBenchViewport(std::string_view a_page, std::string_view a_tab, bool a_allowScroll) :
			page(a_page), tab(a_tab), level(++depth)
		{
			std::scoped_lock lock(mutex);
			if (pending && (Clock::now() > pending->expires || ImGui::GetFrameCount() > pending->frame + 2))
				pending.reset();
			if (pending && pending->page == page && pending->tab == tab) {
				if (a_allowScroll && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
					ImGui::SetScrollY(pending->ratio * ImGui::GetScrollMaxY());
					applied = pending->generation;
				}
				pending.reset();
			}
		}
		~DevBenchViewport()
		{
			std::scoped_lock lock(mutex);
			const auto frame = ImGui::GetFrameCount();
			if (observed.frame != frame || level >= observed.depth)
				observed = { page, tab, frame, level, ImGui::GetScrollY(), ImGui::GetScrollMaxY(), ImGui::GetWindowHeight(), Clock::now() };
			if (level == 1 && pending && frame > pending->frame)
				pending.reset();
			--depth;
		}
		DevBenchViewport(const DevBenchViewport&) = delete;
		DevBenchViewport& operator=(const DevBenchViewport&) = delete;

		/** Queue a finite normalized offset only for the freshly observed viewport. */
		static bool Scroll(std::string_view a_page, std::string_view a_tab, double a_ratio)
		{
			std::scoped_lock lock(mutex);
			const auto now = Clock::now();
			if (!std::isfinite(a_ratio) || a_ratio < 0 || a_ratio > 1 ||
				observed.frame < 0 || now - observed.at > std::chrono::milliseconds(500) ||
				observed.page != a_page || observed.tab != a_tab || (pending && now <= pending->expires))
				return false;
			pending = Request{ observed.page, observed.tab, static_cast<float>(a_ratio), observed.frame, ++requested, now + std::chrono::milliseconds(500) };
			return true;
		}
		/** Returns observed offsets; an applied request may become visible on the next UI frame. */
		static nlohmann::json Describe(bool a_menuOpen)
		{
			std::scoped_lock lock(mutex);
			const auto now = Clock::now();
			return { { "fresh", a_menuOpen && observed.frame >= 0 && now - observed.at <= std::chrono::milliseconds(500) },
				{ "page", observed.page }, { "tab", observed.tab }, { "frame", observed.frame },
				{ "scrollY", observed.y }, { "scrollMaxY", observed.maximum }, { "height", observed.height },
				{ "pending", pending.has_value() && now <= pending->expires },
				{ "requestedGeneration", requested }, { "appliedGeneration", applied } };
		}
	};
	inline DevBenchViewport::Observation DevBenchViewport::observed;
}
#endif
