#pragma once

#include "FurShells/InertiaSpring.h"
#include "FurShells/Interactors.h"
#include <array>
#include <atomic>
#include <cfloat>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

struct FurShells : Feature
{
	virtual inline std::string GetName() override { return "Fur Shells"; }
	virtual std::string GetDisplayName() override { return "Fur Shells"; }
	virtual inline std::string GetShortName() override { return "FurShells"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kCharacters; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { "Adds fluffy fur to clothing and creatures with compatible fur textures.",
			{ "Requires compatible fur textures from a mod",
				"Follows clothing and body movement",
				"Distant fur can use less detail to save performance",
				"Adjustable length, density and droop",
				"Optional wind, wet fur, touch and movement effects",
				"Separate controls for touch accuracy and movement style" } };
	}

	/** @brief Global fur settings, used for every shell texture that has no override. */
	struct Settings
	{
		bool Enabled = false;
		uint32_t ShellCount = 12;
		float Length = 1.2f;
		float BodyLength = 0.4f;
		float Droop = 0.15f;
		float RootThreshold = 0.0f;
		float TipThreshold = 1.0f;
		float RootDarkening = 0.6f;
		float ShellColor = 1.0f;
		float FadeStart = 700.0f;
		float FadeEnd = 2500.0f;
		bool HideCoveredFur = true;
		bool OverlayFur = true;
		float MinFurPixels = 0.0f;

		bool EnableWind = false;
		float WindStrength = 1.0f;
		bool EnableWetFur = false;
		float WetStrength = 1.0f;
		bool EnableTouch = false;
		float TouchStrength = 1.0f;
		bool EnableDentRecovery = false;
		bool FitHandCapsules = false;
		bool PreciseContactSelection = false;
		bool EnableHeldObjects = false;
		uint32_t ContactLimit = 8;
		float ContactRange = 800.0f;
		bool EnableInertia = false;
		float InertiaStrength = 1.0f;
		uint32_t InertiaPreset = 0;
		float InertiaFrequency = FurInertia::Tuning{}.frequencyHz;
		float InertiaGain = FurInertia::Tuning{}.gain;
		float InertiaLengthExponent = FurInertia::Tuning{}.lengthExponent;
		float InertiaDroopExponent = FurInertia::Tuning{}.droopExponent;
		float InertiaMaxBend = FurInertia::Tuning{}.maxBend;
		bool ShowTouchEdges = false;
		bool DistanceShellBudget = true;

	} settings;

	/** @brief Per-shell-texture values, loaded from the .json beside the texture. */
	struct ShellOverride
	{
		bool Enabled = true;
		uint32_t ShellCount = 12;
		float Length = 1.2f;
		float Droop = 0.15f;
		float RootThreshold = 0.0f;
		float TipThreshold = 1.0f;
		float RootDarkening = 0.6f;
		float ShellColor = 1.0f;
		float FadeStart = 700.0f;
		float FadeEnd = 2500.0f;
	};

	virtual bool SupportsVR() override { return true; }

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void SetupResources() override;
	void SetupRenderTargetResources() override;
	virtual void PostPostLoad() override;
	virtual void Reset() override;
	virtual void GenerateShaderPermutations(RE::BSShader* a_shader) override;
	std::string_view GetShaderCacheAbiVersion() override { return "5"; }
	bool SupportsPerformanceCostMeasurement() const override { return true; }
	bool IsPerformanceCostMeasurementEnabled() const override { return settings.Enabled; }
	void SetPerformanceCostMeasurementEnabled(bool a_enabled) override
	{
		settings.Enabled = a_enabled;
		if (!a_enabled)
			ClearDynamics();
	}

	/** @brief Replays the deferred fur passes after terrain blending and before blended decals. */
	void OnBeforeBlendedDecals();

	/** @brief Reports pass counts, deferred passes, resolved shell textures and depth-copy state for devbench. */
#ifdef DEVBENCH_BRIDGE_ENABLED
	json GetDiagnostics();
	/** Inspects fur or applies validated session settings and texture overrides. */
	json Control(const json& a_args);
#endif
	json CapturePerformanceSettingsState() const override;
	void RestorePerformanceToggleState(const json& a_state) override;

	/** Tracks a pass through the shared renderer without replacing its routing. */
	template <typename Draw>
	void RenderPass(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags, Draw&& a_draw)
	{
		const auto previous = currentPass;
		currentPass = { a_pass, a_technique, a_alphaTest, a_renderFlags };
		const SKSE::stl::scope_exit restore([&] {
			EndPass();
			currentPass = previous;
		});
		a_draw();
	}

	/**
	 * @brief Starts a fur pass for the render pass the engine just set up, or defers it when covered-fur hiding is on.
	 * @param a_shader The lighting shader that set up the pass.
	 * @param a_pass The render pass about to draw.
	 */
	void BeginPass(RE::BSShader* a_shader, RE::BSRenderPass* a_pass);

	/** @brief Ends the active fur pass and restores the engine's shaders. */
	void EndPass();

