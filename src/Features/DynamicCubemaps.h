#pragma once

#include <array>
#include <bit>
#include <memory>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <atomic>
#endif

#include "Buffer.h"
#include "Utils/LazyShader.h"

class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource);
	static bool Register();
};

struct DynamicCubemaps : Feature
{
public:
	static constexpr uint32_t kPerformanceCubemapResolution = 128;
	static constexpr uint32_t kQualityCubemapResolution = 256;

	const std::string defaultDynamicCubeMapSavePath = "Data\\textures\\DynamicCubemaps";

	// Specular irradiance

	ID3D11SamplerState* computeSampler = nullptr;

	struct alignas(16) SpecularMapFilterSettingsCB
	{
		float roughness;
		float pad[3];
	};
	STATIC_ASSERT_ALIGNAS_16(SpecularMapFilterSettingsCB);

	Util::LazyShader<ID3D11ComputeShader> specularIrradianceCS;
	std::unique_ptr<ConstantBuffer> spmapCB;
	Texture2D* envTexture = nullptr;
	Texture2D* envReflectionsTexture = nullptr;
	std::unique_ptr<Texture2D> envFilteredTexture;
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 8> filteredUAVs;

	// Reflection capture

	struct alignas(16) UpdateCubemapCB
	{
		float3 CameraPosAdjustDelta;
		uint CapturePadding0;
		uint CaptureIndex;
		float CaptureDeltaTime;
		uint ResetCapture;
		uint pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(UpdateCubemapCB);
	static_assert(sizeof(UpdateCubemapCB) == 32);

	struct alignas(16) CaptureLightingState
	{
		float ReferenceLuminance;
		uint PendingResetMask;
		uint Reset;
		uint Initialized;
	};
	static_assert(sizeof(CaptureLightingState) == 16);
	std::unique_ptr<StructuredBuffer> captureLightingState;
	Util::LazyShader<ID3D11ComputeShader> detectCaptureLightingCS;
	Util::LazyShader<ID3D11ComputeShader> detectCaptureLightingReflectionsCS;
	float3 previousCaptureAnchor[2]{};
	float previousCaptureTime[2]{};



	Util::LazyShader<ID3D11ComputeShader> updateCubemapCS;
	Util::LazyShader<ID3D11ComputeShader> updateCubemapReflectionsCS;
	Util::LazyShader<ID3D11ComputeShader> updateCubemapFakeReflectionsCS;

	std::unique_ptr<ConstantBuffer> updateCubemapCB;

	Util::LazyShader<ID3D11ComputeShader> inferCubemapCS;
	Util::LazyShader<ID3D11ComputeShader> inferCubemapReflectionsCS;
	Util::LazyShader<ID3D11ComputeShader> inferCubemapFakeReflectionsCS;

	Texture2D* envCaptureTexture = nullptr;
	Texture2D* envCaptureRawTexture = nullptr;
	Texture2D* envCapturePositionTexture = nullptr;

	Texture2D* envCaptureReflectionsTexture = nullptr;
	Texture2D* envCaptureRawReflectionsTexture = nullptr;
	Texture2D* envCapturePositionReflectionsTexture = nullptr;

	Texture2D* envInferredTexture = nullptr;

	ID3D11ShaderResourceView* defaultCubemap = nullptr;

	bool realActiveReflections = false;
	bool activeReflections = false;
	bool fakeReflections = false;

	bool resetCapture[2] = { true, true };
	bool cubemapValid[2]{};
	bool recompileFlag = false;
	float previousHoursPassed = 0.0f;

	enum class NextTask
	{
		kCapture,
		kInferrence,
		kIrradiance,
		kBC6HCompress,
		kCapture2,
		kInferrence2,
		kIrradiance2,
		kBC6HCompress2
	};

	NextTask nextTask = NextTask::kCapture;


	// BC6H compression
	struct alignas(16) BC6HEncodeCB
	{
		uint TextureSizeInBlocksX;
		uint TextureSizeInBlocksY;
		uint MipLevel;
		uint pad;
	};
	STATIC_ASSERT_ALIGNAS_16(BC6HEncodeCB);

	Util::LazyShader<ID3D11ComputeShader> bc6hEncodeCS;
	std::unique_ptr<ConstantBuffer> bc6hEncodeCB;

	winrt::com_ptr<ID3D11ShaderResourceView> envFilteredTextureArraySRV;

	std::unique_ptr<Texture2D> envTextureBC6H;
	std::unique_ptr<Texture2D> envReflectionsTextureBC6H;
	std::unique_ptr<Texture2D> bc6hScratchTexture;
	std::unique_ptr<Texture2D> bc6hTailScratchTexture;

	uint32_t bc6hMipLevels = 0;
	uint32_t bc6hScratchMipLevels = 0;

	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 9> bc6hScratchUAVs;

	// Editor window

	struct Settings
	{
		uint EnabledCreator = false;
		uint EnabledSSR = true;
		uint CubemapResolution = kQualityCubemapResolution;
		uint pad0;
		float4 CubemapColor{ 1.0f, 1.0f, 1.0f, 0.0f };
	};

