
struct PerGeometry
{
	float4 VPOSOffset;
	float4 ShadowSampleParam;    // fPoissonRadiusScale / iShadowMapResolution in z and w
	float4 EndSplitDistances;    // cascade end distances int xyz, cascade count int z
	float4 StartSplitDistances;  // cascade start ditances int xyz, 4 int z
	float4 FocusShadowFadeParam;
	float4 DebugColor;
	float4 PropertyColor;
	float4 AlphaTestRef;
	float4 ShadowLightParam;  // Falloff in x, ShadowDistance squared in z
	float4x3 FocusShadowMapProj[4];
	// Since PerGeometry is passed between c++ and hlsl, can't have different defines due to strong typing
	float4x3 ShadowMapProj[2][3];
	float4x4 CameraViewProjInverse[2];
};

cbuffer PerFrame : register(b0)
{
	float4 VPOSOffset : packoffset(c0);
	float4 ShadowSampleParam : packoffset(c1);    // fPoissonRadiusScale / iShadowMapResolution in z and w
	float4 EndSplitDistances : packoffset(c2);    // cascade end distances int xyz, cascade count int z
	float4 StartSplitDistances : packoffset(c3);  // cascade start ditances int xyz, 4 int z
	float4 FocusShadowFadeParam : packoffset(c4);
}

cbuffer PerFrame2 : register(b1)
{
	row_major float4x4 CameraView : packoffset(c0);
	row_major float4x4 CameraProj : packoffset(c4);
	row_major float4x4 CameraViewProj : packoffset(c8);
	row_major float4x4 CameraViewProjUnjittered : packoffset(c12);
	row_major float4x4 CameraPreviousViewProjUnjittered : packoffset(c16);
	row_major float4x4 CameraProjUnjittered : packoffset(c20);
	row_major float4x4 CameraProjUnjitteredInverse : packoffset(c24);
	row_major float4x4 CameraViewInverse : packoffset(c28);
	row_major float4x4 CameraViewProjInverse : packoffset(c32);
	row_major float4x4 CameraProjInverse : packoffset(c36);
	float4 CameraPosAdjust : packoffset(c40);
	float4 CameraPreviousPosAdjust : packoffset(c41);  // fDRClampOffset in w
	float4 FrameParams : packoffset(c42);                 // inverse fGamma in x, some flags in yzw
	float4 DynamicResolutionParams1 : packoffset(c43);    // fDynamicResolutionWidthRatio in x,
														  // fDynamicResolutionHeightRatio in y,
														  // fDynamicResolutionPreviousWidthRatio in z,
														  // fDynamicResolutionPreviousHeightRatio in w
	float4 DynamicResolutionParams2 : packoffset(c44);    // inverse fDynamicResolutionWidthRatio in x, inverse
														  // fDynamicResolutionHeightRatio in y,
														  // fDynamicResolutionWidthRatio - fDRClampOffset in z,
														  // fDynamicResolutionPreviousWidthRatio - fDRClampOffset in w
}

// copied from UtilShader(b2). Assume RENDER_SHADOWMASK
cbuffer PerFrame3 : register(b2)
{
	float4 DebugColor : packoffset(c0);
	float4 PropertyColor : packoffset(c1);
	float4 AlphaTestRef : packoffset(c2);
	float4 ShadowLightParam : packoffset(c3);  // Falloff in x, ShadowDistance squared in z
	float4x3 FocusShadowMapProj[4] : packoffset(c4);
	float4x3 ShadowMapProj[1][3] : packoffset(c16);  // 16, 19, 22
}

RWStructuredBuffer<PerGeometry> ShadowData : register(u0);

[numthreads(1, 1, 1)] void main() {
	PerGeometry perGeometry;
	perGeometry.DebugColor = DebugColor;
	perGeometry.PropertyColor = PropertyColor;
	perGeometry.AlphaTestRef = AlphaTestRef;
	perGeometry.ShadowLightParam = ShadowLightParam;
	perGeometry.FocusShadowMapProj = FocusShadowMapProj;
	perGeometry.ShadowMapProj[0] = ShadowMapProj[0];

	perGeometry.CameraViewProjInverse[0] = CameraViewProjInverse;
	perGeometry.ShadowMapProj[1] = ShadowMapProj[0];

	perGeometry.CameraViewProjInverse[1] = CameraViewProjInverse;

	perGeometry.VPOSOffset = VPOSOffset;
	perGeometry.ShadowSampleParam = ShadowSampleParam;
	perGeometry.EndSplitDistances = EndSplitDistances;
	perGeometry.StartSplitDistances = StartSplitDistances;
	perGeometry.FocusShadowFadeParam = FocusShadowFadeParam;

	ShadowData[0] = perGeometry;
}
