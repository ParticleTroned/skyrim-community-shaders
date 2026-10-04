#pragma once

#include "Buffer.h"
#include "GrassDrawBatch.h"
#include "Utils/LazyShader.h"

#include <atomic>
#include <d3d11_1.h>

/** @brief Combine visible instance groups within one native grass draw. */
struct GrassOptimizations : Feature
{
	std::string GetName() override { return "Grass Optimizations"; }
	std::string GetShortName() override { return "GrassOptimizations"; }
	std::string_view GetCategory() const override { return FeatureCategories::kFoliage; }
	std::string_view GetShaderDefineName() override { return "GRASS_OPTIMIZATIONS"; }
	bool HasShaderDefine(RE::BSShader::Type type) override { return type == RE::BSShader::Type::Grass; }
	bool SupportsVR() override { return true; }
	std::string_view GetShaderCacheAbiVersion() override { return "native-group-batch-v1"; }
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	void DrawSettings() override;
	void LoadSettings(json& settings) override;
	void SaveSettings(json& settings) override;
	void RestoreDefaultSettings() override;
	void SetupResources() override;
	void ClearShaderCache() override;
	void PostPostLoad() override;

	/** @brief Stage a toggle without invalidating the current native draw loop. */
	void SetEnabled(bool enabled);
	bool IsEnabled() const { return requestedEnabled.load(std::memory_order_relaxed); }
	bool IsHookInstalled() const { return hookInstalled; }

	/** @brief Snapshot only this pass's visible GPU groups; no shape survives the callback. */
	void PrepareGeometry(RE::BSRenderPass* pass);

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** @brief Enable cumulative CPU counters without adding GPU readbacks. */
	void SetDiagnosticsEnabled(bool enabled);
	json GetDiagnostics() const;
#endif

private:
	using NativeDraw = void (*)(RE::BSGraphics::Renderer*, RE::BSGraphics::TriShape*, uint32_t,
		uint32_t, uint32_t, RE::BSGraphics::VertexDesc, RE::BSGraphics::VertexBuffer*);
	static void DrawGroup(RE::BSGraphics::Renderer* renderer, RE::BSGraphics::TriShape* mesh,
		uint32_t firstTriangle, uint32_t triangles, uint32_t instances,
		RE::BSGraphics::VertexDesc descriptor, RE::BSGraphics::VertexBuffer* buffer);
	static inline REL::Relocation<NativeDraw> originalDraw;

	struct SourceGroup
	{
		RE::BSGraphics::VertexBuffer* identity;
		winrt::com_ptr<ID3D11Buffer> buffer;
		uint32_t instances;
		bool consumed = false;
	};
	bool PrepareBatch();
	bool EnsureCapacity(uint32_t instances);
	bool HasBatchShader() const;
	void LatchFailure(const char* operation, HRESULT result);
	void ForgetGeometry();

	std::atomic_bool requestedEnabled{ false };
	std::atomic_uint32_t requestedGeneration{ 0 };
	uint32_t resourceGeneration = 0;
	bool hookInstalled = false;
	bool resourceFailure = false;
	bool batchDrawn = false;
	RE::BSGraphics::TriShape* meshIdentity = nullptr;
	uint64_t vertexDescriptor = 0;
	uint32_t triangleCount = 0;
	uint32_t instanceCount = 0;
	size_t groupCursor = 0;
	std::vector<SourceGroup> groups;
	std::vector<GrassDrawBatch::Group> groupCounts;
	std::vector<GrassDrawBatch::Range> ranges;
	Util::LazyShader<ID3D11ComputeShader> fadeShader;
	winrt::com_ptr<ID3D11DeviceContext1> context1;
	std::unique_ptr<Buffer> disabledConstants;
	std::unique_ptr<Buffer> enabledConstants;
	std::unique_ptr<Buffer> dispatchConstants;
	std::unique_ptr<Buffer> groupTable;
	std::unique_ptr<Buffer> mergedInstances;
	std::unique_ptr<Buffer> instanceFades;
	RE::BSGraphics::VertexBuffer mergedVertexBuffer{};
	uint32_t capacity = 0;

#ifdef DEVBENCH_BRIDGE_ENABLED
	std::atomic_bool diagnosticsEnabled{ false };
	std::atomic_uint64_t batchedDraws{ 0 }, combinedGroups{ 0 }, combinedInstances{ 0 };
	std::atomic_uint64_t nativeDraws{ 0 }, shaderFallbacks{ 0 }, capacityFallbacks{ 0 }, resourceFallbacks{ 0 };
#endif
};
