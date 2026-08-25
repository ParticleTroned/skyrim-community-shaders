#ifndef __VR_DEPENDENCY_HLSL__
#define __VR_DEPENDENCY_HLSL__
#ifdef VR

// First person model depth threshold for VR occlusion logic
#	ifndef VR_FP_Z
#		define VR_FP_Z 18.0
#	endif

#	if defined(VSHADER)
#		include "Common/Math.hlsli"
#	endif  // VSHADER

#	if (!defined(COMPUTESHADER) && !defined(CSHADER)) || defined(FRAMEBUFFER)
#		include "Common/FrameBuffer.hlsli"
#	endif
cbuffer VRValues : register(b13)
{
	float AlphaTestRefRS : packoffset(c0);
	float StereoEnabled : packoffset(c0.y);
	float2 EyeOffsetScale : packoffset(c0.z);
	float4 EyeClipEdge[2] : packoffset(c1);
}
#endif

namespace Stereo
{
	/**
	Converts to the eye specific uv [0,1].
	In VR, texture buffers include the left and right eye in the same buffer. Flat
	only has a single camera for the entire width. This means the x value [0, .5]
	represents the left eye, and the x value (.5, 1] are the right eye. This returns
	the adjusted value
	@param uv - uv coords [0,1] to be encoded for VR
	@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
	@param a_invertY Whether to invert the Y direction
	@returns uv with x coords adjusted for the VR texture buffer
	*/
	float2 ConvertToStereoUV(float2 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
#ifdef VR
		// convert [0,1] to eye specific [0,.5] and [.5, 1] dependent on a_eyeIndex
		uv.x = saturate(uv.x);
		uv.x = (uv.x + (float)a_eyeIndex) / 2;
		if (a_invertY)
			uv.y = 1 - uv.y;
#endif
		return uv;
	}

	float3 ConvertToStereoUV(float3 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
		uv.xy = ConvertToStereoUV(uv.xy, a_eyeIndex, a_invertY);
		return uv;
	}

	float4 ConvertToStereoUV(float4 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
		uv.xy = ConvertToStereoUV(uv.xy, a_eyeIndex, a_invertY);
		return uv;
	}

	/**
	Converts from eye specific uv to general uv [0,1].
	In VR, texture buffers include the left and right eye in the same buffer.
	This means the x value [0, .5] represents the left eye, and the x value (.5, 1] are the right eye.
	This returns the adjusted value
	@param uv - eye specific uv coords [0,1]; if uv.x < 0.5, it's a left eye; otherwise right
	@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
	@param a_invertY Whether to invert the Y direction
	@returns uv with x coords adjusted to full range for either left or right eye
	*/
	float2 ConvertFromStereoUV(float2 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
#ifdef VR
		// convert [0,.5] to [0, 1] and [.5, 1] to [0,1]
		uv.x = 2 * uv.x - (float)a_eyeIndex;
		if (a_invertY)
			uv.y = 1 - uv.y;
#endif
		return uv;
	}

	float3 ConvertFromStereoUV(float3 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
		uv.xy = ConvertFromStereoUV(uv.xy, a_eyeIndex, a_invertY);
		return uv;
	}

	float4 ConvertFromStereoUV(float4 uv, uint a_eyeIndex, uint a_invertY = 0)
	{
		uv.xy = ConvertFromStereoUV(uv.xy, a_eyeIndex, a_invertY);
		return uv;
	}

	/**
	Converts to the eye specific screenposition [0,Resolution].
	In VR, texture buffers include the left and right eye in the same buffer. Flat only has a single camera for the entire width.
	This means the x value [0, resx/2] represents the left eye, and the x value (resx/2, x] are the right eye.
	This returns the adjusted value
	@param screenPosition - Screenposition coords ([0,resx], [0,resy]) to be encoded for VR
	@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
	@param a_resolution The resolution of the screen
	@returns screenPosition with x coords adjusted for the VR texture buffer
	*/
	float2 ConvertToStereoSP(float2 screenPosition, uint a_eyeIndex, float2 a_resolution)
	{
		screenPosition.x /= a_resolution.x;
		float2 stereoUV = ConvertToStereoUV(screenPosition, a_eyeIndex);
		return stereoUV * a_resolution;
	}

	float3 ConvertToStereoSP(float3 screenPosition, uint a_eyeIndex, float2 a_resolution)
	{
		float2 xy = screenPosition.xy / a_resolution;
		xy = ConvertToStereoUV(xy, a_eyeIndex);
		return float3(xy * a_resolution, screenPosition.z);
	}