	struct alignas(16) CommonBufferData
	{
		uint Enabled = false;
		float MaxMipLevel = 7.0f;
		float pad0[2]{};
		float4 CubemapColor{ 1.0f, 1.0f, 1.0f, 0.0f };
	};
	STATIC_ASSERT_ALIGNAS_16(CommonBufferData);

	Settings settings;
	CommonBufferData GetCommonBufferData() const
	{
		auto data = CommonBufferData{};
		data.Enabled = settings.EnabledCreator;
		data.MaxMipLevel = static_cast<float>(activeCubemapMipLevels - 1);
		data.CubemapColor = settings.CubemapColor;
		return data;
	}

	/** @brief Sets the configured startup resolution when it is supported. */
	bool SetCubemapResolution(uint32_t a_resolution);
	/** @brief Returns the resolution requested before renderer resource creation. */
	uint32_t GetCubemapResolutionForResourceCreation();
	/** @brief Returns the resolution used by the current renderer resources. */
	uint32_t GetActiveCubemapResolution() const { return activeCubemapResolution; }
	/** @brief Returns the mip count used by the current renderer resources. */
	uint32_t GetActiveCubemapMipLevels() const { return activeCubemapMipLevels; }
	/** @brief Reports whether the configured resolution requires a restart. */
	bool IsCubemapResolutionRestartRequired() const { return settings.CubemapResolution != activeCubemapResolution; }
	bool IsSSRRuntimeActive() const;
	void UpdateCubemap();

	void PostDeferred();

	virtual inline std::string GetName() override { return "Dynamic Cubemaps"; }
	std::string GetDisplayName() override { return T("feature.dynamic_cubemaps.name", "Dynamic Cubemaps"); }
	virtual inline std::string GetShortName() override { return "DynamicCubemaps"; }
	virtual inline std::string_view GetShaderDefineName() override { return "DYNAMIC_CUBEMAPS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kMaterials; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			T("feature.dynamic_cubemaps.updates_reflections_to_match_the_surrounding_scene", "Updates reflections to match the surrounding scene."),
			{ T("feature.dynamic_cubemaps.key_feature_1", "Real-time environment capture for realistic reflections"),
				T("feature.dynamic_cubemaps.refreshes_stale_captures_after_substantial_li_6a26fb20", "Refreshes stale captures after substantial lighting changes"),
				T("feature.dynamic_cubemaps.key_feature_2", "Dynamic cube map generation based on camera position"),
				T("feature.dynamic_cubemaps.key_feature_3", "Enhanced water reflections with environmental details"),
				T("feature.dynamic_cubemaps.key_feature_4", "Optimized cubemap inference and irradiance calculation") }
		};
	}
	virtual std::vector<std::pair<std::string_view, std::string_view>> GetShaderDefineOptions() override;

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	virtual void SetupResources() override;
	virtual void Reset() override;

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual void DataLoaded() override;





	virtual void ClearShaderCache() override;
	/** @brief Returns the detector using the capture pass's sky-sampling policy. */
	ID3D11ComputeShader* GetComputeShaderDetectLighting(bool a_reflections);
	ID3D11ComputeShader* GetComputeShaderUpdate();
	ID3D11ComputeShader* GetComputeShaderUpdateReflections();
	ID3D11ComputeShader* GetComputeShaderUpdateFakeReflections();

	ID3D11ComputeShader* GetComputeShaderInferrence();
	ID3D11ComputeShader* GetComputeShaderInferrenceReflections();
	ID3D11ComputeShader* GetComputeShaderInferrenceFakeReflections();

	ID3D11ComputeShader* GetComputeShaderSpecularIrradiance();

	/** @brief Captures one cubemap stage, or returns false when its shader is unavailable. */
	bool UpdateCubemapCapture(bool a_reflections);

	/** @brief Runs cubemap inference, or returns false when its shader is unavailable. */
	bool Inferrence(bool a_reflections);

	/** @brief Filters cubemap irradiance, or returns false when its shader is unavailable. */
	bool Irradiance(bool a_reflections);

	/** @brief Encodes BC6H output, or returns false when its shader is unavailable. */
	bool CompressToBC6H(bool a_reflections);

	ID3D11ComputeShader* GetComputeShaderBC6HEncode();
	/** @brief Returns a completed float cubemap, or null until first publication. */
	ID3D11ShaderResourceView* GetPublishedCubemap(bool a_reflections = false) const;
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** @brief Queues a render-thread reset of both captures without GPU readback. */
	void RequestCaptureRefresh() { captureRefreshRequested.store(true, std::memory_order_release); }
#endif

	virtual bool IsCore() const override { return true; };

private:
	void SetupResourcesImpl();
	bool captureResourcesReady = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
	std::atomic_bool captureRefreshRequested{ false };
#endif
	bool SetupCapturePublicationResources(const D3D11_TEXTURE2D_DESC& a_desc, const D3D11_SHADER_RESOURCE_VIEW_DESC& a_srvDesc);
	bool SetupCompressionResources(const D3D11_TEXTURE2D_DESC& a_desc);
	static uint32_t SanitizeCubemapResolution(uint32_t a_resolution);
	void RefreshActiveCubemapResolution();

	uint32_t activeCubemapResolution = settings.CubemapResolution;
	uint32_t activeCubemapMipLevels = std::bit_width(activeCubemapResolution);
	bool cubemapResolutionLocked = false;
};
