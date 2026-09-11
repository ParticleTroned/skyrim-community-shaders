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
	void AcceptedDrawGeometryHooksInstalled();
	void BeginAcceptedDrawGeometry(RE::BSRenderPass* a_pass);
	void ActivateAcceptedDrawGeometry(RE::BSRenderPass* a_pass);
	void SuspendAcceptedDrawGeometry();
	void EndAcceptedDrawGeometry(RE::BSRenderPass* a_pass);
	void PublishAcceptedDraw(ID3D11DeviceContext* a_context,
		const CSXAcceptedDrawAPI::Arguments& a_arguments, AcceptedDrawRegistry::NativeReplay a_replay) noexcept;
	/** Redirected UI capture is never a main-scene accepted draw. */
	class SuppressAcceptedDraw
	{
	public:
		SuppressAcceptedDraw();
		~SuppressAcceptedDraw();
		SuppressAcceptedDraw(const SuppressAcceptedDraw&) = delete;
		SuppressAcceptedDraw& operator=(const SuppressAcceptedDraw&) = delete;
	};
	const CSXAcceptedDrawAPI::API* GetAcceptedDrawAPI();
	void RegisterAcceptedDrawService();
	struct AcceptedDrawStatus
	{
		bool ready;
		uint64_t filteredDraws, wrongThreadDraws, geometryScopeErrors;
		AcceptedDrawRegistry::Statistics registry;
	};
	AcceptedDrawStatus InspectAcceptedDrawService();
}
