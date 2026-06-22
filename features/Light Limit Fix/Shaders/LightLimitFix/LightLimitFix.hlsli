
namespace LightLimitFix
{

#include "LightLimitFix/Common.hlsli"

	struct StrictLightDataType
	{
		uint NumStrictLights;
		int RoomIndex;
		uint ShadowBitMask;
		uint pad0;
		Light StrictLights[15];
	};
}

cbuffer StrictLightDataBuf : register(b3) { LightLimitFix::StrictLightDataType sld; };

namespace LightLimitFix
{
	StructuredBuffer<Light> lights : register(t35);
	StructuredBuffer<uint> lightList : register(t36);       //MAX_CLUSTER_LIGHTS * 16^3
	StructuredBuffer<LightGrid> lightGrid : register(t37);  //16^3

	bool GetClusterIndex(in float2 uv, in float z, inout uint clusterIndex)
	{
		const uint3 clusterSize = fd.lightLimitFixSettings.ClusterSize.xyz;

		if (!fb.FrameParams.y)  // Fix first person lights
			uv = 0.5;

		z = max(z, sd.CameraData.y);

		uint clusterZ = log(z / sd.CameraData.y) * clusterSize.z / log(sd.CameraData.x / sd.CameraData.y);
		uint3 cluster = uint3(uint2(uv * clusterSize.xy), clusterZ);

		// Bounds validation to prevent out-of-range cluster indices
		if (any(cluster >= clusterSize))
			return false;

		clusterIndex = cluster.x + (clusterSize.x * cluster.y) + (clusterSize.x * clusterSize.y * cluster.z);
		return true;
	}

	bool IsLightIgnored(Light light)
	{
		if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
			return !(sld.ShadowBitMask & (1u << light.shadowLightIndex));
		}

		bool lightIgnored = false;
		if ((light.lightFlags & LightFlags::PortalStrict) && sld.RoomIndex >= 0) {
			lightIgnored = true;
			int roomIndex = sld.RoomIndex;
			[unroll] for (int flagsIndex = 0; flagsIndex < 4; ++flagsIndex)
			{
				if (roomIndex < 32) {
					if (((light.roomFlags[flagsIndex] >> roomIndex) & 1) == 1) {
						lightIgnored = false;
					}
					break;
				}
				roomIndex -= 32;
			}
		}
		return lightIgnored;
	}
}
