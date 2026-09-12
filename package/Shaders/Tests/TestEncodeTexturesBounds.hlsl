// Tests the source-sampling bounds used by the upscaling input encoder.
#include "/Shaders/Upscaling/EncodeTexturesBounds.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags upscaling, vr, stereo, motion-vectors
/// Packed-stereo samples never cross from the left eye into the right eye.
[numthreads(1, 1, 1)] void TestEncodeTexturesLeftEyeBounds() {
	int2 textureDim = int2(16, 8);
	float2 leftEyeBounds = float2(0, 8);

	ASSERT(IsTrue, IsEncodeTextureSourceSampleInBounds(int2(0, 0), textureDim, leftEyeBounds));
	ASSERT(IsTrue, IsEncodeTextureSourceSampleInBounds(int2(7, 7), textureDim, leftEyeBounds));
	ASSERT(IsFalse, IsEncodeTextureSourceSampleInBounds(int2(8, 4), textureDim, leftEyeBounds));
	ASSERT(IsFalse, IsEncodeTextureSourceSampleInBounds(int2(9, 4), textureDim, leftEyeBounds));
}

	/// @tags upscaling, vr, stereo, motion-vectors
	/// Packed-stereo samples never cross from the right eye into the left eye.
	[numthreads(1, 1, 1)] void TestEncodeTexturesRightEyeBounds()
{
	int2 textureDim = int2(16, 8);
	float2 rightEyeBounds = float2(8, 16);

	ASSERT(IsTrue, IsEncodeTextureSourceSampleInBounds(int2(8, 0), textureDim, rightEyeBounds));
	ASSERT(IsTrue, IsEncodeTextureSourceSampleInBounds(int2(15, 7), textureDim, rightEyeBounds));
	ASSERT(IsFalse, IsEncodeTextureSourceSampleInBounds(int2(7, 4), textureDim, rightEyeBounds));
	ASSERT(IsFalse, IsEncodeTextureSourceSampleInBounds(int2(6, 4), textureDim, rightEyeBounds));
}

/// @tags upscaling, vr, stereo, foveated, motion-vectors
/// A left-eye foveated crop keeps its full-eye 5x5 source footprint.
[numthreads(1, 1, 1)] void TestEncodeTexturesLeftFoveatedCropUsesFullEyeBounds() {
	int2 textureDim = int2(16, 8);
	float2 leftEyeBounds = float2(0, 8);
	int4 dispatchCrop = int4(6, 2, 8, 6);
	bool acceptedOutsideCropX = false;
	bool acceptedOutsideCropY = false;
	bool rejectedOppositeEye = false;

	[unroll] for (int centerY = dispatchCrop.y; centerY < dispatchCrop.w; centerY += 3)
	{
		[unroll] for (int centerX = dispatchCrop.x; centerX < dispatchCrop.z; ++centerX)
		{
			[unroll] for (int offsetY = -2; offsetY <= 2; ++offsetY)
			{
				[unroll] for (int offsetX = -2; offsetX <= 2; ++offsetX)
				{
					int2 samplePos = int2(centerX + offsetX, centerY + offsetY);
					bool accepted = IsEncodeTextureSourceSampleInBounds(samplePos, textureDim, leftEyeBounds);
					bool expected = samplePos.x >= 0 && samplePos.x < 8 && samplePos.y >= 0 && samplePos.y < textureDim.y;
					bool insideOppositeEye = samplePos.x >= 8 && samplePos.x < 16 &&
					                         samplePos.y >= 0 && samplePos.y < textureDim.y;

					ASSERT(IsTrue, accepted == expected);
					acceptedOutsideCropX = acceptedOutsideCropX ||
					                       (accepted && (samplePos.x < dispatchCrop.x || samplePos.x >= dispatchCrop.z));
					acceptedOutsideCropY = acceptedOutsideCropY ||
					                       (accepted && (samplePos.y < dispatchCrop.y || samplePos.y >= dispatchCrop.w));
					if (insideOppositeEye) {
						rejectedOppositeEye = rejectedOppositeEye || !accepted;
						ASSERT(IsFalse, accepted);
					}
				}
			}
		}
	}

	ASSERT(IsTrue, acceptedOutsideCropX);
	ASSERT(IsTrue, acceptedOutsideCropY);
	ASSERT(IsTrue, rejectedOppositeEye);
}

	/// @tags upscaling, vr, foveated, motion-vectors
	/// A right-eye foveated crop keeps its full-eye 5x5 source footprint.
	[numthreads(1, 1, 1)] void TestEncodeTexturesRightFoveatedCropUsesFullEyeBounds()
{
	int2 textureDim = int2(16, 8);
	float2 rightEyeBounds = float2(8, 16);
	int4 dispatchCrop = int4(8, 2, 10, 6);
	bool acceptedOutsideCropX = false;
	bool acceptedOutsideCropY = false;
	bool rejectedOppositeEye = false;

	[unroll] for (int centerY = dispatchCrop.y; centerY < dispatchCrop.w; centerY += 3)
	{
		[unroll] for (int centerX = dispatchCrop.x; centerX < dispatchCrop.z; ++centerX)
		{
			[unroll] for (int offsetY = -2; offsetY <= 2; ++offsetY)
			{
				[unroll] for (int offsetX = -2; offsetX <= 2; ++offsetX)
				{
					int2 samplePos = int2(centerX + offsetX, centerY + offsetY);
					bool accepted = IsEncodeTextureSourceSampleInBounds(samplePos, textureDim, rightEyeBounds);
					bool expected = samplePos.x >= 8 && samplePos.x < 16 && samplePos.y >= 0 && samplePos.y < textureDim.y;
					bool insideOppositeEye = samplePos.x >= 0 && samplePos.x < 8 &&
					                         samplePos.y >= 0 && samplePos.y < textureDim.y;

					ASSERT(IsTrue, accepted == expected);
					acceptedOutsideCropX = acceptedOutsideCropX ||
					                       (accepted && (samplePos.x < dispatchCrop.x || samplePos.x >= dispatchCrop.z));
					acceptedOutsideCropY = acceptedOutsideCropY ||
					                       (accepted && (samplePos.y < dispatchCrop.y || samplePos.y >= dispatchCrop.w));
					if (insideOppositeEye) {
						rejectedOppositeEye = rejectedOppositeEye || !accepted;
						ASSERT(IsFalse, accepted);
					}
				}
			}
		}
	}

	ASSERT(IsTrue, acceptedOutsideCropX);
	ASSERT(IsTrue, acceptedOutsideCropY);
	ASSERT(IsTrue, rejectedOppositeEye);
}

/// @tags upscaling, bounds, motion-vectors
/// Source-eye bounds compose with the physical texture bounds.
[numthreads(1, 1, 1)] void TestEncodeTexturesRejectsTextureEdges() {
	int2 textureDim = int2(16, 8);
	float2 fullWidthBounds = float2(0, 16);

	ASSERT(IsFalse, IsEncodeTextureSourceSampleInBounds(int2(-1, 4), textureDim, fullWidthBounds));
	ASSERT(IsFalse, IsEncodeTextureSourceSampleInBounds(int2(16, 4), textureDim, fullWidthBounds));
	ASSERT(IsFalse, IsEncodeTextureSourceSampleInBounds(int2(4, -1), textureDim, fullWidthBounds));
	ASSERT(IsFalse, IsEncodeTextureSourceSampleInBounds(int2(4, 8), textureDim, fullWidthBounds));
	ASSERT(IsTrue, IsEncodeTextureSourceSampleInBounds(int2(15, 7), textureDim, fullWidthBounds));
}
