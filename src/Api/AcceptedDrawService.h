#pragma once

#include "Api/AcceptedDrawRegistry.h"

namespace RE
{
	class BSRenderPass;
}

namespace CSX::Api
{
	/** Called at renderer initialization, before scene submission can begin. */
	void InitializeAcceptedDrawService(ID3D11DeviceContext* a_context);
	/** Mark attribution hooks installed before renderer initialization can expose readiness. */
	void AcceptedDrawGeometryHooksInstalled();
	/** Enter setup with attribution hidden until the native setup has completed. */
	void BeginAcceptedDrawGeometry(RE::BSRenderPass* a_pass);
	/** Associate the current verified scope with its native borrowed geometry. */
	void ActivateAcceptedDrawGeometry(RE::BSRenderPass* a_pass);
	/** Hide attribution before native restoration can submit further draws. */
	void SuspendAcceptedDrawGeometry();
	/** Unwind a verified pass or count and invalidate an unmatched scope. */
	void EndAcceptedDrawGeometry(RE::BSRenderPass* a_pass);
	/** Borrow the live native geometry, or null during setup, restoration and replays. */
	const void* GetCurrentAcceptedDrawGeometry() noexcept;
	/** Publish only attributed immediate-context draws against the current main scene depth. */
	void PublishAcceptedDraw(ID3D11DeviceContext* a_context,
		const CSXAcceptedDrawAPI::Arguments& a_arguments, AcceptedDrawRegistry::NativeReplay a_replay) noexcept;
	/** At the renderer's completed-frame boundary, retire this frame's thread owner.
	 * The first eligible main-scene draw in the next frame claims ownership again.
	 * Never call from a draw callback or an asynchronous load/menu notification.
	 */
	void AdvanceAcceptedDrawFrame(ID3D11DeviceContext* a_context) noexcept;
	/** Redirected UI capture is never a main-scene accepted draw. */
	class SuppressAcceptedDraw
	{
	public:
		SuppressAcceptedDraw();
		~SuppressAcceptedDraw();
		SuppressAcceptedDraw(const SuppressAcceptedDraw&) = delete;
		SuppressAcceptedDraw& operator=(const SuppressAcceptedDraw&) = delete;
	};
	/** Return the process-lifetime table; registration independently checks runtime readiness. */
	const CSXAcceptedDrawAPI::API* GetAcceptedDrawAPI();
	/** Advertise v1 through the shared service registry on VR only. */
	void RegisterAcceptedDrawService();
	struct AcceptedDrawStatus
	{
		bool ready;
		uint64_t filteredDraws, wrongThreadDraws, geometryScopeErrors;
		AcceptedDrawRegistry::Statistics registry;
	};
	/** Read readiness and best-effort counters without enabling capture or taking GPU state. */
	AcceptedDrawStatus InspectAcceptedDrawService();
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Inspect active callback identities for an explicit DevBench diagnostic request. */
	bool InspectAcceptedDrawObservers(std::array<AcceptedDrawRegistry::ObserverSnapshot, 8>& a_output);
#endif
}
