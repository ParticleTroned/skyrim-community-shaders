#include "Common/DepthOrder.hlsli"

#ifndef BASE_REDUCTION
#	define BASE_REDUCTION 2
#endif

cbuffer Parameters : register(b0)
{
	uint Width, Height, SourceWidth, SourceHeight;
	uint Eyes, FirstLevel, ActiveWidth, ActiveHeight;
};
Texture2D<float> Source : register(t0);
RWTexture2D<float> Output : register(u0);
float SafeDepth(uint2 pixel)
{
	float value = Source.Load(int3(pixel, 0));
	return isfinite(value) && value >= 0 && value <= 1 ? value : DepthOrder::Far();
}
[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	if (id.x >= Width || id.y >= Height)
		return;
	float maximum = DepthOrder::Near();
	uint eyeWidth = Width / Eyes;
	uint eye = id.x / eyeWidth;
	uint2 base = uint2(id.x % eyeWidth, id.y) * (FirstLevel ? BASE_REDUCTION : 2);
	[unroll] for (uint y = 0; y < BASE_REDUCTION; ++y)
		[unroll] for (uint x = 0; x < BASE_REDUCTION; ++x)
	{
		if (!FirstLevel && (x >= 2 || y >= 2))
			continue;
		uint2 pixel = base + uint2(x, y);
		if (FirstLevel) {
			if (pixel.x >= ActiveWidth / Eyes || pixel.y >= ActiveHeight)
				maximum = DepthOrder::Far();
			else
				maximum = DepthOrder::Farthest(maximum, SafeDepth(uint2(pixel.x + eye * (ActiveWidth / Eyes), pixel.y)));
		} else {
			pixel.x += eye * (SourceWidth / Eyes);
			maximum = DepthOrder::Farthest(maximum, SafeDepth(min(pixel, uint2(SourceWidth, SourceHeight) - 1)));
		}
	}
	Output[id.xy] = isfinite(maximum) ? maximum : DepthOrder::Far();
}
