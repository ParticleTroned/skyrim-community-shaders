#pragma once

#include "RenderMap/Artifacts.h"

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace CSX::RenderMap
{
	struct PreparedCapturePublication
	{
		CaptureArtifactBundle artifacts;
		bool finished{ false };
	};

	static_assert(std::is_nothrow_move_assignable_v<CaptureArtifactBundle>);

	/// Reserves cache ownership before publication; callers hold the cache lock and callbacks must not re-enter it.
	/// A failed response retains the verified bundle for a fresh command without rewriting immutable artifacts.
	template <class Cache, class WriteArtifacts, class PrepareResponse>
	decltype(auto) WithPreparedCaptureArtifacts(Cache& a_cache, std::string_view a_captureId,
		WriteArtifacts&& a_write, PrepareResponse&& a_response)
	{
		const auto [entry, inserted] = a_cache.try_emplace(std::string(a_captureId));
		if (!entry->second.finished) {
			try {
				auto artifacts = a_write();
				entry->second.artifacts = std::move(artifacts);
				entry->second.finished = true;
			} catch (...) {
				if (inserted)
					a_cache.erase(entry);
				throw;
			}
		}
		return a_response(entry->second.artifacts);
	}
}
