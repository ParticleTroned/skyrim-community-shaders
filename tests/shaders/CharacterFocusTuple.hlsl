#include "../../package/Shaders/Common/CharacterCategoryMask.hlsli"

RWTexture2D<unorm float2> Output : register(u0);

[numthreads(8, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	Output[id.xy] = CharacterCategoryMask::Encode(0.25, id.y + 1u, 1.0, id.x).xy;
}
