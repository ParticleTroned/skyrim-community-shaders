#ifndef CSX_FINITE
#define CSX_FINITE

namespace Finite
{
	static const float kMaximumHalf = 65504.0;

	// Bit classification must survive floating-point folding around storage clamps.
	bool IsFinite(float value)
	{
		return (asuint(value) & 0x7F800000u) != 0x7F800000u;
	}

	bool2 IsFinite(float2 value)
	{
		return bool2(IsFinite(value.x), IsFinite(value.y));
	}

	bool3 IsFinite(float3 value)
	{
		return bool3(IsFinite(value.xy), IsFinite(value.z));
	}

	bool4 IsFinite(float4 value)
	{
		return bool4(IsFinite(value.xy), IsFinite(value.zw));
	}
}

#endif