	float4 ConvertToStereoSP(float4 screenPosition, uint a_eyeIndex, float2 a_resolution)
	{
		float2 xy = screenPosition.xy / a_resolution;
		xy = ConvertToStereoUV(xy, a_eyeIndex);
		return float4(xy * a_resolution, screenPosition.zw);
	}

	/**
	Gets the eyeIndex for Compute Shaders
	@param texCoord Texcoord on the screen [0,1]
	@returns eyeIndex (0 left, 1 right)
	*/
	uint GetEyeIndexFromTexCoord(float2 texCoord)
	{
#ifdef VR
		return (texCoord.x >= 0.5) ? 1 : 0;
#endif  // VR
		return 0;
	}

	/**
	* @brief Returns an eye-stable pixel coordinate for screen-space noise.
	*
	* Raw packed stereo pixels hash different values in each eye. Mapping to
	* the eye-local mono grid keeps stochastic shader work stereo-stable while
	* flat builds return the input coordinate unchanged.
	*/
	float2 EyeStableNoiseCoord(float2 sbsPixel, float2 bufferDim)
	{
#ifdef VR
		float2 stereoUV = sbsPixel / bufferDim;
		uint eyeIndex = GetEyeIndexFromTexCoord(stereoUV);
		float2 monoUV = ConvertFromStereoUV(stereoUV, eyeIndex);
		return monoUV * float2(bufferDim.x * 0.5, bufferDim.y);
#else
		return sbsPixel;
#endif
	}

	/**
	* @brief Applies motion velocity to UV coordinates and reports whether the previous-frame mono UV went out of bounds.
	*
	* The returned UV is converted back to stereo space in VR so history reprojection stays inside the current eye.
	* In flat mode this behaves like a normal `uv + velocity` reprojection.
	*/
	float2 ApplyVelocityToUV(float2 uv, float2 velocity, out bool isOutOfBounds)
	{
		uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
		float2 prevUVMono = Stereo::ConvertFromStereoUV(uv, eyeIndex) + velocity;
		float2 clampedMono = prevUVMono;

#ifdef VR
		// Reject the left edge (mono.x <= 0) too, not clamp-and-sample: clamping smears a
		// stretched history column at each eye's left/centre-seam edge on fast head turns.
		isOutOfBounds = (prevUVMono.x >= 1.0) || (prevUVMono.x <= 0.0) || (prevUVMono.y <= 0.0) || (prevUVMono.y >= 1.0);
		clampedMono.x = saturate(prevUVMono.x);
#else
		isOutOfBounds = any(prevUVMono >= 1.0) || any(prevUVMono <= 0.0);
#endif

		return Stereo::ConvertToStereoUV(clampedMono, eyeIndex);
	}

	/**
	* @brief Converts UV coordinates from the range [0, 1] to normalized screen space [-1, 1].
	*
	* This function takes texture coordinates and transforms them into a normalized
	* coordinate system centered at the origin. This is useful for various graphical
	* calculations, especially in shaders that require symmetry around the center.
	*
	* @param uv The input UV coordinates in the range [0, 1].
	* @return float2 Normalized screen space coordinates in the range [-1, 1].
	*/
	float2 ConvertUVToNormalizedScreenSpace(float2 uv)
	{
		float2 normalizedCoord;
		normalizedCoord.x = 2.0 * (-0.5 + abs(2.0 * (uv.x - 0.5)));  // Convert UV.x
		normalizedCoord.y = 2.0 * uv.y - 1.0;                        // Convert UV.y
		return normalizedCoord;
	}

	/**
	* @brief Clamps a stereo UV coordinate to the eye-local X range of the packed stereo buffer.
	*
	* Prevents cross-neighbor UV samples from crossing the x=0.5 seam into the other eye's
	* region of the side-by-side stereo texture. Y is not clamped; sampler address modes
	* handle vertical out-of-bounds. In flat builds there is no seam, so X is clamped to
	* the full [0,1] range.
	*
	* @param[in] uv        Stereo UV coordinate to clamp.
	* @param[in] eyeIndex  Eye index (0 = left [0, 0.5], 1 = right [0.5, 1]).
	* @return UV with x restricted to eyeIndex's half of the stereo buffer.
	*/
	float2 ClampToEyeUV(float2 uv, uint eyeIndex)
	{
#ifdef VR
		uv.x = clamp(uv.x, eyeIndex == 0 ? 0.0f : 0.5f, eyeIndex == 0 ? 0.5f : 1.0f);
#else
		uv.x = saturate(uv.x);
#endif
		return uv;
	}

