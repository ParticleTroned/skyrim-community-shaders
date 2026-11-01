#pragma once

#include "Buffer.h"
#include "Utils/LazyShader.h"

#include <cfloat>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "GrassCollisionVertexProbe.h"
#	include <atomic>
#	include <mutex>
#	include <optional>
#	include <vector>
#endif

struct GrassCollision : Feature
{
private:
	static constexpr std::string_view MOD_ID = "87816";

public:
	static constexpr float kMinCollisionStrength = 0.0f;
	static constexpr float kMaxCollisionStrength = 5.0f;
	static constexpr float kDefaultCollisionStrength = 1.0f;

	virtual inline std::string GetName() override { return "Grass Collision"; }
	std::string GetDisplayName() override { return T("feature.grass_collision.name", "Grass Collision"); }
	virtual inline std::string GetShortName() override { return "GrassCollision"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "GRASS_COLLISION"; }
	virtual bool IsCore() const override { return true; };
	virtual std::string_view GetCategory() const override { return FeatureCategories::kFoliage; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.grass_collision.description", "Enables dynamic grass interactions where grass bends and moves in response to actors walking through it, creating more immersive environmental reactions."),
			{ T("feature.grass_collision.key_feature_1", "Real-time grass deformation from actor movement"),
				T("feature.grass_collision.key_feature_2", "Collision detection for up to 256 simultaneous interactions"),
				T("feature.grass_collision.key_feature_3", "Dynamic tracking of actor positions for grass response"),
				T("feature.grass_collision.key_feature_4", "Performance-optimized collision calculation"),
				T("feature.grass_collision.key_feature_5", "Seamless integration with existing grass rendering"),
				T("feature.grass_collision.key_feature_6", "Adjustable bending strength for the player and other actors") } };
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	void UpdateCollisionTexture();

	struct Settings
	{
		bool EnableGrassCollision = 1;
		bool TrackRagdolls = 1;
		bool EnableBlur = 1;
		float CollisionStrength = kDefaultCollisionStrength;
	};

	struct alignas(16) BoundingBoxPacked
	{
		float2 MinExtent = { FLT_MAX, FLT_MAX };
		float2 MaxExtent = { -FLT_MAX, -FLT_MAX };
		uint IndexStart = 0;
		uint IndexEnd = 0;
		float2 pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(BoundingBoxPacked);

	struct alignas(16) PerFrame
	{
		float2 PosOffset;              // cell origin in camera model space
		DirectX::XMUINT2 ArrayOrigin;  // xy: array origin (clipmap wrapping)

		DirectX::XMINT2 ValidMargin;
		float TimeDelta;
		uint BoundingBoxCount;

		float CameraHeightDelta;
		float CollisionStrength;
		float PreviousCollisionStrength;
		float pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);
	static_assert(sizeof(PerFrame) == 48 && offsetof(PerFrame, CollisionStrength) == 36 && offsetof(PerFrame, PreviousCollisionStrength) == 40);

	Settings settings;

	ConstantBuffer* perFrame = nullptr;

	eastl::unique_ptr<Buffer> collisionBoundingBoxes = nullptr;
	eastl::unique_ptr<Buffer> collisionInstances = nullptr;

	eastl::vector<BoundingBoxPacked> queuedBoundingBoxes;
	eastl::vector<float4> queuedCollisions;

	virtual void ClearShaderCache() override;

	ID3D11ComputeShader* GetCollisionUpdateCS();
	Util::LazyShader<ID3D11ComputeShader> collisionUpdateCS;

	Texture2D* collisionTexture = nullptr;

	virtual void SetupResources() override;

