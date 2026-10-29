#pragma once

#include "GrassRuntime.h"
#include <memory>

/** @brief Batches visible grass and retains unchanged GPU instance records across frames. */
class GrassBucketRenderer
{
public:
	GrassBucketRenderer();
	~GrassBucketRenderer();
	void InstallHooks();
	void SetupResources();
	void ClearShaderCache();
	/** @brief Preserve geometry inputs and pre-setup wind history for batching at the native draw boundary. */
	void PrepareGeometry(RE::BSRenderPass*, RE::BSShader*, uint32_t, GrassRuntime::SetupGeometry, std::optional<uint32_t> nativeWindTimer);
	bool IsHookInstalled() const;
	/** @brief Whether native integration is installed without a latched rendering failure. */
	bool IsRenderingAvailable() const;
	void RecordModel(RE::BSMultiStreamInstanceTriShape* shape, const char* path);
	/** @brief Invalidate records and retire a removed group's fade before its storage is reused. */
	void MarkGroupsChanged(RE::BSMultiStreamInstanceTriShape* shape, std::optional<uint32_t> removedGroup = std::nullopt);
	void MarkGenerated(RE::BSMultiStreamInstanceTriShape* shape);
	/** @brief Record native creation independently of capture readiness; replacements receive a new spawn time. */
	void RecordGeneratedGroups(RE::BSMultiStreamInstanceTriShape* shape, std::span<const uint32_t> groupIndices, bool replace = false);
	void RemoveShape(RE::BSMultiStreamInstanceTriShape* shape);
	/** @brief Snapshot generated grass without changing visibility; generation hooks invalidate retained records. */
	bool CaptureVisible(RE::BSMultiStreamInstanceTriShape* shape, bool nativeVisible);
	/** @brief Queue one compatible representative, retaining native fallback for every member. */
	bool QueueVisible(RE::BSMultiStreamInstanceTriShape*, RE::NiCullingProcess*, int32_t);
	using RenderPass = void (*)(RE::BSRenderPass*, uint32_t, bool, uint32_t);
	/** @brief Expand a failed representative draw through the original native pass route. */
	void DrawRenderPass(RE::BSRenderPass*, uint32_t, bool, uint32_t, RenderPass);
#ifdef DEVBENCH_BRIDGE_ENABLED
	void SetDiagnosticsEnabled(bool enabled);
	/** @brief Request a rate-limited render-thread snapshot without enabling shader counters. */
	void RequestTelemetry();
	json GetDiagnostics() const;
#endif
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
	using NativeDraw = void (*)(RE::BSGraphics::Renderer*, RE::BSGraphics::TriShape*, uint32_t, uint32_t, uint32_t,
		RE::BSGraphics::VertexDesc, RE::BSGraphics::VertexBuffer*);
	static void DrawGroup(RE::BSGraphics::Renderer*, RE::BSGraphics::TriShape*, uint32_t, uint32_t, uint32_t,
		RE::BSGraphics::VertexDesc, RE::BSGraphics::VertexBuffer*);
	static inline REL::Relocation<NativeDraw> originalDraw;
};
