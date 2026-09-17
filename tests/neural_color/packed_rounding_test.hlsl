#define main ReconstructMain
#include "Upscaling/NeuralRendering/ColorReconstructCS.hlsl"
#undef main

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	float4 value = Baseline.Load(int3(id.xy, 0));
	Result[id.xy] = ColorMode == 0 ? value : float4(RoundPreserveSourceForStorage(value.rgb), value.a);
}
