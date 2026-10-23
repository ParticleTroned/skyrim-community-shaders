struct PackedCropTile
{
	uint4 source;
	uint4 model;
	uint4 packed;
};

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
	uint PackedCount;
	uint PackedPadding;
	uint2 UnpackedSize;
	PackedCropTile PackedTiles[4];
	uint SelectionMode;
	uint OutputStorageFormat;
	uint SelectedOutput;
	uint ReconstructionFilter;
	float4 SelectionBounds;
};

uint FindPackedCrop(float2 pixel, bool sourceCoordinates)
{
	[loop] for (uint i = 0u; i < PackedCount; ++i)
	{
		uint4 rect = sourceCoordinates ? PackedTiles[i].source : PackedTiles[i].packed;
		if (all(pixel >= float2(rect.xy)) && all(pixel < float2(rect.xy + rect.zw)))
			return i;
	}
	return PackedCount;
}
