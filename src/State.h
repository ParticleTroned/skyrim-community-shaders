#pragma once

#include <Tracy/Tracy.hpp>
#include <Tracy/TracyC.h>
#include <Tracy/TracyD3D11.hpp>

#include <Buffer.h>
#include <REX/W32/COMPTR.h>
#include <atomic>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <utility>

using json = nlohmann::json;

#include <FeatureBuffer.h>

#include <Hooks.h>

class State
{
public:
	struct ShaderDefinesSnapshot
	{
		std::string canonicalText;
		std::vector<std::pair<std::string, std::string>> defines;
	};

	struct RenderTargetResourcePublication
	{
		// This is a coordinated setup acknowledgement, not an exhaustive inventory
		// of every optional feature resource. Required physical targets are probed
		// separately before render-scale publication.
		uint64_t generation = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t loadedFeatureSetupCount = 0;
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		bool deferredSetupAcknowledged = false;
		bool complete = false;
	};

	struct RenderTargetResourcePublicationDiagnostics
	{
		uint64_t currentGeneration = 0;
		uint64_t completedGeneration = 0;
		uint64_t publishedGeneration = 0;
		uint32_t expectedWidth = 0;
		uint32_t expectedHeight = 0;
		uint32_t publishedWidth = 0;
		uint32_t publishedHeight = 0;
		uint32_t loadedFeatureSetupCount = 0;
		bool evaluated = false;
		bool present = false;
		bool complete = false;
		bool deferredSetupAcknowledged = false;
		bool generationMatchesCurrent = false;
		bool generationMatchesCompleted = false;
		bool dimensionsMatch = false;
		bool deviceMatches = false;
		bool contextMatches = false;
		bool current = false;
	};

	State()
	{
		std::lock_guard<std::mutex> lock(statsMutex);
		for (auto& v : smoothDrawCalls) v = 0.0;
		for (auto& v : drawCalls) v = 0;
		for (auto& v : frameTimePerType) v = 0.0f;
		for (auto& v : smoothFrameTimePerType) v = 0.0f;
		for (auto& v : enabledClasses) v = true;

		// Initialize QueryPerformanceCounter frequency
		frameTimingFrequency.QuadPart = 0;
		frameStartTime.QuadPart = 0;
	}
	std::lock_guard<std::mutex> Lock() { return std::lock_guard<std::mutex>(statsMutex); }

	static State* GetSingleton()
	{
		static State singleton;
		return &singleton;
	}

	bool enabledClasses[RE::BSShader::Type::Total - 1];
	bool enablePShaders = true;
	bool enableVShaders = true;
	bool enableCShaders = true;

	bool updateShader = true;
	bool settingCustomShader = false;
	RE::BSShader* currentShader = nullptr;
	std::string adapterDescription = "";

	uint32_t currentVertexDescriptor = 0;
	uint32_t currentPixelDescriptor = 0;
	spdlog::level::level_enum logLevel = spdlog::level::info;

	float timer = 0;
	float refractionScale = 0.25f;  // Default LLF heat warp strength
	static constexpr float kDefaultPbrMetalReflectionScale = 1.0f;
	static constexpr float kDefaultPbrMetalHighlightScale = 0.25f;
	float pbrMetalReflectionScale = kDefaultPbrMetalReflectionScale;  // Global scale for PBR metal reflections
	float pbrMetalHighlightScale = kDefaultPbrMetalHighlightScale;    // Global scale for direct PBR metal highlights
	// Burley SSS human skin controls.
	float sssHumanMaleIntensity = 1.0f;
	float sssHumanMaleSaturation = 1.0f;
	float sssHumanMaleBrightness = 1.0f;
	float sssHumanMaleBaseSaturation = 1.0f;
	float sssHumanFemaleIntensity = 1.0f;
	float sssHumanFemaleSaturation = 1.0f;
	float sssHumanFemaleBrightness = 1.0f;
	float sssHumanFemaleBaseSaturation = 1.0f;
	double smoothDrawCalls[RE::BSShader::Type::Total + 1];
	int drawCalls[RE::BSShader::Type::Total + 1];