	/**
	 * @brief Replaces one engine draw with the instanced fur draws when a fur pass is active.
	 * @param a_indexCount Index count of the engine draw.
	 * @param a_engineInstances Instance count of the engine draw; 2 for a VR stereo draw, otherwise 1.
	 * @param a_startIndexLocation First index of the engine draw.
	 * @param a_baseVertexLocation Base vertex of the engine draw.
	 * @return True when the fur draws replaced the engine draw.
	 */
	bool DrawShells(UINT a_indexCount, UINT a_engineInstances, UINT a_startIndexLocation, INT a_baseVertexLocation);

	/** @brief True while a fur pass is active or deferred passes are replaying, except during the fur draws themselves so VR's DrawIndexedInstanced hook does not re-enter. */
	bool IsDrawing() const { return !issuingDraw && (instanceCount != 0 || replaying); }

private:
	struct Hooks;

	struct Dynamics
	{
		float4 Wind;
		float WetStrength;
		float3 Inertia;
		float TouchEye[2][4];
		float TouchCount;
		float TouchStrength;
		float TouchDebug;
		float TouchPad1 = 0.0f;
		float TouchA[FurInteractors::MaxCapsulesPerPass][4];
		float TouchB[FurInteractors::MaxCapsulesPerPass][4];
		float TouchMove[FurInteractors::MaxCapsulesPerPass][4];
	};
	static_assert(sizeof(Dynamics) == 656);

	struct PerPass
	{
		float Length;
		float ShellCount;
		float Droop;
		float RootThreshold;
		float TipThreshold;
		float RootDarkening;
		float ShellColor;
		float RootTest;
		float Density;
		float TipWeight;
		float Pad0 = 0.0f;
		float Pad1 = 0.0f;
		std::array<float, 4> PreviousShape{};
		Dynamics Current{};
		Dynamics Previous{};
	};
	static_assert(offsetof(PerPass, Density) == 32);
	static_assert(offsetof(PerPass, TipWeight) == 36);
	static_assert(offsetof(PerPass, PreviousShape) == 48);
	static_assert(offsetof(PerPass, Current) == 64);
	static_assert(offsetof(PerPass, Previous) == 720);
	static_assert(sizeof(PerPass) == 1376);

	struct DeferredPass
	{
		RE::BSRenderPass* pass = nullptr;
		uint32_t technique = 0;
		bool alphaTest = false;
		uint32_t renderFlags = 0;
	};

	struct ShellData
	{
		ShellOverride values;
		bool overridden = false;
		bool skin = false;
	};

	struct Entry
	{
		RE::BSFixedString name;
		RE::NiSourceTexturePtr shell;
		ShellData* data = nullptr;
		bool emptyOverlay = false;
		bool retryLoad = false;
		uint32_t resolvedFrame = 0;
	};

	struct ShellLookup
	{
		RE::NiSourceTexture* shell = nullptr;
		ShellData* data = nullptr;
		bool resolved = false;
		bool emptyOverlay = false;
	};

	struct FrameBody
	{
		RE::NiSkinPartition* partition = nullptr;
		RE::NiAVObject* rootParent = nullptr;
		RE::BSGeometry* geometry = nullptr;
		winrt::com_ptr<ID3D11ShaderResourceView> shellView;
		winrt::com_ptr<ID3D11ShaderResourceView> normalView;
		PerPass perPass{};
		bool deferred = false;
	};

	struct DepthStates
	{
		winrt::com_ptr<ID3D11DepthStencilState> source;
		winrt::com_ptr<ID3D11DepthStencilState> prepass;
		winrt::com_ptr<ID3D11DepthStencilState> shade;
		winrt::com_ptr<ID3D11DepthStencilState> overlay;
	};

	struct PassState
	{
		explicit PassState(ID3D11DeviceContext* a_context);
		~PassState();
		PassState(const PassState&) = delete;
		PassState& operator=(const PassState&) = delete;
		ID3D11DeviceContext* context;
		winrt::com_ptr<ID3D11VertexShader> vertexShader;
		winrt::com_ptr<ID3D11PixelShader> pixelShader;
		winrt::com_ptr<ID3D11Buffer> vertexBuffer;
		winrt::com_ptr<ID3D11Buffer> pixelBuffer;
		winrt::com_ptr<ID3D11ShaderResourceView> shellView;
		winrt::com_ptr<ID3D11ShaderResourceView> depthView;
		winrt::com_ptr<ID3D11ShaderResourceView> normalView;
	};

	/** Fills optional dynamics once per geometry and frame, retaining the previous deformation for motion vectors. */
	void FillDynamics(PerPass& a_perPass, const RE::BSGeometry* a_geometry);
	/** Selects bounded live contacts and optionally appends recovering dents. */
	void FillTouch(Dynamics& a_dynamics, const RE::BSGeometry* a_geometry, float a_length);
	FurInertia::Tuning GetInertiaTuning() const;
	float3 GetInertia(const RE::BSGeometry* a_geometry, float a_length, float a_droop);
	float3 GetWind() const;
	void DrawInertiaSettings();
	bool IsTouchActive() const { return settings.EnableTouch && (settings.TouchStrength > 0.0f || settings.ShowTouchEdges); }
	void ClearDynamics();
	void StartAssetScan();

