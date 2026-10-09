cbuffer ModelResolutionConstants : register(b0)
{
	uint2 SourceSize;
	uint2 ModelSize;
	uint2 SourceRegionOffset;
	uint2 SourceRegionSize;
	uint2 ModelRegionOffset;
	uint2 ModelRegionSize;
	uint HasMask;
	uint OutputFormat;
	float2 MotionVectorScale;
	float2 FullEyeSize;
	float2 CropOrigin;
	float CentralScale;
	float CentralHorizontalScale;
	float CentralFeather;
	uint CentralActive;
	float2 CentralOffset;
	float2 FinalOutputSize;
};