	// Frame time tracking per shader type (in milliseconds)
	float frameTimePerType[RE::BSShader::Type::Total + 1];
	float smoothFrameTimePerType[RE::BSShader::Type::Total + 1];

	// Timing state for per-type frame time tracking using QueryPerformanceCounter
	LARGE_INTEGER frameTimingFrequency;
	LARGE_INTEGER frameStartTime;
	bool frameTimingActive = false;

	enum ConfigMode
	{
		DEFAULT,
		USER,
		TEST,
		THEME
	};
	enum class SettingsApplyMode
	{
		Runtime,
		StartupHydration,
	};

	void Draw();
	void Debug();
	void Reset();
	void Setup();
	void SetupRenderTargetResources();
	void InvalidateRenderTargetResourcePublication() noexcept;
	std::shared_ptr<const RenderTargetResourcePublication> GetRenderTargetResourcePublication() const
	{
		return renderTargetResourcePublication.load(std::memory_order_acquire);
	}
	uint64_t GetCompletedRenderTargetResourcePublicationGeneration() const;
	/** Returns the current generation, including an in-progress invalidation. */
	uint64_t GetRenderTargetResourcePublicationGeneration() const noexcept
	{
		return renderTargetResourcePublicationGeneration.load(
			std::memory_order_acquire);
	}
	/** Returns the publication facts used to evaluate the expected target size. */
	RenderTargetResourcePublicationDiagnostics GetRenderTargetResourcePublicationDiagnostics(
		uint32_t a_expectedWidth,
		uint32_t a_expectedHeight) const noexcept;
	/** Returns publication facts compared with the current physical main target. */
	RenderTargetResourcePublicationDiagnostics
	GetCurrentMainRenderTargetResourcePublicationDiagnostics() const noexcept;
	bool HasCompleteRenderTargetResourcePublication(uint32_t a_width, uint32_t a_height) const;

	void Load(
		ConfigMode a_configMode = ConfigMode::USER,
		bool a_allowReload = true,
		SettingsApplyMode a_applyMode = SettingsApplyMode::Runtime);
	// Returns true only when the main settings file and every required
	// persistence layer have been saved successfully.
	bool Save(ConfigMode a_configMode = ConfigMode::USER);

	static constexpr uint32_t kSaveLoadSafeModeGraceFrames = 120;
	static constexpr uint32_t kSaveLoadSafeModeFallbackFrames = 36000;
	static constexpr uint32_t kSaveMutationBlockGraceFrames = 60;

	bool IsSaveLoadSafeModeActive() const;
	// Current engine-owned save/load work only. Unlike IsSaveLoadSafeModeActive,
	// this excludes the fixed post-event grace used for mutation/persistence safety.
	bool IsEngineSaveLoadActivityActive() const;
	bool IsPersistentMutationBlocked() const;
	void BeginSaveLoadSafeMode(uint32_t a_currentFrame);
	void ExtendSaveLoadSafeMode(uint32_t a_currentFrame, uint32_t a_frameCount = kSaveLoadSafeModeGraceFrames);
	void BeginPersistentMutationBlock(uint32_t a_currentFrame, uint32_t a_frameCount = kSaveMutationBlockGraceFrames);
	void ExtendPersistentMutationBlock(uint32_t a_currentFrame, uint32_t a_frameCount = kSaveMutationBlockGraceFrames);
	void UpdateSaveLoadSafeMode();

	// In-memory serialization used by A/B testing and config persistence.
	// Overlay configs pass false so an absent disabled feature continues to inherit
	// its SettingsDefault section.
	void SaveToJson(
		nlohmann::json& o_json,
		bool a_includeMissingUnloadedFeatures = true);
	void LoadFromJson(
		nlohmann::json& i_json,
		bool a_loadFeatureSettings = true,
		SettingsApplyMode a_applyMode = SettingsApplyMode::Runtime);

	void LoadTheme();

	bool ValidateCache(CSimpleIniA& a_ini);
	void WriteDiskCacheInfo(CSimpleIniA& a_ini);

	void SetLogLevel(spdlog::level::level_enum a_level = spdlog::level::info);
	spdlog::level::level_enum GetLogLevel();

