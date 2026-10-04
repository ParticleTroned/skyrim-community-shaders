#pragma once

#include <memory>

/** @brief Frame-local renderer retaining GPU buffers and material ownership, never deferred shape dereferences. */
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
	void RecordModel(RE::BSMultiStreamInstanceTriShape* shape, const char* path);
	void RemoveShape(RE::BSMultiStreamInstanceTriShape* shape);
	bool CaptureVisible(RE::BSMultiStreamInstanceTriShape* shape);
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
