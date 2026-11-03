#pragma once

#include "Feature.h"
#include "Utils/DepthConvention.h"
#include <atomic>
#include <mutex>

namespace globals
{
	struct FrameBuffer;
	struct FrameBufferVR;
}

struct ReverseZ : Feature
{
	std::string GetName() override { return "Reverse Z"; }
	std::string GetShortName() override { return "ReverseZ"; }
	std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }
	bool IsCore() const override { return true; }
	bool IsInMenu() const override { return false; }
	bool SupportsVR() override { return true; }
	std::string_view GetShaderDefineName() override { return "REVERSE_Z"; }
	bool HasShaderDefine(RE::BSShader::Type) override;
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	struct Settings
	{
		bool EnableReverseZBuffer = true;
	};
	Settings settings;
	void DrawSettings() override;
	void LoadSettings(json&) override;
	void SaveSettings(json&) override;
	void RestoreDefaultSettings() override;
	void SetupResources() override;
	void DataLoaded() override;
	void PostPostLoad() override;
	/** Install on the renderer immediate context before engine shader creation. */
	void InstallRuntimeHooks();
	/** Requested and effective states remain separate until preparation commits. */
	json GetDiagnostics() const;
	bool IsActive() const { return activeThisBoot.load(std::memory_order_acquire); }
	Depth::Convention GetConvention() const { return IsActive() ? Depth::Convention::Reversed : Depth::Convention::Standard; }
	uint64_t GetGeneration() const { return generation.load(std::memory_order_acquire); }
	/** Actual extra scene-target storage, normalized for existing resolution planning. */
	double GetAdditionalDepthBytesPerPixel() const { return extraDepthBytesPerPixel.load(std::memory_order_acquire); }
	float GetFarDepth() const { return Depth::Far(GetConvention()); }
	float GetNearDepth() const { return Depth::Near(GetConvention()); }
	/** Runs under the recreation lock before engine target ownership reconciliation. */
	bool PrepareDepthTargets();
	/** Registers feature-owned views in the current scene-depth generation. */
	void RegisterSceneDepthView(ID3D11DepthStencilView*);
	/** Releases a feature-owned view's retained identity before its owner recreates it. */
	void UnregisterSceneDepthView(ID3D11DepthStencilView*);
	void FixupMappedFrameBuffer(globals::FrameBuffer&);
	/** Returns whether reversed camera normalization ran and needs uploading. */
	bool FixupMappedFrameBufferVR(globals::FrameBufferVR&);
	void ApplyReverseProjection(void*, const RE::NiCamera*);
	void ReversePublishedProjection();
	bool ExpectPublishedReversal(const RE::NiCamera*, bool) const;
	bool IsConvertedDepthTarget(uint32_t) const;
	bool IsReverseDepthView(ID3D11DepthStencilView*) const;
	ID3D11DepthStencilView* ResolveCubemapFaceDepthView(ID3D11RenderTargetView*, ID3D11DepthStencilView*) const;
	ID3D11DepthStencilState* GetReversedState(ID3D11DepthStencilState*);
	ID3D11RasterizerState* GetReversedRasterizerState(ID3D11RasterizerState*);

private:
	void RecordFailure(const char* reason, const char* phase, uint64_t targetGeneration);
	bool RejectDepthPreparation(const char* reason);
	void RejectRendering(const char* reason);
	void LatchBootState();
	bool PrepareNativeShaders();
	bool bootLatched = false, requestedThisBoot = false, activationRejectedThisBoot = false;
	std::atomic_bool activeThisBoot{ false };
	bool depthTargetsConverted = false, runtimeHooksInstalled = false, cameraHooksInstalled = false;
	uint32_t convertedTargetMask = 0;
	std::atomic_uint64_t generation{ 0 };
	std::atomic<double> extraDepthBytesPerPixel{ 0 };
	mutable std::mutex failureMutex;
	std::string failure;
	const char* failurePhase = "none";
	uint64_t failureGeneration = 0;
};
