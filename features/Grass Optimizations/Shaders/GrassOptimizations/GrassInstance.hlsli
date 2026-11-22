#ifndef GRASS_INSTANCE_HLSLI
#define GRASS_INSTANCE_HLSLI

struct GrassInstanceExtra
{
	float4 originFade;
	float2 wind;
	float previousPlacementDelta;
};

float2 UnpackGrassInstance(uint value) { return float2(f16tof32(value), f16tof32(value >> 16)); }

void LoadGrassInstance(ByteAddressBuffer records, uint address, bool compact,
	out float4 data1, out float4 data2, out float4 data3, out float4 data4, out uint seed)
{
	uint4 a = records.Load4(address);
	seed = a.y;
	data1 = float4(UnpackGrassInstance(a.x), UnpackGrassInstance(a.y));
	[branch] if (compact)
	{
		float3 small = (float3(a.z & 0x7FFF, (a.z >> 15) & 0x7FFF, a.w & 0x7FFF) - 16384.0) * (0.70710678 / 16383.0);
		float large = sqrt(saturate(1 - dot(small, small)));
		uint omitted = a.z >> 30;
		float4 q = omitted == 0 ? float4(large, small) : omitted == 1 ? float4(small.x, large, small.yz) :
		                                             omitted == 2     ? float4(small.xy, large, small.z) :
		                                                                float4(small, large);
		float3x3 rotation = float3x3(
			1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y - q.z * q.w), 2 * (q.x * q.z + q.y * q.w),
			2 * (q.x * q.y + q.z * q.w), 1 - 2 * (q.x * q.x + q.z * q.z), 2 * (q.y * q.z - q.x * q.w),
			2 * (q.x * q.z - q.y * q.w), 2 * (q.y * q.z + q.x * q.w), 1 - 2 * (q.x * q.x + q.y * q.y));
		data2 = float4(rotation[0], rotation[2][1]);
		data3 = float4(rotation[1], rotation[2][2]);
		data4 = float4(rotation[2][0], f16tof32(a.w >> 16), 0, 0);
	}
	else
	{
		uint4 b = records.Load4(address + 16);
		data2 = float4(UnpackGrassInstance(a.z), UnpackGrassInstance(a.w));
		data3 = float4(UnpackGrassInstance(b.x), UnpackGrassInstance(b.y));
		data4 = float4(UnpackGrassInstance(b.z), UnpackGrassInstance(b.w));
	}
}

float GrassWindScalar(float2 root, float timer)
{
	float angle = 0.4 * ((root.x + root.y) * -0.0078125 + timer);
	float s, c;
	sincos(angle, s, c);
	return (sin(Math::PI * s) + sin(Math::TAU * s)) * 0.3 + 0.2 * cos(Math::PI * c);
}

#endif
