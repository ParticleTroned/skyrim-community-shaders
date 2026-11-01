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
#	include <utility>

namespace MenuUI
{
	/** Observes the innermost settings viewport; navigation invalidates its requests. */
	class DevBenchViewport
	{
		using Clock = std::chrono::steady_clock;
		static constexpr auto maximumAge = std::chrono::milliseconds(500);
		static constexpr int maximumPendingFrames = 2;
		struct Observation
		{
			std::string page, tab;
			ImGuiContext* context = nullptr;
			ImGuiID window = 0;
			int frame = -1;
			unsigned depth = 0;
			float y = 0, maximum = 0, height = 0;
			Clock::time_point at{};
		};
		struct Request
		{
			std::string page, tab;
			ImGuiContext* context;
			ImGuiID window;
			float ratio;
			int frame;
			std::uint64_t generation;
			Clock::time_point expires;
		};
		static inline std::mutex mutex;
		static Observation observed;
		static inline std::optional<Request> pending;
		static inline unsigned depth = 0;
		static inline std::uint64_t requested = 0, applied = 0, navigationGeneration = 0;
		std::string page, tab;
		unsigned level;
		ImGuiID window;
		bool allowScroll;
		std::uint64_t navigationAtEntry;

		static bool Fresh(Clock::time_point a_now)
		{
			return observed.frame >= 0 && observed.context == ImGui::GetCurrentContext() &&
			       ImGui::GetFrameCount() >= observed.frame && ImGui::GetFrameCount() - observed.frame <= 1 && a_now - observed.at <= maximumAge;
		}
		static bool PendingExpired(Clock::time_point a_now)
		{
			return pending && (a_now > pending->expires || pending->context != ImGui::GetCurrentContext() ||
								  ImGui::GetFrameCount() < pending->frame || ImGui::GetFrameCount() - pending->frame > maximumPendingFrames);
		}

	public:
		DevBenchViewport(std::string_view a_page, std::string_view a_tab, bool a_allowScroll) :
			page(a_page), tab(a_tab), window(ImGui::GetID("##DevBenchViewport")), allowScroll(a_allowScroll)
		{
			std::scoped_lock lock(mutex);
			level = ++depth;
			navigationAtEntry = navigationGeneration;
			if (PendingExpired(Clock::now()))
				pending.reset();
		}
		~DevBenchViewport()
		{
			std::scoped_lock lock(mutex);
			const auto frame = ImGui::GetFrameCount();
			const auto now = Clock::now();
			if (PendingExpired(now))
				pending.reset();
			// Nested content resolves the scroll owner before any target is issued.
			if (navigationAtEntry == navigationGeneration && (observed.frame != frame || level >= observed.depth)) {
				if (pending && pending->page == page && pending->tab == tab) {
					if (pending->window == window && allowScroll && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
						ImGui::SetScrollY(pending->ratio * ImGui::GetScrollMaxY());
						applied = pending->generation;
					}
					pending.reset();
				}
				observed = { std::move(page), std::move(tab), ImGui::GetCurrentContext(), window, frame, level, ImGui::GetScrollY(), ImGui::GetScrollMaxY(), ImGui::GetWindowHeight(), now };
			}
			if (level == 1 && pending && frame > pending->frame)
				pending.reset();
			--depth;
		}
		DevBenchViewport(const DevBenchViewport&) = delete;
		DevBenchViewport& operator=(const DevBenchViewport&) = delete;

		/** Cancel queued scrolling and require a new observation after navigation or closure. */
		static void Invalidate()
		{
			std::scoped_lock lock(mutex);
			++navigationGeneration;
			observed.frame = -1;
			pending.reset();
		}
		/** Queue a finite normalized offset only for the freshly observed viewport. */
		static bool Scroll(std::string_view a_page, std::string_view a_tab, double a_ratio)
		{
			std::scoped_lock lock(mutex);
			const auto now = Clock::now();
			if (PendingExpired(now))
				pending.reset();
			if (!std::isfinite(a_ratio) || a_ratio < 0 || a_ratio > 1 || !Fresh(now) ||
				observed.page != a_page || observed.tab != a_tab || pending)
				return false;
			pending = Request{ observed.page, observed.tab, observed.context, observed.window, static_cast<float>(a_ratio), observed.frame, ++requested, now + maximumAge };
			return true;
		}
		/** Returns observed offsets; an applied request may become visible on the next UI frame. */
		static nlohmann::json Describe(bool a_menuOpen)
		{
			std::scoped_lock lock(mutex);
			const auto now = Clock::now();
			return { { "fresh", a_menuOpen && Fresh(now) },
				{ "page", observed.page }, { "tab", observed.tab }, { "frame", observed.frame },
				{ "scrollY", observed.y }, { "scrollMaxY", observed.maximum }, { "height", observed.height },
				{ "pending", pending.has_value() && !PendingExpired(now) },
				{ "requestedGeneration", requested }, { "appliedGeneration", applied } };
		}
	};
	inline DevBenchViewport::Observation DevBenchViewport::observed;
}
#endif