	void SetDefines(std::string defines);
	std::shared_ptr<const ShaderDefinesSnapshot> GetShaderDefinesSnapshot() const;
	uint64_t GetShaderDefinesGeneration() const
	{
		return shaderDefinesGeneration.load(std::memory_order_acquire);
	}

	/*
     * Whether a_type is currently enabled in CSX
     *
     * @param a_type The type of shader to check
     * @return Whether the shader has been enabled.
     */
	bool ShaderEnabled(const RE::BSShader::Type a_type);

	/*
     * Whether a_shader is currently enabled in CSX
     *
     * @param a_shader The shader to check
     * @return Whether the shader has been enabled.
     */
	bool IsShaderEnabled(const RE::BSShader& a_shader);

	/*
     * Whether developer mode is enabled allowing advanced options.
	 * Use at your own risk! No support provided.
     *
	 * <p>
	 * Developer mode is active when the log level is trace or debug.
	 * </p>
	 *
     * @return Whether in developer mode.
     */
	bool IsDeveloperMode();

	void ModifyRenderTarget(RE::RENDER_TARGETS::RENDER_TARGET a_targetIndex, RE::BSGraphics::RenderTargetProperties* a_properties);

	void SetupResources();

	/// @brief Log per-format support for D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD.
	///        We perform typed UAV loads on a number of non-guaranteed formats; on GPUs
	///        that lack TypedUAVLoadAdditionalFormats those reads return undefined data.
	///        Called once at startup; emits one info line per supported format and one
	///        warn line per unsupported format with the feature that needs it.
	void CheckTypedUAVLoadSupport();
	void ModifyShaderLookup(const RE::BSShader& a_shader, uint& a_vertexDescriptor, uint& a_pixelDescriptor, bool a_forceDeferred = false);

	/** @brief Opens a named D3D annotation and Tracy zone. */
	void BeginPerfEvent(std::string_view title);
	/** @brief Formats an event title into reusable thread-local storage. */
	template <class... Args>
	void BeginPerfEvent(std::format_string<Args...> fmt, Args&&... args)
	{
		thread_local std::string perfTitle;
		perfTitle.clear();
		std::format_to(std::back_inserter(perfTitle), fmt, std::forward<Args>(args)...);
		BeginPerfEvent(std::string_view{ perfTitle });
	}
	void EndPerfEvent();

	/** @brief Opens a high-frequency D3D annotation without creating a Tracy zone. */
	void BeginDrawEvent(std::string_view title);
	/** @brief Formats a high-frequency annotation into reusable thread-local storage. */
	template <class... Args>
	void BeginDrawEvent(std::format_string<Args...> fmt, Args&&... args)
	{
		thread_local std::string drawTitle;
		drawTitle.clear();
		std::format_to(std::back_inserter(drawTitle), fmt, std::forward<Args>(args)...);
		BeginDrawEvent(std::string_view{ drawTitle });
	}
	void EndDrawEvent();

	void SetPerfMarker(std::string_view title);
	/** @brief Formats marker text into reusable thread-local storage. */
	template <class... Args>
	void SetPerfMarker(std::format_string<Args...> fmt, Args&&... args)
	{
		thread_local std::string markerText;
		markerText.clear();
		std::format_to(std::back_inserter(markerText), fmt, std::forward<Args>(args)...);
		SetPerfMarker(std::string_view{ markerText });
	}

	void SetAdapterDescription(const std::wstring& description);

	bool frameAnnotations = false;

	// Pass D3DCOMPILE_PARTIAL_PRECISION to fxc. With explicit min16float types this is
	// mostly belt-and-braces in SM5, but it lets the compiler downgrade unmarked float
	// ops to FP16 where it can prove safety. On by default; toggle off when reversing
	// shaders or chasing a precision bug.
	// Atomic: written from the UI thread, read from compilation pool workers.
	std::atomic_bool enablePartialPrecision{ false };

