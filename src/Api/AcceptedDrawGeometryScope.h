#pragma once

#include <array>
#include <cstdint>

namespace CSX::Api
{
	/** No retained geometry ownership; mismatched scopes invalidate attribution. */
	class AcceptedDrawGeometryScope
	{
	public:
		bool Begin(const void* a_pass)
		{
			const bool fits = depth < entries.size();
			if (fits)
				entries[depth] = { a_pass, nullptr };
			++depth;
			return fits;
		}
		void Activate(const void* a_pass, const void* a_geometry)
		{
			if (depth && depth <= entries.size() && entries[depth - 1].pass == a_pass)
				entries[depth - 1].geometry = a_geometry;
		}
		void Suspend()
		{
			if (depth && depth <= entries.size())
				entries[depth - 1].geometry = nullptr;
		}
		bool End(const void* a_pass)
		{
			if (!depth || (depth <= entries.size() && entries[depth - 1].pass != a_pass)) {
				depth = 0;
				return false;
			}
			--depth;
			return true;
		}
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
