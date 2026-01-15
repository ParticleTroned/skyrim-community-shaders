#pragma once

struct TerrainBlending : Feature
{
public:
	virtual inline std::string GetName() override { return "Terrain Overlay VR"; }
	virtual inline std::string GetShortName() override { return "TerrainBlending"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_BLENDING"; }
	virtual std::string_view GetCategory() const override { return "Landscape & Textures"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Provides VR-friendly terrain overlay blending with edge-aware, angle-scaled fades to soften object-ground seams without affecting shadow stability.",
			{ "Terrain-to-object overlay blending tuned for VR",
				"Edge-only blending using depth-mask discontinuities",
				"Angle-aware blend range/gain for steep seams",
				"Configurable blend shape and edge thresholds",
				"Isolated depth usage to avoid shadow swimming" }
		};
	}
	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	virtual void SetupResources() override;

	ID3D11VertexShader* GetTerrainVertexShader();
	ID3D11VertexShader* GetTerrainOffsetVertexShader();

	ID3D11VertexShader* terrainVertexShader = nullptr;
	ID3D11VertexShader* terrainOffsetVertexShader = nullptr;

	ID3D11ComputeShader* GetDepthBlendShader();

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;

	struct Settings
	{
		bool Enable = true;
		float BlendRange = 5.0f;
		uint BlendShapeMode = 2;
		uint BlendMode = 1;
		uint DitherMode = 2;
		float EdgeStart = 0.001f;
		float EdgeEnd = 0.055f;
		uint EdgeSlopeMode = 1;
		bool SkipEdgeSamplesWhenNoGap = true;
		float AngleStartDeg = 0.0f;
		float AngleEndDeg = 12.5f;
		float AngleRangeScale = 0.25f;
		float AngleGainScale = 3.0f;
		bool BypassAngleEdge = false;
		bool EnableReplayCulling = true;
		float ReplayCullDistance = 768.0f;
		float ReplayCullMinPixels = 0.0f;
	};

	struct alignas(16) PerFrame
	{
		float BlendRange;
		uint BlendShapeMode;
		uint BlendMode;
		uint DitherMode;
		float EdgeStart;
		float EdgeEnd;
		uint EdgeSlopeMode;
		uint SkipEdgeSamplesWhenNoGap;
		float AngleStartCos;
		float AngleEndCos;
		float AngleRangeScale;
		float AngleGainScale;
		uint BypassAngleEdge;
		uint pad0[3];
	};

	Settings settings;

	PerFrame GetCommonBufferData();
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	bool renderDepth = false;
	bool renderTerrainDepth = false;
	bool renderAltTerrain = false;
	bool inTBReplay = false;

	RE::NiPoint3 averageEyePosition;

	struct RenderPass
	{
		RE::BSRenderPass* a_pass;
		uint32_t a_technique;
		bool a_alphaTest;
		uint32_t a_renderFlags;
	};

	std::vector<RenderPass> renderPasses;
	std::vector<RenderPass> terrainRenderPasses;
	std::vector<D3D11_RECT> prevScissorRectsCache;
	std::vector<D3D11_RECT> scissorRectsCache;
	std::vector<D3D11_RECT> fullScissorRectsCache;
	std::vector<D3D11_VIEWPORT> viewportsCache;

	void TerrainShaderHacks();

	void ResetDepth();
	void ResetTerrainDepth();
	void BlendPrepassDepths();
	void ToggleDebugCapture();
	void DumpDebugStats();

	Texture2D* blendedDepthTexture = nullptr;
	Texture2D* blendedDepthTexture16 = nullptr;

	RE::BSGraphics::DepthStencilData terrainDepth;

	ID3D11DepthStencilState* terrainDepthStencilState = nullptr;
	ID3D11RasterizerState* terrainScissorState = nullptr;
	ID3D11RasterizerState* terrainScissorBaseState = nullptr;

	ID3D11ShaderResourceView* depthSRVBackup = nullptr;
	ID3D11ShaderResourceView* prepassSRVBackup = nullptr;

	ID3D11ComputeShader* depthBlendShader = nullptr;

	virtual void ClearShaderCache() override;

	void RenderTerrainBlendingPasses();

	struct Hooks
	{
		struct Main_RenderDepth
		{
			static void thunk(bool a1, bool a2);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSBatchRenderer__RenderPassImmediately
		{
			static void thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			// To know when we are rendering z-prepass depth vs shadows depth
			stl::write_thunk_call<Main_RenderDepth>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x395, 0x395, 0x2EE));

			// To manipulate the depth buffer write, depth testing, alpha blending
			stl::write_thunk_call<BSBatchRenderer__RenderPassImmediately>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));

			logger::info("[Terrain Overlay] Installed hooks");
		}
	};
	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };
};
