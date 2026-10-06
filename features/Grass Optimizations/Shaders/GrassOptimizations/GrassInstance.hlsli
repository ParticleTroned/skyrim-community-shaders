#ifndef GRASS_INSTANCE_HLSLI
#define GRASS_INSTANCE_HLSLI

struct GrassInstanceExtra
{
	float4 originFade;
	float2 wind;
};

float GrassWindScalar(float2 root, float timer)
{
	float angle = 0.4 * ((root.x + root.y) * -0.0078125 + timer);
	float s, c;
	sincos(angle, s, c);
	return (sin(Math::PI * s) + sin(Math::TAU * s)) * 0.3 + 0.2 * cos(Math::PI * c);
}

#endif
