#pragma once

#include <cstdint>

namespace RigidMotion
{
	/** Retain the last rendered transform without advancing history between stereo draws. */
	template <class Transform>
	class History
	{
	public:
		const Transform& Observe(std::uint32_t a_frame, const Transform& a_current)
		{
			if (!initialized || a_frame != frame) {
				previous = initialized && a_frame - frame == 1 ? current : a_current;
				frame = a_frame;
				initialized = true;
			}
			current = a_current;
			return previous;
		}

	private:
		Transform current{};
		Transform previous{};
		std::uint32_t frame = 0;
		bool initialized = false;
	};

	/** Limit a history override to native constant generation, including exceptional exits. */
	template <class Geometry>
	class PreviousWorldScope
	{
	public:
		using Transform = decltype(Geometry::previousWorld);

		PreviousWorldScope(Geometry& a_geometry, const Transform& a_previous) :
			geometry(a_geometry), original(a_geometry.previousWorld)
		{
			geometry.previousWorld = a_previous;
		}
		~PreviousWorldScope() { geometry.previousWorld = original; }
		PreviousWorldScope(const PreviousWorldScope&) = delete;
		PreviousWorldScope& operator=(const PreviousWorldScope&) = delete;

	private:
		Geometry& geometry;
		Transform original;
	};
}