	ShellLookup FindShell(RE::BSRenderPass* a_pass);
	const FrameBody* FindFrameBody(RE::BSGeometry* a_geometry) const;
	void RecordFrameBody(RE::NiSkinInstance* a_skinInstance, RE::BSGeometry* a_geometry, const PerPass& a_perPass, ID3D11ShaderResourceView* a_shellView, ID3D11ShaderResourceView* a_normalView, bool a_deferred);
	void BeginOverlayPass(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, const FrameBody& a_body);
	void BindPass(const PerPass& a_perPass, ID3D11ShaderResourceView* a_shellView, ID3D11ShaderResourceView* a_normalView);
	const DepthStates* GetDepthStates(ID3D11DepthStencilState* a_source);
	ShellOverride GetGlobalValues(bool a_skin) const;
	void LoadOverride(const std::string& a_shellPath, ShellData& a_data);
	bool SaveOverride(const std::string& a_shellPath, const ShellOverride& a_values, std::string& a_outputPath);
	void UpdateOverrideFadeEnd();
	void DrawOverrideSettings();
	bool CopySceneDepth();
	void DrawInstanced(UINT a_indexCount, UINT a_instanceCount, UINT a_startIndexLocation, INT a_baseVertexLocation);

	std::unordered_map<std::string, ShellData> shellTextures;
	std::string selectedShellName;
	ShellData* selectedShell = nullptr;
	std::string overrideStatus;
	bool overrideStatusFailed = false;
	float overrideFadeEnd = 0.0f;

	DeferredPass currentPass;
	std::vector<DeferredPass> deferredPasses;
	std::vector<DeferredPass> deferredOverlayPasses;
	std::vector<FrameBody> frameBodies;
	winrt::com_ptr<ID3D11Texture2D> depthCopy;
	winrt::com_ptr<ID3D11ShaderResourceView> depthCopyView;
	bool depthCopyFailed = false;
	bool deferralClosed = false;
	bool replaying = false;
	bool rootTest = false;
	bool overlayPass = false;
	bool issuingDraw = false;
	uint32_t stereoDraws = 0;
	uint32_t monoDraws = 0;
	uint32_t lastStereoDraws = 0;
	uint32_t lastMonoDraws = 0;
	uint32_t maxShells = 0;
	uint32_t lastMaxShells = 0;
	float minDensity = 1.0f;
	float lastMinDensity = 1.0f;

	FurInteractors::Memory dentMemory;
	struct DynamicsState
	{
		Dynamics current{};
		Dynamics previous{};
		std::array<float, 4> shape{};
		std::array<float, 4> previousShape{};
		const void* skin = nullptr;
		const void* partition = nullptr;
		const RE::NiAVObject* root = nullptr;
		RE::NiPoint3 center{};
		uint32_t frame = 0;
	};
	struct InertiaState
	{
		FurInertia::Spring spring;
		uint32_t frame = 0;
		const void* skin = nullptr;
		const RE::NiAVObject* root = nullptr;
	};
	std::unordered_map<const RE::BSGeometry*, DynamicsState> dynamicsStates;
	std::unordered_map<const RE::BSGeometry*, InertiaState> inertiaStates;
	uint32_t frameIndex = 0;
	uint32_t lastTouchCapsules = 0;
	uint32_t maxTouchPerPass = 0;
	uint32_t lastMaxTouchPerPass = 0;
	float touchNearestGap = FLT_MAX;
	float lastTouchNearestGap = FLT_MAX;
	float maxInertia = 0.0f;
	float lastMaxInertia = 0.0f;
	float windSpeed = 0.0f;
	float lastWindSpeed = 0.0f;
	std::atomic<bool> shellAssetsPresent{ true };
	std::jthread shellAssetScan;
	bool shellAssetScanStarted = false;

	eastl::unique_ptr<ConstantBuffer> perPassCB;
	winrt::com_ptr<ID3D11BlendState> noColorWrite;
	ankerl::unordered_dense::map<ID3D11DepthStencilState*, DepthStates> depthStates;
	ankerl::unordered_dense::map<RE::NiSourceTexture*, Entry> entries;
	ankerl::unordered_dense::set<std::string> missingShells;
	RE::NiSourceTexture* lastDiffuse = nullptr;
	ShellLookup lastLookup;
	PerPass lastPerPass{};
	std::optional<PassState> passState;
	ID3D11VertexShader* furVertexShader = nullptr;
	ID3D11PixelShader* furPixelShader = nullptr;
	ID3D11PixelShader* depthPixelShader = nullptr;
	uint32_t instanceCount = 0;
	uint32_t resolveBudget = 0;
	uint32_t passCount = 0;
	uint32_t lastPassCount = 0;
};
