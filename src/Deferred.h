#pragma once

#include <DirectXMath.h>
#include "Buffer.h"

#define ALBEDO RE::RENDER_TARGETS::kINDIRECT
#define SPECULAR RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED
#define REFLECTANCE RE::RENDER_TARGETS::kRAWINDIRECT
#define NORMALROUGHNESS RE::RENDER_TARGETS::kRAWINDIRECT_DOWNSCALED
#define MASKS RE::RENDER_TARGETS::kRAWINDIRECT_PREVIOUS
#define MASKS2 RE::RENDER_TARGETS::kRAWINDIRECT_PREVIOUS_DOWNSCALED

class Deferred
{
public:
	struct alignas(16) DirectionalShadowLightData
	{
		float4x4 ShadowProj[2];
		float4x4 InvShadowProj[2];
		float2 EndSplitDistances;
		float2 StartSplitDistances;
	};
	STATIC_ASSERT_ALIGNAS_16(DirectionalShadowLightData);

	static Deferred* GetSingleton()
	{
		static Deferred singleton;
		return &singleton;
	}

	void SetupResources();
	void CopyShadowData();
	void CopyShadowLightData();
	void ReflectionsPrepasses();
	void EarlyPrepasses();
	void StartDeferred();
	void OverrideBlendStates();
	void ResetBlendStates();
	void DeferredPasses();
	void EndDeferred();

	void PrepassPasses();

	void ClearShaderCache();

	template <typename T>
	void SetShadowCascadeParameters(T& lightData, DirectionalShadowLightData& dd);

	ID3D11ComputeShader* GetComputeMainComposite();
	ID3D11ComputeShader* GetComputeMainCompositeInterior();
	ID3D11ComputeShader* GetComputeMainCompositeMetadata();
	ID3D11ComputeShader* GetComputeMainCompositeMetadataInterior();
	ID3D11PixelShader* GetPixelMainComposite();
	ID3D11PixelShader* GetPixelMainCompositeInterior();
	ID3D11VertexShader* GetPixelMainCompositeVS();
	Texture2D* EnsureDeferredCompositeColorCopy(ID3D11Texture2D* a_source, ID3D11ShaderResourceView* a_sourceSRV);

	ID3D11BlendState* deferredBlendStates[7][2][13][2];
	ID3D11BlendState* forwardBlendStates[7][2][13][2];

	RE::RENDER_TARGET forwardRenderTargets[4];

	ID3D11ComputeShader* mainCompositeCS = nullptr;
	ID3D11ComputeShader* mainCompositeInteriorCS = nullptr;
	ID3D11ComputeShader* mainCompositeMetadataCS = nullptr;
	ID3D11ComputeShader* mainCompositeMetadataInteriorCS = nullptr;
	ID3D11PixelShader* mainCompositePS = nullptr;
	ID3D11PixelShader* mainCompositeInteriorPS = nullptr;
	ID3D11VertexShader* mainCompositeVS = nullptr;
	ID3D11BlendState* compositeColorBlendState = nullptr;
	ID3D11DepthStencilState* compositeColorDepthStencilState = nullptr;
	ID3D11RasterizerState* compositeColorRasterizerState = nullptr;
	Buffer* directionalShadowLights = nullptr;
	Texture2D* deferredCompositeColorCopy = nullptr;

	bool deferredPass = false;

	ID3D11SamplerState* linearSampler = nullptr;
	ID3D11SamplerState* pointSampler = nullptr;

	struct alignas(16) PerGeometry
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
		DirectX::XMFLOAT4X3 FocusShadowMapProj[4];
		// Since PerGeometry is passed between c++ and hlsl, can't have different defines due to strong typing
		DirectX::XMFLOAT4X3 ShadowMapProj[2][3];
		DirectX::XMFLOAT4X3 CameraViewProjInverse[2];
	};
	STATIC_ASSERT_ALIGNAS_16(PerGeometry);

	ID3D11ComputeShader* copyShadowCS = nullptr;
	Buffer* perShadow = nullptr;
	ID3D11ShaderResourceView* shadowView = nullptr;

	struct Hooks
	{
		struct Main_RenderShadowMaps
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld
		{
			static void thunk(bool a1);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_Start
		{
			static void thunk(RE::BSBatchRenderer* This, uint32_t StartRange, uint32_t EndRanges, uint32_t RenderFlags, int GeometryGroup);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_BlendedDecals
		{
			static void thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSCubeMapCamera_RenderCubemap
		{
			static void thunk(RE::NiAVObject* camera, int a2, bool a3, bool a4, bool a5);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderFirstPersonView
		{
			static void thunk(bool a1, bool a2);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Renderer_ResetState
		{
			static void thunk(void* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x35, BSCubeMapCamera_RenderCubemap>(RE::VTABLE_BSCubeMapCamera[0]);

			stl::write_thunk_call<Main_RenderShadowMaps>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x2EC, 0x2EC));

			stl::write_thunk_call<Main_RenderWorld>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x831, 0x841));
			stl::write_thunk_call<Main_RenderWorld_Start>(REL::RelocationID(99938, 106583).address() + REL::Relocate(0x8E, 0x84));
			stl::write_thunk_call<Main_RenderWorld_BlendedDecals>(REL::RelocationID(99938, 106583).address() + REL::Relocate(0x319, 0x308));

			stl::write_thunk_call<Main_RenderFirstPersonView>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x944, 0x954));

			stl::detour_thunk<Renderer_ResetState>(REL::RelocationID(75570, 77371));

			logger::info("[Deferred] Installed hooks");
		}
	};
};