	// Pass D3DCOMPILE_AVOID_FLOW_CONTROL to fxc. Forces the compiler to flatten branches
	// into predicated ops instead of using dynamic flow control. Can win on uniform-branch
	// or short-body branches; can lose on long divergent branches that vanilla flow
	// control would skip. Transient (session-only); not saved to config because the
	// right setting depends on the current scene/work, not the user.
	// Atomic: written from the UI thread, read from compilation pool workers.
	std::atomic_bool enableAvoidFlowControl{ false };

	uint lastVertexDescriptor = 0;
	uint lastPixelDescriptor = 0;
	uint modifiedVertexDescriptor = 0;
	uint modifiedPixelDescriptor = 0;
	uint lastModifiedVertexDescriptor = 0;
	uint lastModifiedPixelDescriptor = 0;
	uint lastExtraDescriptor = 0;
	uint lastExtraFeatureDescriptor = 0;

	enum class ExtraShaderDescriptors : uint32_t
	{
		InWorld = 1 << 0,
		IsReflections = 1 << 1,
		IsBeastRace = 1 << 2,
		EffectShadows = 1 << 3,
		IsTree = 1 << 4,
		GrassSphereNormal = 1 << 5,
		IsFemale = 1 << 6,
		SuppressExternalEmittance = 1 << 7,
		AdditiveLighting = 1 << 8
	};

	enum class ExtraFeatureDescriptors : uint32_t
	{
		THLand0HasDisplacement = 1 << 0,
		THLand1HasDisplacement = 1 << 1,
		THLand2HasDisplacement = 1 << 2,
		THLand3HasDisplacement = 1 << 3,
		THLand4HasDisplacement = 1 << 4,
		THLand5HasDisplacement = 1 << 5,
		ETMaterialModel = 0b111 << 6,
		THLandHasDisplacement = 1 << 9
	};

	bool inWorld = false;
	uint32_t lastWorldRenderFrame = std::numeric_limits<uint32_t>::max();
	uint32_t lastCompletedWorldRenderFrame = std::numeric_limits<uint32_t>::max();
	bool pendingPostLoadRuntimeReset = false;
	std::atomic_bool saveLoadSafeModeActive{ false };
	std::atomic_bool engineSaveLoadActivityActive{ false };
	std::atomic_uint32_t saveLoadSafeModeStartFrame{ 0 };
	std::atomic_uint32_t saveLoadSafeModeEndFrame{ 0 };
	std::atomic_bool persistentMutationBlocked{ false };
	std::atomic_uint32_t persistentMutationBlockEndFrame{ 0 };
	bool activeReflections = false;
	// Set after startup work that can block the first usable render completes.
	std::atomic_bool startupMenuInitializationComplete{ false };
	// Set after the first successful Present following startup initialization.
	bool startupMenuBlurSourceReady = false;

	// Cached menu open states, updated once per frame in Reset().
	// Avoids repeated IsMenuOpen calls (each constructs a BSFixedString).
	bool isMainMenuOpen = false;
	bool isLoadingMenuOpen = false;
	bool isMapMenuOpen = false;
	bool IsMainOrLoadingMenuOpen() const { return isMainMenuOpen || isLoadingMenuOpen; }
	bool IsMainOrLoadingMenuOpen(RE::UI* ui) const
	{
		return IsMainOrLoadingMenuOpen() ||
		       (ui && (ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)));
	}

	void UpdateSharedData(bool a_inWorld, bool a_prepass);
	void UpdateFeatureData(bool a_inWorld);
	/**
	 * @brief Updates the lighting permutation from the current render pass blend state.
	 * @param a_pass Lighting render pass to inspect.
	 */
	void UpdateLightingShaderPermutation(RE::BSRenderPass* a_pass);
	bool HasDirectionalShadows() const;

	struct PermutationCB
	{
		uint VertexShaderDescriptor;
		uint PixelShaderDescriptor;
		uint ExtraShaderDescriptor;
		uint ExtraFeatureDescriptor;

		bool operator==(const PermutationCB& other) const
		{
			return PixelShaderDescriptor == other.PixelShaderDescriptor &&
			       ExtraShaderDescriptor == other.ExtraShaderDescriptor &&
			       ExtraFeatureDescriptor == other.ExtraFeatureDescriptor;
		}
	};
	STATIC_ASSERT_ALIGNAS_16(PermutationCB);

	ConstantBuffer* permutationCB = nullptr;

