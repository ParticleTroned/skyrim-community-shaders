#include "Upscaling/NeuralRendering/MonoCommon.hlsli"
#include "Upscaling/NeuralRendering/MonoSelection.hlsli"
RWByteAddressBuffer Coverage : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= SourceSize) || Packing.x == 0u) return;
	if (SelectionWeight(id.xy) * ScreenWeight(id.xy) > 0.0 && SourceTile(id.xy) < 0)
		Coverage.InterlockedOr(0, 1u);
}
