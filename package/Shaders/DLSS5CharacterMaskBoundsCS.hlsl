// Exact bounds of positive pixels in the resolved, eye-local R8 selection mask.
// One group owns one 32x32 tile and always replaces its output record, including
// empty tiles. Bounds are global mask coordinates with exclusive maximum edges.
cbuffer CharacterMaskBoundsCB : register(b0)
{
	uint4 Size;  // mask width, mask height, tile count X, reserved
};

Texture2D<unorm float> CharacterSelectionMask : register(t0);
RWStructuredBuffer<uint4> TileBounds : register(u0);
groupshared uint4 GroupBounds[64];

[numthreads(8, 8, 1)]
void main(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID,
	uint lane : SV_GroupIndex)
{
	uint2 minimum = uint2(0xffffffffu, 0xffffffffu);
	uint2 maximum = 0u;
	const uint2 base = groupID.xy * 32u + threadID.xy * 4u;
	[unroll] for (uint y = 0u; y < 4u; ++y) {
		[unroll] for (uint x = 0u; x < 4u; ++x) {
			const uint2 pixel = base + uint2(x, y);
			if (all(pixel < Size.xy) &&
				CharacterSelectionMask.Load(int3(pixel, 0)) > 0.0) {
				minimum = min(minimum, pixel);
				maximum = max(maximum, pixel + 1u);
			}
		}
	}

	GroupBounds[lane] = uint4(minimum, maximum);
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 32u; stride > 0u; stride >>= 1u) {
		if (lane < stride) {
			GroupBounds[lane].xy = min(GroupBounds[lane].xy, GroupBounds[lane + stride].xy);
			GroupBounds[lane].zw = max(GroupBounds[lane].zw, GroupBounds[lane + stride].zw);
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0u) {
		TileBounds[groupID.y * Size.z + groupID.x] =
			GroupBounds[0].x == 0xffffffffu ? uint4(0u, 0u, 0u, 0u) : GroupBounds[0];
	}
}
