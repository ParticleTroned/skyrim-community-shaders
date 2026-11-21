#ifndef CSX_NORMAL_BLENDING_HLSLI
#define CSX_NORMAL_BLENDING_HLSLI

namespace NormalBlending
{
	// Reoriented normal mapping, https://blog.selfshadow.com/publications/blending-in-detail/
	float3 Reorient(float3 u, float3 t, float3 s)
	{
		float d = dot(s, t);
		float denominator = 2.0 * (d + 1.0);
		if (denominator <= 1e-6)
			return u;
		float4 q = float4(cross(s, t), d + 1.0) * rsqrt(denominator);
		return u * (q.w * q.w - dot(q.xyz, q.xyz)) + 2 * q.xyz * dot(q.xyz, u) + 2 * q.w * cross(q.xyz, u);
	}

	float3 Reorient(float3 n1, float3 n2)
	{
		n1 += float3(0, 0, 1);
		n2 *= float3(-1, -1, 1);
		float safeZ = abs(n1.z) < 1e-6 ? (n1.z < 0 ? -1e-6 : 1e-6) : n1.z;
		return n1 * dot(n1, n2) / safeZ - n2;
	}
}
#endif