#ifdef _MSC_VER
#	pragma warning(push)
#	pragma warning(disable: 4324)  // SharedDataCB intentionally uses aligned float4 members for shader-compatible math
#endif
	struct alignas(16) SharedDataCB
	{
		float4 WaterData[25];
		float4 DirLightDirection;
		float4 DirLightColor;
		float4 CameraData;
		float4 BufferDim;
		float Timer;
		uint FrameCount;
		uint FrameCountAlwaysActive;
		uint InInterior;
		uint InMapMenu;
		uint HideSky;
		float MipBias;
		float WaterSystemHeight;        // TES::GetWaterHeight at eye-0 in camera-relative Z; -NI_INFINITY when no water body found (VR only)
		float RefractionScale;          // matches HLSL SharedData::RefractionScale
		float PBRMetalReflectionScale;  // matches HLSL SharedData::PBRMetalReflectionScale
		float PBRMetalHighlightScale;   // matches HLSL SharedData::PBRMetalHighlightScale
		uint HasDirectionalShadows;     // Uses the existing scalar padding slot before the float2 below
		float PBRMetalReflectionScalePad0;
		float PBRMetalReflectionScalePad1;
		float SSSHumanMaleIntensity;
		float SSSHumanMaleSaturation;
		float SSSHumanMaleBrightness;
		float SSSHumanMaleBaseSaturation;
		float SSSHumanFemaleIntensity;
		float SSSHumanFemaleSaturation;
		float SSSHumanFemaleBrightness;
		float SSSHumanFemaleBaseSaturation;
		uint VolumetricShadowsEnabled;
		float VolumetricLightingOpacity;
		float4 AmbientSHR;
		float4 AmbientSHG;
		float4 AmbientSHB;
		float4 VRFoveationData0;          // x=center scale, y=feather, z=horizontal scale, w=lighting auxiliary mode: 0 off, 1 feathered, 2 hard cutoff
		float4 VRFoveationModes;          // x=SSR raymarch mode, y=water parallax mode, z=Wetterness dynamic detail mode, w=unused: 0 off, 1 feathered, 2 hard cutoff
		float4 VRFoveationCenterOffsets;  // xy=left eye offset, zw=right eye offset
	};
