#pragma once

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
	void PrepareGeometry(RE::BSRenderPass* pass);
	bool IsHookInstalled() const;
	/** @brief Whether native integration is installed without a latched rendering failure. */
	bool IsRenderingAvailable() const;
	void RecordModel(RE::BSMultiStreamInstanceTriShape* shape, const char* path);
	void MarkGroupsChanged(RE::BSMultiStreamInstanceTriShape* shape);
	void MarkGenerated(RE::BSMultiStreamInstanceTriShape* shape);
	void RemoveShape(RE::BSMultiStreamInstanceTriShape* shape);
	/** @brief Snapshot generated or culled grass without altering native visibility; only accepted groups renew residency. */
	bool CaptureVisible(RE::BSMultiStreamInstanceTriShape* shape, bool nativeVisible);
#ifdef DEVBENCH_BRIDGE_ENABLED
	void SetDiagnosticsEnabled(bool enabled);
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