	/**
	* @brief Clamps a pixel coordinate to the eye-local X bounds of the packed stereo buffer.
	*
	* Prevents cross-neighbor pixel reads from crossing the half-width seam into the
	* other eye's region of the side-by-side stereo texture.
	*
	* @param[in] px        Pixel coordinate to clamp.
	* @param[in] eyeIndex  Eye index (0 = left, 1 = right).
	* @param[in] frameDim  Full stereo buffer dimensions (width covers both eyes).
	* @return Clamped pixel coordinate, restricted to eyeIndex's half of the buffer.
	*/
	int2 ClampToEyeBounds(int2 px, uint eyeIndex, float2 frameDim)
	{
		int halfWidth = (int)((uint)frameDim.x >> 1);
		px.x = clamp(px.x, eyeIndex == 0 ? 0 : halfWidth, eyeIndex == 0 ? (halfWidth - 1) : ((int)frameDim.x - 1));
		px.y = clamp(px.y, 0, (int)frameDim.y - 1);
		return px;
	}

#if defined(PSHADER) || defined(FRAMEBUFFER)
	// These functions require the framebuffer which is typically provided with the PSHADER
	/**
	Gets the eyeIndex for PSHADER
	@returns eyeIndex (0 left, 1 right)
	*/
	uint GetEyeIndexPS(float4 position, float4 offset = 0.0.xxxx)
	{
#	if !defined(VR)
		uint eyeIndex = 0;
#	else
		float4 stereoUV;
		stereoUV.xy = position.xy * offset.xy + offset.zw;
		stereoUV.x = FrameBuffer::DynamicResolutionParams2.x * stereoUV.x;
		stereoUV.x = (stereoUV.x >= 0.5);
		uint eyeIndex = (uint)(((int)((uint)StereoEnabled)) * (int)stereoUV.x);
#	endif
		return eyeIndex;
	}

#endif      // PSHADER

#ifdef VSHADER
	struct VR_OUTPUT
	{
		float4 VRPosition;
		float ClipDistance;
		float CullDistance;
	};

	/**
	Gets the eyeIndex for VSHADER
	@returns eyeIndex (0 left, 1 right)
	*/
	uint GetEyeIndexVS(uint instanceID = 0)
	{
#	ifdef VR
		return StereoEnabled * (instanceID & 1);
#	endif  // VR
		return 0;
	}

	/**
	Gets VR Output
	@param clipPos clipPosition. Typically the VSHADER position at SV_POSITION0
	@param a_eyeIndex The eyeIndex; 0 is left, 1 is right
	@returns VR_OUTPUT with VR values
	*/
	VR_OUTPUT GetVRVSOutput(float4 clipPos, uint a_eyeIndex = 0)
	{
		VR_OUTPUT vsout = {
			0.0.xxxx,  // VRPosition
			0.0f,      // ClipDistance
			0.0f       // CullDistance
		};

#	ifdef VR
		bool isStereoEnabled = (StereoEnabled != 0);
		float2 clipEdges;

		if (isStereoEnabled) {
			clipEdges.x = dot(clipPos, EyeClipEdge[a_eyeIndex]);
			clipEdges.y = clipEdges.x;  // Both use the same calculation
		} else {
			clipEdges = float2(1.0f, 1.0f);
		}

		float stereoAdjustment = 2.0f - StereoEnabled;
		float eyeOffset = dot(EyeOffsetScale, Math::IdentityMatrix[a_eyeIndex].xy);

		float xPositionOffset = eyeOffset * clipPos.w * (isStereoEnabled ? 1.0f : 0.0f);
		float xPositionBase = stereoAdjustment * clipPos.x;

		vsout.VRPosition.x = xPositionBase * 0.5f + xPositionOffset;
		vsout.VRPosition.y = clipPos.y;
		vsout.VRPosition.z = clipPos.z;
		vsout.VRPosition.w = clipPos.w;

		vsout.ClipDistance = clipEdges.y;
		vsout.CullDistance = clipEdges.x;
#	endif  // VR
		return vsout;
	}
#endif

}
#endif  //__VR_DEPENDENCY_HLSL__