	virtual void DrawSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool a_advanced) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override { return settings.EnableGrassCollision; }
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override { settings.EnableGrassCollision = a_enabled; }
	void QueueCollisions();
	void Update();
	/** @brief Read the bounded multiplier applied to actor-driven grass displacement. */
	float GetCollisionStrength() const;
	/** @brief Stage a finite multiplier in [0,5]; reject invalid input without changing settings. */
	bool SetCollisionStrength(float a_strength);

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** @brief Request three frames of collision observations and one asynchronous texture readback. */
	void RequestDiagnostics();
	/** @brief Read the last requested collision observations without touching renderer state. */
	json GetDiagnostics() const;
	/** @brief Observe collision bindings immediately before a native or batched grass draw. */
	void ObserveDrawBindings(bool a_batched);
	/** @brief Inspect bounded output pairs from the exact native or indirect D3D draw. */
	void ObserveVertexDraw(ID3D11DeviceContext* a_context, const GrassCollisionVertexProbe::Draw& a_draw,
		GrassCollisionVertexProbe::DirectDraw a_direct, GrassCollisionVertexProbe::IndirectDraw a_indirect);
#endif

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual void PostPostLoad() override;


	struct Hooks
	{
		struct BSGrassShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct MainUpdate_QueueCollisions
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSGrassShader_SetupGeometry>(RE::VTABLE_BSGrassShader[0]);
			stl::write_thunk_call<MainUpdate_QueueCollisions>(REL::RelocationID(35565, 36564).address() + REL::Relocate(0x748, 0xC26));
			logger::info("[GRASS COLLISION] Installed hooks");
		}
	};

private:
	void DrawStrengthControl();
	float previousCollisionStrength = kDefaultCollisionStrength;

#ifdef DEVBENCH_BRIDGE_ENABLED
	static constexpr uint32_t kDiagnosticsWindowFrames = 3;
	struct QueueDiagnostics
	{
		uint32_t frame = 0;
		size_t candidates = 0, actorRoots = 0, shapeVisits = 0, boundFailures = 0, radiusRejected = 0;
		size_t playerShapes = 0, otherActorShapes = 0, boundingBoxes = 0, shapes = 0;
		float3 cameraPosition{};
		std::vector<float4> shapeSamples;
	};
	struct UpdateDiagnostics
	{
		uint32_t frame = 0, boundingBoxes = 0;
		size_t shapes = 0;
		bool shaderObjectAvailable = false, dispatchIssued = false;
		float timeDelta = 0;
		float2 posOffset{};
		DirectX::XMUINT2 arrayOrigin{};
		DirectX::XMINT2 validMargin{};
		float cameraHeightDelta = 0;
		float collisionStrength = kDefaultCollisionStrength;
		float previousCollisionStrength = kDefaultCollisionStrength;
	};
	struct DrawDiagnostics
	{
		uint32_t firstFrame = 0, lastFrame = 0;
		size_t nativeChecks = 0, batchedChecks = 0;
		size_t missingConstantBuffer = 0, wrongConstantBuffer = 0, missingTexture = 0, wrongTexture = 0;
		size_t collisionShaderChecks = 0, missingCollisionShaderInputs = 0, unidentifiedVertexShaders = 0, shaderReflectionFailures = 0;
	};
	ID3D11DeviceContext1* GetVertexProbeContext();
	void PollVertexProbe(uint32_t a_frame);
	void ObserveCollisionTexture(uint32_t a_frame, const PerFrame& a_constants);
	void PollCollisionTexture(uint32_t a_frame);
	bool DiagnosticsActive(uint32_t a_frame) const;
	mutable std::mutex diagnosticsMutex;
	std::atomic_uint64_t diagnosticsRequestFrame{ UINT64_MAX };
	std::atomic_uint64_t diagnosticsGeneration{ 0 };
	std::optional<QueueDiagnostics> queueDiagnostics;
	std::optional<UpdateDiagnostics> updateDiagnostics;
	DrawDiagnostics drawDiagnostics;
	std::vector<std::pair<winrt::com_ptr<ID3D11VertexShader>, uint8_t>> shaderDiagnostics;
	json textureDiagnostics = nullptr;
	winrt::com_ptr<ID3D11Texture2D> diagnosticReadback;
	uint64_t readbackRequestGeneration = UINT64_MAX;
	uint32_t readbackFrame = 0;
	DirectX::XMUINT2 readbackOrigin{};
	bool readbackPending = false;
	GrassCollisionVertexProbe vertexProbe;
	winrt::com_ptr<ID3D11DeviceContext1> vertexProbeContext;
	std::string vertexProbeError;
	std::atomic_bool vertexProbeFailed{ false };
#endif
};
