#pragma once

#include "Buffer.h"
#include "GrassCollisionMode.h"
#include "Utils/LazyShader.h"

#include <array>
#include <atomic>
#include <cfloat>
#include <d3d11_1.h>
#include <unordered_map>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "GrassCollisionVertexProbe.h"
#	include <memory>
#	include <exception>
#	include <mutex>
#	include <optional>
#	include <vector>
#endif

// GPU constants retain their required alignment inside the owning feature.
#pragma warning(push)
#pragma warning(disable: 4324)

struct GrassCollision : Feature
{
private:
	static constexpr std::string_view MOD_ID = "87816";

public:
	static constexpr float kMinCollisionStrength = 0.0f;
	static constexpr float kMaxCollisionStrength = 5.0f;
	static constexpr float kDefaultCollisionStrength = 1.0f;
	static constexpr float kBasicCollisionDistance = 2048.0f;

	virtual inline std::string GetName() override { return "Grass Collision"; }
	virtual inline std::string GetShortName() override { return "GrassCollision"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "GRASS_COLLISION"; }
	virtual bool IsCore() const override { return true; };
	virtual std::string_view GetCategory() const override { return FeatureCategories::kFoliage; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Bends grass around actors, with optional advanced collision.",
			{ "Basic bending with adjustable strength",
				"Optional swept actor capsules with persistent bend and compression",
				"Adjustable impact, spring response, damping and recovery",
				"Advanced tracks up to 64 nearby actors with three colliders each",
				"Current and previous deformation for motion vectors",
				"Advanced lighting normals follow the bend direction" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	struct Settings
	{
		bool EnableGrassCollision = true;
		bool EnableAdvancedGrassCollision = false;
		bool TrackRagdolls = true;
		float CollisionStrength = kDefaultCollisionStrength;
		float CollisionRadiusScale = 1.0f;
		float GrassInteractionRadius = 25.0f;
		float CollisionImpactStrength = 3.0f;
		float SpringStrength = 5.0f;
		float Damping = 7.5f;
		float MaximumBend = 89.0f;
		float MaximumCompression = 0.2f;
		float CompressionHeight = 10.0f;
		float MaximumCompressibleGrassHeight = 40.0f;
		float CompressionRecovery = 0.25f;
	};
	struct Parameter
	{
		const char* name;
		const char* label;
		float Settings::* member;
		float minimum, maximum;
	};
	static constexpr std::array parameters{
		Parameter{ "CollisionStrength", "Deformation strength", &Settings::CollisionStrength, kMinCollisionStrength, kMaxCollisionStrength },
		Parameter{ "CollisionRadiusScale", "Actor radius scale", &Settings::CollisionRadiusScale, 1.0f, 10.0f },
		Parameter{ "GrassInteractionRadius", "Interaction radius", &Settings::GrassInteractionRadius, 0.0f, 128.0f },
		Parameter{ "CollisionImpactStrength", "Impact strength", &Settings::CollisionImpactStrength, 0.0f, 4.0f },
		Parameter{ "SpringStrength", "Spring strength", &Settings::SpringStrength, 0.05f, 40.0f },
		Parameter{ "Damping", "Damping", &Settings::Damping, 0.0f, 20.0f },
		Parameter{ "MaximumBend", "Maximum bend (degrees)", &Settings::MaximumBend, 5.0f, 89.0f },
		Parameter{ "MaximumCompression", "Maximum compression", &Settings::MaximumCompression, 0.0f, 1.0f },
		Parameter{ "CompressionHeight", "Compression height (%)", &Settings::CompressionHeight, 10.0f, 100.0f },
		Parameter{ "MaximumCompressibleGrassHeight", "Maximum compressible height", &Settings::MaximumCompressibleGrassHeight, 16.0f, 512.0f },
		Parameter{ "CompressionRecovery", "Compression recovery", &Settings::CompressionRecovery, 0.1f, 10.0f }
	};
	/** @brief Apply a partial settings object atomically; reject unknown or invalid values. */
	bool Configure(const json& values, std::string& error);
	/** @brief Invalidate temporal fields and actor sweeps after a scene load. */
	void ResetRuntimeStateAfterGameLoad();

	struct alignas(16) BoundingBoxPacked
	{
		float2 MinExtent = { FLT_MAX, FLT_MAX };
		float2 MaxExtent = { -FLT_MAX, -FLT_MAX };
		uint IndexStart = 0;
		uint IndexEnd = 0;
		float2 pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(BoundingBoxPacked);

	struct alignas(16) CollisionShapePacked
	{
		float4 CurrentPointAAndRadius, CurrentPointB, PreviousPointA, PreviousPointB;
	};
	static_assert(sizeof(CollisionShapePacked) == 64);
	struct alignas(16) PerFrame
	{
		float2 PosOffset;
		DirectX::XMUINT2 ArrayOrigin;
		float2 PreviousPosOffset;
		DirectX::XMUINT2 PreviousArrayOrigin;
		float CompressionHeight, CollisionStrength, PreviousCollisionStrength, MaximumCompressibleGrassHeight;
		float MaximumBend, PreviousMaximumBend, PreviousCompressionHeight, PreviousMaximumCompressibleGrassHeight;
	};
	static_assert(sizeof(PerFrame) == 64 && offsetof(PerFrame, CollisionStrength) == 36 && offsetof(PerFrame, PreviousCollisionStrength) == 40);
	struct alignas(16) UpdateConstants
	{
		float2 PosOffset;
		DirectX::XMUINT2 ArrayOrigin;
		DirectX::XMINT2 ValidMargin;
		float TimeDelta;
		uint BoundingBoxCount;
		float GrassInteractionRadius, CollisionStrength, SpringStrength, Damping;
		float MaximumBend, MaximumCompression, CompressionRecovery, Padding;
	};
	static_assert(sizeof(UpdateConstants) == 64);
	struct alignas(16) BasicUpdateConstants
	{
		float2 PosOffset;
		DirectX::XMUINT2 ArrayOrigin;
		DirectX::XMINT2 ValidMargin;
		float TimeDelta;
		uint BoundingBoxCount;
		float CameraHeightDelta;
		float3 Padding;
	};
	static_assert(sizeof(BasicUpdateConstants) == 48);

	Settings settings;

	eastl::unique_ptr<ConstantBuffer> perFrame;

	eastl::unique_ptr<Buffer> collisionBoundingBoxes = nullptr;
	eastl::unique_ptr<Buffer> collisionInstances = nullptr;

	eastl::vector<BoundingBoxPacked> queuedBoundingBoxes;
	eastl::vector<CollisionShapePacked> queuedCollisions;
	eastl::vector<float4> queuedBasicCollisions;

	virtual void ClearShaderCache() override;

	ID3D11ComputeShader* GetCollisionUpdateCS();
	Util::LazyShader<ID3D11ComputeShader> collisionUpdateCS;
	Util::LazyShader<ID3D11ComputeShader> basicUpdateCS;

	virtual void SetupResources() override;
	/** @brief Release an obsolete backend after a frame with no grass draws. */
	void Reset() override;

	virtual void DrawSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool a_advanced) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override { return settings.EnableGrassCollision; }
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override { settings.EnableGrassCollision = a_enabled; }
	void QueueCollisions();
	void Update();
	/** @brief Keep collision vertex code only while enabled or consuming the final temporal history. */
	bool NeedsVertexDeformation() const;
	/** @brief Return the frame's backend, including a retiring backend's final motion history. */
	GrassCollisionMode::Mode GetVertexMode() const;
	/** @brief Conservative culling multiplier for the active backend and temporal strength. */
	float GetCullingStrength() const;

	/** @brief Read the bounded multiplier applied to actor-driven grass displacement. */
	float GetCollisionStrength() const;
	/** @brief Stage a finite multiplier in [0,5]; reject invalid input without changing settings. */
	bool SetCollisionStrength(float a_strength);

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** @brief Request a bounded window of collision observations and one asynchronous texture readback. */
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

	virtual bool SupportsVR() override { return true; };

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
			stl::write_thunk_call<MainUpdate_QueueCollisions>(REL::RelocationID(35565, 36564).address() + REL::Relocate(0x748, 0xC26, 0x7EE));
			logger::info("[GRASS COLLISION] Installed hooks");
		}
	};

private:
	void DrawStrengthControl();
	void NormalizeSettings();
#ifdef DEVBENCH_BRIDGE_ENABLED
	struct QueueDiagnostics;
#endif
	void QueueBasicActor(RE::NiAVObject* root, float minimumRadius, BoundingBoxPacked& box
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		QueueDiagnostics* diagnostic
#endif
	);
	bool UpdateCollisionTexture(const UpdateConstants& constants, bool simulate);
	bool UpdateBasicCollisionTexture(const UpdateConstants& constants);
	GrassCollisionMode::Mode GetRequestedMode() const;
	void ClearFields();
	void ReleaseResources();
	static constexpr uint32_t kFieldSize = 1024;
	static constexpr float kWorldSize = 8192.0f;
	static constexpr uint32_t kBasicFieldSize = 512;
	static constexpr float kBasicWorldSize = 4096.0f;
	struct ColliderHistory
	{
		RE::NiPoint3 pointA, pointB;
		const void* root;
		const void* body;
		const void* shape;
		uint32_t actorID, frame;
	};
	std::unordered_map<const RE::bhkNiCollisionObject*, ColliderHistory> colliderHistory;
	RE::NiPoint3 queuedCamera{};
	GrassCollisionMode::Mode queuedMode = GrassCollisionMode::Mode::Disabled;
	std::atomic<GrassCollisionMode::Mode> resourceMode{ GrassCollisionMode::Mode::Disabled };
	std::atomic<GrassCollisionMode::Mode> failedMode{ GrassCollisionMode::Mode::Disabled };
	float2 previousCell{};
	float previousCameraHeight = 0;
	PerFrame published{};
	std::array<eastl::unique_ptr<Texture2D>, 2> deformation, velocity;
	eastl::unique_ptr<ConstantBuffer> updateConstants;
	winrt::com_ptr<ID3D11SamplerState> fieldSampler;
	winrt::com_ptr<ID3D11DeviceContext1> computeContext;
	uint32_t fieldIndex = 0, lastUpdateFrame = UINT32_MAX;
	bool fieldInitialized = false, fieldsCleared = false, vertexResourcesBound = false;
	std::atomic_bool currentDeformationActive{ false };
	std::atomic_uint32_t publishedFrame{ UINT32_MAX };
	std::atomic_bool resourceFailed{ false };
	std::atomic_bool resetRequested{ true }, historyResetRequested{ true };

#ifdef DEVBENCH_BRIDGE_ENABLED
	static constexpr uint32_t kDiagnosticsWindowFrames = GrassCollisionVertexProbe::kCaptureFrames;
	std::atomic_uint64_t fieldResourceBytes{ 0 };
	struct QueueDiagnostics
	{
		uint32_t frame = 0;
		size_t candidates = 0, actorRoots = 0, shapeVisits = 0, boundFailures = 0, radiusRejected = 0;
		size_t playerShapes = 0, otherActorShapes = 0, boundingBoxes = 0, shapes = 0;
		float3 cameraPosition{};
		std::vector<CollisionShapePacked> shapeSamples;
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
		float collisionStrength = kDefaultCollisionStrength;
		float previousCollisionStrength = kDefaultCollisionStrength;
		Settings settings;
	};
	struct DrawDiagnostics
	{
		uint32_t firstFrame = 0, lastFrame = 0;
		size_t nativeChecks = 0, batchedChecks = 0, disabledShaderChecks = 0, residualCollisionShaderChecks = 0;
		size_t missingConstantBuffer = 0, wrongConstantBuffer = 0, missingTexture = 0, wrongTexture = 0;
		size_t missingPreviousTexture = 0, wrongPreviousTexture = 0, missingSampler = 0, wrongSampler = 0;
		size_t collisionShaderChecks = 0, missingCollisionShaderInputs = 0, unidentifiedVertexShaders = 0, shaderReflectionFailures = 0;
	};
	ID3D11DeviceContext1* GetVertexProbeContext();
	void PollVertexProbe(uint32_t a_frame);
	void ObserveCollisionTexture(uint32_t a_frame, const PerFrame& a_constants);
	void PollCollisionTexture(uint32_t a_frame);
	void ObserveUpdateDiagnostics(uint32_t frame, const UpdateConstants* constants = nullptr, size_t shapes = 0, bool dispatched = false);
	void RecordDiagnosticsFailure(const char* stage, const std::exception& error) noexcept;
	bool DiagnosticsActive(uint32_t a_frame) const;
	mutable std::mutex diagnosticsMutex;
	std::atomic_uint64_t diagnosticsRequestFrame{ UINT64_MAX };
	std::atomic_uint64_t diagnosticsGeneration{ 0 };
	std::atomic_bool diagnosticsFailed{ false };
	std::array<char, 512> diagnosticsError{};
	std::optional<QueueDiagnostics> queueDiagnostics;
	std::optional<UpdateDiagnostics> updateDiagnostics;
	DrawDiagnostics drawDiagnostics;
	struct DiagnosticShader
	{
		winrt::com_ptr<ID3D11VertexShader> vertex;
		std::shared_ptr<const std::vector<uint8_t>> bytes;
		uint32_t descriptor = 0;
		uint8_t inputs = 0;
	};
	DiagnosticShader* GetDiagnosticShader(ID3D11VertexShader* vertex);
	std::vector<DiagnosticShader> shaderDiagnostics;
	json textureDiagnostics = nullptr;
	winrt::com_ptr<ID3D11Texture2D> diagnosticReadback;
	uint64_t readbackRequestGeneration = UINT64_MAX;
	uint32_t readbackFrame = 0;
	DirectX::XMUINT2 readbackOrigin{};
	GrassCollisionMode::Mode readbackMode = GrassCollisionMode::Mode::Disabled;
	bool readbackPending = false;
	GrassCollisionVertexProbe vertexProbe;
	winrt::com_ptr<ID3D11DeviceContext1> vertexProbeContext;
	std::atomic_bool vertexProbeFailed{ false };
#endif
};

#pragma warning(pop)
