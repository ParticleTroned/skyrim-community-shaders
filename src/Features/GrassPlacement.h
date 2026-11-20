#pragma once

#include "GrassPlacementSettings.h"
#include <array>
#include <cstddef>
#include <d3d11_1.h>
#include <winrt/base.h>

namespace RE
{
	class BSMultiStreamInstanceTriShape;
	namespace BSGraphics
	{
		struct VertexBuffer;
	}
}

namespace GrassPlacement
{
	/** Publish attached-cell rules at the frame boundary; no engine lookup occurs in a shader or draw. */
	void UpdateFrame();
	void SetHooksReady(bool ready);
	void RecordModel(RE::BSMultiStreamInstanceTriShape* shape, const char* path);
	/** Observe attached groups under the native group lock and maintain conservative native bounds. */
	void ObserveGroups(RE::BSMultiStreamInstanceTriShape* shape);
	void RemoveShape(RE::BSMultiStreamInstanceTriShape* shape);
	/** Restore native bounds before mutation; preserve surviving groups' temporal history. */
	void BeforeGroupMutation(RE::BSMultiStreamInstanceTriShape* shape, uint32_t removed = UINT32_MAX);
	/** Remove our envelope before native world-bound recomputation. */
	void RestoreWorldBound(RE::BSMultiStreamInstanceTriShape* shape);
	/** Reapply the bounded envelope after native world-bound recomputation. */
	void UpdateWorldBound(RE::BSMultiStreamInstanceTriShape* shape);
	/** The same current/previous offsets feed native draws and batched slices. */
	std::array<float, 2> GetOffsets(RE::BSGraphics::VertexBuffer* buffer);
	float BoundsPadding();
	/** Establish neutral native constants even when optional instance hooks are unavailable. */
	void PrepareNativeGeometry();
	/** Reassert constants only inside the owning grass draw, without another state flush. */
	void ApplyNativeBinding(bool compute);
	void BindNative(RE::BSGraphics::VertexBuffer* buffer);

	/** Own the borrowed vertex slot during a grass draw; restore its range and enclosing owner. */
	class BindingScope
	{
	public:
		BindingScope();
		~BindingScope();
		BindingScope(const BindingScope&) = delete;
		BindingScope& operator=(const BindingScope&) = delete;

	private:
		winrt::com_ptr<ID3D11DeviceContext> context;
		winrt::com_ptr<ID3D11DeviceContext1> rangedContext;
		winrt::com_ptr<ID3D11Buffer> previous;
		ID3D11DeviceContext* previousContext = nullptr;
		ID3D11Buffer* previousNative = nullptr;
		size_t previousDepth = 0;
		UINT first = 0, count = 0;
	};

	bool IsAvailable();
}