#ifdef _MSC_VER
#	pragma warning(pop)
#endif
	STATIC_ASSERT_ALIGNAS_16(SharedDataCB);
	static_assert(offsetof(SharedDataCB, RefractionScale) % 16 == 0);
	static_assert(offsetof(SharedDataCB, PBRMetalReflectionScalePad0) % 16 == 0);
	static_assert(offsetof(SharedDataCB, VolumetricShadowsEnabled) == offsetof(SharedDataCB, SSSHumanFemaleBaseSaturation) + sizeof(float));
	static_assert(offsetof(SharedDataCB, VolumetricLightingOpacity) == offsetof(SharedDataCB, VolumetricShadowsEnabled) + sizeof(uint));
	static_assert(offsetof(SharedDataCB, AmbientSHR) % 16 == 0);
	static_assert(offsetof(SharedDataCB, VRFoveationData0) % 16 == 0);
	static_assert(offsetof(SharedDataCB, VRFoveationModes) % 16 == 0);
	static_assert(offsetof(SharedDataCB, VRFoveationCenterOffsets) % 16 == 0);
	ConstantBuffer* sharedDataCB = nullptr;
	ConstantBuffer* featureDataCB = nullptr;

	PermutationCB permutationData{};
	PermutationCB permutationDataPrevious{};

	Util::FrameChecker frameChecker;
	uint frameCount = 0;
	// Render-thread publication for diagnostics recorded by shader workers.
	std::atomic<uint32_t> frameCountAtomic{ 0 };

	// Skyrim constants
	float2 screenSize = {};
	D3D_FEATURE_LEVEL featureLevel;

	TracyD3D11Ctx tracyCtx = nullptr;  // Tracy context

	void ClearDisabledFeatures();
	bool SetFeatureDisabled(const std::string& featureName, bool isDisabled);
	bool IsFeatureDisabled(const std::string& featureName);
	std::unordered_map<std::string, bool>& GetDisabledFeatures();

	bool useFrameAnnotations = false;

	// --- Utility Methods ---
	/**
	 * @brief Gets the total smoothed draw calls from the global state
	 * @return Total number of draw calls as float
	 */
	float GetTotalSmoothedDrawCalls() const;

	/**
	 * @brief Base helper that iterates through valid shader types (excluding None and Total)
	 * @param callback Function to call for each valid shader type with parameters: (type, typeIndex, classIndex)
	 */
	template <typename Callback>
	static void ForEachValidShaderType(Callback callback)
	{
		for (auto type : magic_enum::enum_values<RE::BSShader::Type>()) {
			if (type == RE::BSShader::Type::None || type == RE::BSShader::Type::Total)
				continue;
			int typeIndex = magic_enum::enum_integer(type);
			int classIndex = typeIndex - 1;
			callback(type, typeIndex, classIndex);
		}
	}

	/**
	 * @brief Iterates through valid shader types with performance metrics
	 * @param callback Function to call for each shader type with parameters: (type, typeIndex, drawCalls, frameTime, percent, costPerCall)
	 */
	template <typename Callback>
	static void ForEachShaderTypeWithMetrics(Callback callback)
	{
		ForEachValidShaderType([&](auto type, int typeIndex, [[maybe_unused]] int classIndex) {
			float drawCalls = static_cast<float>(GetSingleton()->smoothDrawCalls[typeIndex]);
			float frameTime = static_cast<float>(GetSingleton()->smoothFrameTimePerType[typeIndex]);
			float percent = (frameTime > 0.0f && GetSingleton()->smoothFrameTimePerType[magic_enum::enum_integer(RE::BSShader::Type::Total)] > 0.0f) ?
			                    (frameTime / GetSingleton()->smoothFrameTimePerType[magic_enum::enum_integer(RE::BSShader::Type::Total)] * 100.0f) :
			                    0.0f;
			float costPerCall = (drawCalls > 0.0f) ? (frameTime / drawCalls) : 0.0f;
			callback(type, typeIndex, drawCalls, frameTime, percent, costPerCall);
		});
	}

	/**
	 * @brief Iterates through valid shader types with class indices for UI operations
	 * @param callback Function to call for each shader type with parameters: (type, classIndex)
	 */
	template <typename Callback>
	static void ForEachShaderTypeWithIndex(Callback callback)
	{
		ForEachValidShaderType([&](auto type, [[maybe_unused]] int typeIndex, int classIndex) {
			callback(type, classIndex);
		});
	}

	std::unordered_map<std::string, bool> disabledFeatures;
	std::mutex m_mutex;

	inline ~State()
	{
#ifdef TRACY_ENABLE
		if (tracyCtx)
			TracyD3D11Destroy(tracyCtx);
#endif
	}

private:
	uint64_t BeginRenderTargetResourcePublication() noexcept;
	void CompleteRenderTargetResourcePublication(
		uint64_t a_generation,
		uint32_t a_loadedFeatureSetupCount);

	std::atomic<std::shared_ptr<const ShaderDefinesSnapshot>> shaderDefinesSnapshot{
		std::make_shared<const ShaderDefinesSnapshot>()
	};
	std::atomic<uint64_t> shaderDefinesGeneration{ 0 };
	std::atomic<std::shared_ptr<const RenderTargetResourcePublication>> renderTargetResourcePublication{
		std::make_shared<const RenderTargetResourcePublication>()
	};
	std::atomic<uint64_t> renderTargetResourcePublicationGeneration{ 0 };
	std::atomic<uint64_t> completedRenderTargetResourcePublicationGeneration{ 0 };
	ID3D11Device* setupResourcesDevice = nullptr;
	ID3D11DeviceContext* setupResourcesContext = nullptr;
	REX::W32::ComPtr<REX::W32::ID3DUserDefinedAnnotation> pPerf;
	std::mutex statsMutex;
};
