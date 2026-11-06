#ifndef SNOW_EXCLUSIONS_HLSLI
#define SNOW_EXCLUSIONS_HLSLI
#ifdef __cplusplus
#	include <cstdint>
#	define SNOW_INLINE inline
#	define SNOW_ARG(T) const T&
#else
#	define SNOW_INLINE
#	define SNOW_ARG(T) T
#endif

namespace SnowExclusions
{
#ifdef __cplusplus
	using uint = uint32_t;
#endif
	static const uint MaxAreas = 8;
	static const uint MaxVertices = 16;

	struct Vertex
	{
		float x, y, pad0, pad1;
	};
	struct Area
	{
		float minX, minY, maxX, maxY;
		uint count, pad0, pad1, pad2;
		Vertex vertices[MaxVertices];
	};
	struct MaskData
	{
		uint count, pad0, pad1, pad2;
		Area areas[MaxAreas];
	};

	// Closed edges keep the weather and surface masks consistent at boundaries.
	SNOW_INLINE bool Contains(SNOW_ARG(Area) area, float x, float y)
	{
		if (area.count < 3 || area.count > MaxVertices ||
			x < area.minX || y < area.minY || x > area.maxX || y > area.maxY)
			return false;
		bool inside = false;
		uint previous = area.count - 1;
		for (uint i = 0; i < area.count; ++i) {
			Vertex a = area.vertices[previous];
			Vertex b = area.vertices[i];
			float cross = (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
			if (cross == 0 && (x - a.x) * (x - b.x) <= 0 && (y - a.y) * (y - b.y) <= 0)
				return true;
			if ((a.y > y) != (b.y > y)) {
				if ((cross > 0) == (b.y > a.y))
					inside = !inside;
			}
			previous = i;
		}
		return inside;
	}

	SNOW_INLINE bool ContainsAny(SNOW_ARG(MaskData) data, float x, float y)
	{
		for (uint i = 0; i < data.count && i < MaxAreas; ++i)
			if (Contains(data.areas[i], x, y))
				return true;
		return false;
	}
#ifdef __cplusplus
	static_assert(sizeof(Vertex) == 16);
	static_assert(sizeof(Area) == 32 + 16 * MaxVertices);
	static_assert(sizeof(MaskData) == 16 + sizeof(Area) * MaxAreas);
#endif
}

#undef SNOW_INLINE
#undef SNOW_ARG
#endif
