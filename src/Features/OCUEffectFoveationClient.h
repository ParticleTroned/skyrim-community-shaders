#pragma once

#include "OCUEffectFoveationPolicy.h"
#include <windows.h>

namespace OCUEffectFoveation
{

	// Render-thread only. Never loads another OpenVR runtime or retains gaze
	// across game frames. A successful provider response may itself be Disabled.
	class Client
	{
	public:
		enum class Status
		{
			Disabled,
			ProviderUnavailable,
			QueryUnsupported,
			ProfileDisabled,
			InvalidOrStale,
			Active
		};
		Status GetStatus() const noexcept { return status; }

		Constants ReadForFrame(std::uint64_t a_rendererFrame, bool a_enabled) noexcept
		{
			if (!a_enabled) {
				cached = {};
				hasCachedFrame = false;
				status = Status::Disabled;
				return {};
			}
			// Prefer app-local OCU. A normal Valve loader is not an OCU provider;
			// system-wide installs expose the same exact export in vrclient_x64.
			// Check selection before cache reuse so a late loader or provider change
			// cannot keep a publication from the previous selection alive.
			HMODULE selectedModule = GetModuleHandleW(L"openvr_api.dll");
			auto selectedQuery = ResolveQuery(selectedModule);
			if (!selectedQuery) {
				selectedModule = GetModuleHandleW(L"vrclient_x64.dll");
				selectedQuery = ResolveQuery(selectedModule);
			}
			if (!selectedQuery)
				selectedModule = nullptr;
			if (selectedModule != module || selectedQuery != query) {
				module = selectedModule;
				query = selectedQuery;
				freshness = {};
				cached = {};
				hasCachedFrame = false;
			}
			if (hasCachedFrame && a_rendererFrame == cachedFrame)
				return cached;
			cachedFrame = a_rendererFrame;
			hasCachedFrame = true;
			cached = {};
			status = Status::ProviderUnavailable;
			if (!query)
				return {};

			ocu_effect_foveation::Snapshot snapshot{};
			status = Status::QueryUnsupported;
			if (query(ocu_effect_foveation::Version, sizeof(snapshot), &snapshot) !=
				static_cast<std::uint32_t>(ocu_effect_foveation::Result::Success))
				return {};
			if (snapshot.structSize != sizeof(snapshot) || snapshot.version != ocu_effect_foveation::Version)
				return {};
			if (snapshot.mode == ocu_effect_foveation::Mode::Disabled) {
				status = Status::ProfileDisabled;
				return {};
			}
			status = Status::InvalidOrStale;
			LARGE_INTEGER now{}, frequency{};
			if (!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&frequency))
				return {};
			cached = freshness.Consume(snapshot, now.QuadPart, frequency.QuadPart);
			if (cached.Active())
				status = Status::Active;
			return cached;
		}

	private:
		static ocu_effect_foveation::QueryFn ResolveQuery(HMODULE a_module) noexcept
		{
			return a_module ? reinterpret_cast<ocu_effect_foveation::QueryFn>(
								  GetProcAddress(a_module, "OCU_GetEffectFoveationV1")) :
			                  nullptr;
		}

		HMODULE module = nullptr;
		ocu_effect_foveation::QueryFn query = nullptr;
		FreshnessPolicy freshness;
		Constants cached;
		std::uint64_t cachedFrame = 0;
		bool hasCachedFrame = false;
		Status status = Status::Disabled;
	};

}  // namespace OCUEffectFoveation
