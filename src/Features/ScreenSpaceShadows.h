#pragma once

#include "Buffer.h"
#include "Utils/LazyShader.h"

struct ScreenSpaceShadows : Feature
{
public:
	virtual inline std::string GetName() override { return "Screen Space Shadows"; }
	virtual std::string GetDisplayName() override { return T("feature.screen_space_shadows.name", "Screen Space Shadows"); }
	virtual inline std::string GetShortName() override { return "ScreenSpaceShadows"; }
	virtual inline std::string_view GetShaderDefineName() override { return "SCREEN_SPACE_SHADOWS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	/** @brief Returns a localized description and list of key features for the UI summary panel. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.screen_space_shadows.description", "Screen Space Shadows enhances shadow quality by adding detailed contact shadows and improving shadow accuracy.\nThis technique adds fine-detail shadows that traditional shadow mapping might miss."),
			{ T("feature.screen_space_shadows.key_feature_1", "Enhanced contact shadows"),
				T("feature.screen_space_shadows.key_feature_2", "Improved shadow detail"),
				T("feature.screen_space_shadows.key_feature_3", "Better shadow accuracy"),
				T("feature.screen_space_shadows.key_feature_4", "Fine-scale shadow effects"),
				T("feature.screen_space_shadows.key_feature_5", "Configurable shadow contrast") } };
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	struct BendSettings
	{
		float SurfaceThickness = 0.02f;
		float BilinearThreshold = 0.02f;
		float ShadowContrast = 1.0f;
		uint Enable = 1;
		uint SampleCount = 1;
		uint pad0[3];
	};

	BendSettings bendSettings;

	struct alignas(16) RaymarchCB
	{
		// Runtime data returned from BuildDispatchList():
		float LightCoordinate[4];  // Values stored in DispatchList::LightCoordinate_Shader by BuildDispatchList()
		int WaveOffset[2];         // Values stored in DispatchData::WaveOffset_Shader by BuildDispatchList()

		// Renderer Specific Values:
		float FarDepthValue;   // Set to the Depth Buffer Value for the far clip plane, as determined by renderer projection matrix setup (typically 0).
		float NearDepthValue;  // Set to the Depth Buffer Value for the near clip plane, as determined by renderer projection matrix setup (typically 1).

		// Sampling data:
		float InvDepthTextureSize[2];  // Inverse of the texture dimensions for 'DepthTexture' (used to convert from pixel coordinates to UVs)
									   // If 'PointBorderSampler' is an Unnormalized sampler, then this value can be hard-coded to 1.
									   // The 'USE_HALF_PIXEL_OFFSET' macro might need to be defined if sampling at exact pixel coordinates isn't precise (e.g., if odd patterns appear in the shadow).

		float2 DynamicRes;

		BendSettings settings;
	};
	STATIC_ASSERT_ALIGNAS_16(RaymarchCB);

	ID3D11SamplerState* pointBorderSampler = nullptr;

	ConstantBuffer* raymarchCB = nullptr;
	Util::LazyShader<ID3D11ComputeShader> raymarchCS;

	Texture2D* screenSpaceShadowsTexture = nullptr;

	/** @brief Creates the raymarch constant buffer, point border sampler, and shadow output texture. */
	virtual void SetupResources() override;

	/** @brief Draws the ImGui settings UI for screen-space shadow configuration. */
	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual PerformanceTuningConfig GetPerformanceTuningConfig() const override
	{
		return { 1,
			T("menu.performance_tuning.feature.screen_space_shadows.comparison_label", "Off"),
			T("menu.performance_tuning.feature.screen_space_shadows.comparison_details", "Screen Space Shadows are switched off.") };
	}
	virtual bool IsPerformanceTuningApplicable() const override;
	virtual const char* GetPerformanceTuningApplicabilityReason() const override;
	virtual json GetPerformanceTuningUserSettingsMask() const override
	{
		return {
			{ "Enable", true },
			{ "SampleCount", true },
			{ "SurfaceThickness", true },
			{ "BilinearThreshold", true },
			{ "ShadowContrast", true }
		};
	}
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override { return bendSettings.Enable != 0; }
	virtual bool IsPerformanceCostMeasurementReady() const override;
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override
	{
		if (a_enabled) {
			bendSettings = BendSettings{};
			return;
		}

		bendSettings.Enable = 0u;
	}
	virtual json CapturePerformanceCostMeasurementState() const override { return CapturePerformanceSettingsState(); }
	virtual void RestorePerformanceCostMeasurementState(const json& a_state) override
	{
		auto state = a_state;
		LoadSettings(state);
	}

	/** @brief Releases the compiled raymarch compute shader for recompilation. */
	virtual void ClearShaderCache() override;
	/** @brief Releases the raymarch compute shader so it is recompiled on next use. */
	void InvalidateRaymarchShaders();
	/** @brief Calculates the resolution-scaled and quantized sample count for the raymarch shader. */
	uint GetScaledSampleCount() const;
	uint lastCompiledSampleCount = 0;
	/**
	 * @brief Returns the compiled raymarch compute shader, recompiling if the sample count changed.
	 * @return The compiled ID3D11ComputeShader, or nullptr on failure.
	 */
	ID3D11ComputeShader* GetComputeRaymarch();

	/** @brief Clears the shadow texture and dispatches shadow ray marching if conditions are met. */
	virtual void Prepass() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	/** @brief Dispatches the Bend SSS compute shader to generate screen-space contact shadows. */
	void DrawShadows();

	virtual void RestoreDefaultSettings() override;

private:
	enum class RuntimeReadiness
	{
		Ready,
		NoFullSky,
		NoRuntimeResources,
		NoDirectionalLight,
		ShaderUnavailable
	};

	struct RuntimeContext
	{
		ID3D11DeviceContext* context = nullptr;
		ID3D11ShaderResourceView* depthSRV = nullptr;
		ID3D11ComputeShader* raymarchShader = nullptr;
		RE::NiDirectionalLight* directionalLight = nullptr;
		float2 renderSize{};
		float2 dynamicResolution{};
	};

	/**
	 * @brief Checks every runtime prerequisite without creating or changing resources.
	 *
	 * Execution calls this before and after shader compilation. Applicability uses
	 * the side-effect-free resource/scene form; enabled measurement legs additionally
	 * require the shader compiled for the current dynamic resolution.
	 */
	RuntimeReadiness GetRuntimeReadiness(bool a_requireCompiledShader, RuntimeContext* a_context = nullptr) const;
};
