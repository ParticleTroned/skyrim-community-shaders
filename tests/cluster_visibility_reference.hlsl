#include "LightLimitFix/ClusterCullingCS.hlsl"

RWStructuredBuffer<uint> referenceVisible : register(u6);

// Exercise the production exact predicate without sharing the broad phase or its traversal.
[numthreads(64, 1, 1)] void ReferenceVisibility(uint3 id : SV_DispatchThreadID) {
	if (id.x >= ClusterSize.x * ClusterSize.y * ClusterSize.z)
		return;

	ClusterAABB cluster = clusters[id.x];
	uint offset = id.x * (MAX_CLUSTER_LIGHTS + 1);
	uint count = 0;
	for (uint i = 0; i < min(LightCount, GROUP_SIZE) && count < MAX_CLUSTER_LIGHTS; ++i) {
		Light light = lights[i];
		float radiusSquared = light.radius * light.radius;
		bool visible = LightIntersectsCluster(FrameBuffer::WorldToView(light.positionWS[0].xyz, true, 0), radiusSquared, cluster);
#if defined(VR)
		visible = visible || LightIntersectsCluster(FrameBuffer::WorldToView(light.positionWS[1].xyz, true, 1), radiusSquared, cluster);
#endif
		if (visible)
			referenceVisible[offset + 1 + count++] = i;
	}
	referenceVisible[offset] = count;
}
