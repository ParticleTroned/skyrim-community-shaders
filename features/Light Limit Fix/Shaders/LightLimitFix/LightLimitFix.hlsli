namespace LightLimitFix
{

#include "LightLimitFix/Common.hlsli"

	cbuffer StrictLightData : register(b3)
	{
		uint NumStrictLights;
		int RoomIndex;
		uint ShadowBitMask;
		uint pad0;
		Light StrictLights[15];
	};

	StructuredBuffer<Light> lights : register(t35);
	StructuredBuffer<uint> lightList : register(t36);       //MAX_CLUSTER_LIGHTS * 16^3
	StructuredBuffer<LightGrid> lightGrid : register(t37);  //16^3
#if defined(DEFERRED)
	StructuredBuffer<uint> contactShadowList : register(t38);
	StructuredBuffer<LightGrid> contactShadowGrid : register(t39);
#endif

	bool GetClusterIndex(in float2 uv, in float z, inout uint clusterIndex)
	{
		const uint3 clusterSize = SharedData::lightLimitFixSettings.ClusterSize.xyz;

		if (!FrameBuffer::FrameParams.y)  // Fix first person lights
			uv = 0.5;

		z = max(z, SharedData::CameraData.y);
		uint clusterZ = log(z / SharedData::CameraData.y) * clusterSize.z / log(SharedData::CameraData.x / SharedData::CameraData.y);
		uint3 cluster = uint3(uint2(uv * clusterSize.xy), clusterZ);

		// Bounds validation to prevent out-of-range cluster indices
		if (any(cluster >= clusterSize))
			return false;

		clusterIndex = cluster.x + (clusterSize.x * cluster.y) + (clusterSize.x * clusterSize.y * cluster.z);
		return true;
	}

#if defined(DEFERRED)
	bool IsSaturated(float value)
	{
		return value == saturate(value);
	}

	bool IsSaturated(float2 value)
	{
		return IsSaturated(value.x) && IsSaturated(value.y);
	}

	bool IsContactShadowCandidate(uint lightIndex, uint contactShadowOffset, uint contactShadowCount)
	{
		contactShadowCount = min(contactShadowCount, MAX_CONTACT_SHADOW_LIGHTS);

		[unroll] for (uint i = 0; i < MAX_CONTACT_SHADOW_LIGHTS; ++i) {
			if (i >= contactShadowCount)
				break;

			if (contactShadowList[contactShadowOffset + i] == lightIndex)
				return true;
		}

		return false;
	}

	bool GetContactShadowClusterRange(uint clusterIndex, out uint contactShadowOffset, out uint contactShadowCount)
	{
		contactShadowOffset = 0;
		contactShadowCount = 0;

		const uint3 clusterSize = SharedData::lightLimitFixSettings.ClusterSize.xyz;
		const uint clusterCount = clusterSize.x * clusterSize.y * clusterSize.z;
		if (clusterCount == 0)
			return false;
		if (clusterIndex >= clusterCount)
			return false;

		const uint maxEntries = clusterCount * MAX_CONTACT_SHADOW_LIGHTS;
		LightGrid cachedLights = contactShadowGrid[clusterIndex];
		contactShadowCount = min(cachedLights.lightCount, MAX_CONTACT_SHADOW_LIGHTS);
		if (contactShadowCount == 0)
			return false;

		const uint offset = cachedLights.offset;
		if (offset >= maxEntries)
			return false;

		if (offset > maxEntries - contactShadowCount)
			return false;

		contactShadowOffset = offset;
		return true;
	}

	uint GetContactShadowQuality()
	{
		return min(ContactShadowParams::Quality(SharedData::lightLimitFixSettings.ContactShadowParams), 2u);
	}

	bool CanUseContactShadows(Light light, bool isParticle)
	{
		const uint flags = SharedData::lightLimitFixSettings.ContactShadowFlags;
		if (isParticle)
			return (flags & ContactShadowFlags::Particle) != 0;

		return (flags & ContactShadowFlags::Point) != 0 && (light.lightFlags & LightFlags::Simple) == 0;
	}

	uint GetContactShadowMaxSamples(bool isParticle, uint quality)
	{
		return isParticle ? (1u + quality) : (2u + quality * 2u);
	}

	uint GetStrictContactShadowBudget()
	{
		return min(ContactShadowParams::StrictBudget(SharedData::lightLimitFixSettings.ContactShadowParams), MAX_CONTACT_SHADOW_LIGHTS);
	}

	float GetContactShadowFadeDistance(bool isParticle)
	{
		return isParticle ? 512.0 : 1024.0;
	}

	float2 GetContactShadowScreenDim()
	{
		return SharedData::BufferDim.xy;
	}

	float2 GetContactShadowNoiseCoord(float2 screenPosition, float2 screenUV, float2 screenDim)
	{
		return screenPosition;
	}

	float ContactShadows(float3 viewPosition, float2 screenUV, float3 lightPositionWS, float screenNoise, float2 screenDim, bool isParticle, uint eyeIndex)
	{
		const float fadeDistance = GetContactShadowFadeDistance(isParticle);
		const float viewDistance = abs(viewPosition.z);
		if (viewDistance >= fadeDistance)
			return 1.0;

		float3 lightPositionVS = FrameBuffer::WorldToView(lightPositionWS, true, eyeIndex);
		float3 rayVS = lightPositionVS - viewPosition;
		float rayLength = length(rayVS);
		if (rayLength <= 1e-3)
			return 1.0;

		float3 rayDirVS = rayVS / rayLength;
		uint quality = GetContactShadowQuality();
		float maxTraceDistance = isParticle ? (4.0 + 2.0 * quality) : (7.0 + 2.5 * quality);
		float traceDistance = min(rayLength, maxTraceDistance);

		float startOffset = min(lerp(0.35, 0.85, screenNoise), traceDistance * 0.5);
		float3 startVS = viewPosition + rayDirVS * startOffset;
		float3 endVS = viewPosition + rayDirVS * traceDistance;
		if (endVS.z <= SharedData::CameraData.y)
			return 1.0;

		float2 endUV = FrameBuffer::ViewToUV(endVS, true, eyeIndex);

		float2 rayPixels = (endUV - screenUV) * screenDim;
		float rayPixelsLength = length(rayPixels);
		uint sampleCount = min(GetContactShadowMaxSamples(isParticle, quality), max(1u, (uint)ceil(rayPixelsLength / 18.0)));

		float2 depthDeltaMult = isParticle ? float2(0.16, 0.04) : float2(0.20, 0.05);
		float minValidSceneDepth = max(SharedData::CameraData.y + 1.0, 16.5);
		float contactShadow = 0.0;

		[unroll] for (uint i = 0; i < 6; ++i) {
			if (i >= sampleCount)
				break;

			float stepT = (i + 1.0 + screenNoise * 0.5) / (sampleCount + 1.0);
			float3 raySampleVS = lerp(startVS, endVS, stepT);
			float2 rayUV = lerp(screenUV, endUV, stepT);

			if (!IsSaturated(rayUV))
				break;

			float sceneDepth = SharedData::GetScreenDepth(rayUV, eyeIndex);
			float depthDelta = raySampleVS.z - sceneDepth;
			if (sceneDepth > minValidSceneDepth) {
				contactShadow = max(contactShadow, saturate(depthDelta * depthDeltaMult.x) - saturate(depthDelta * depthDeltaMult.y));
			}

			if (contactShadow >= 1.0)
				break;
		}

		float edgeFade = saturate(min(min(screenUV.x, 1.0 - screenUV.x), min(screenUV.y, 1.0 - screenUV.y)) * 32.0);
		float distanceFade = saturate(1.0 - viewDistance / fadeDistance);
		float intensity = isParticle ? 0.65 : 1.0;

		return 1.0 - saturate(contactShadow * edgeFade * distanceFade * intensity);
	}
#endif

	bool IsLightIgnored(Light light)
	{
		if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
			return !(ShadowBitMask & (1 << light.shadowLightIndex));
		}

		bool lightIgnored = false;
		if ((light.lightFlags & LightFlags::PortalStrict) && RoomIndex >= 0) {
			lightIgnored = true;
			int roomIndex = RoomIndex;
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
