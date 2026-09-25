#pragma once

#include <array>
#include <cstdint>

namespace CSX::Api
{
	/** No retained geometry ownership; mismatched scopes invalidate attribution. */
	class AcceptedDrawGeometryScope
	{
	public:
		/** Hide parent geometry during setup; false means the bounded stack overflowed. */
		bool Begin(const void* a_pass)
		{
			const bool fits = depth < entries.size();
			if (fits)
				entries[depth] = { a_pass, nullptr };
			++depth;
			return fits;
		}
		/** Publish borrowed geometry only for the currently verified pass. */
		void Activate(const void* a_pass, const void* a_geometry)
		{
			if (depth && depth <= entries.size() && entries[depth - 1].pass == a_pass)
				entries[depth - 1].geometry = a_geometry;
		}
		/** Hide geometry while the native shader restores its state. */
		void Suspend()
		{
			if (depth && depth <= entries.size())
				entries[depth - 1].geometry = nullptr;
		}
		/** Restore the parent, or invalidate all attribution when pairing is unverifiable. */
		bool End(const void* a_pass)
		{
			if (!depth || depth > entries.size() || entries[depth - 1].pass != a_pass) {
				depth = 0;
				return false;
			}
			--depth;
			return true;
		}
		/** Return a callback-lifetime identity, or null while attribution is unavailable. */
		const void* Current() const
		{
			return depth && depth <= entries.size() ? entries[depth - 1].geometry : nullptr;
		}

	private:
		struct Entry
		{
			const void* pass = nullptr;
			const void* geometry = nullptr;
		};
		std::array<Entry, 32> entries;
		uint32_t depth = 0;
	};
}
