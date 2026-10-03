// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CSX contributors
#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

namespace CSX::WorldOverlayAPI
{
	inline constexpr char          ServiceName[] = "csx.vr.world_overlays";
	inline constexpr std::uint32_t Major = 1;
	inline constexpr std::uint32_t MaxClients = 4;
	inline constexpr std::uint32_t MaxQuads = 64;
	inline constexpr std::uint32_t MaxAtlasDimension = 4096;
	inline constexpr std::uint32_t MaxAtlasPixels = 4096 * 2048;
	inline constexpr double        MaxWorldCoordinate = 1e9;
	inline constexpr float         MaxQuadDimension = 100000;
	using Client = std::uint64_t;

	enum class Status : std::uint32_t
	{
		Success,
		InvalidArgument,
		StructureTooSmall,
		UnsupportedRuntime,
		NotReady,
		WrongThread,
		Busy,
		Stale,
		ResourceFailure,
		InvalidClient
	};
	enum class Occlusion : std::uint32_t
	{
		SceneDepth,
		Disabled
	};

#pragma pack(push, 8)
	/** Status is also returned by BeginAtlasUpdate and PublishBatch, without a polling loop.
	 * ready describes the latest completed capture; Begin/Publish recheck current admission.
	 */
	struct Capabilities001
	{
		std::uint32_t structSize = sizeof(Capabilities001);
		std::uint32_t ready = 0;
		std::uint64_t worldEpoch = 0;
		std::uint64_t resourceGeneration = 0;
		std::uint32_t maxQuads = MaxQuads;
		std::uint32_t maxAtlasDimension = MaxAtlasDimension;
		std::uint32_t maxAtlasPixels = MaxAtlasPixels;
		std::uint32_t atlasFormat = 28;  // DXGI_FORMAT_R8G8B8A8_UNORM, premultiplied alpha.
		/// Per-client receipt on Begin/Publish: both eyes were accepted recently in this epoch.
		std::uint32_t presentationReady = 0;
		std::uint32_t presentedFrame = 0;
		std::uint64_t presentedSequence = 0;
	};
	struct AtlasRequest001
	{
		std::uint32_t        structSize = sizeof(AtlasRequest001);
		std::uint32_t        width = 0;
		std::uint32_t        height = 0;
		std::uint32_t        reserved = 0;
		ID3D11Device*        device = nullptr;
		ID3D11DeviceContext* immediateContext = nullptr;
	};
	/** Borrowed target, valid until PublishBatch, CancelAtlasUpdate or render-thread resource invalidation.
	 * A lease is exclusive. Fully redraw it; alternate pages do not preserve prior pixels.
	 * Restore all D3D state before publication. Never retain or write the RTV afterward.
	 */
	struct AtlasLease001
	{
		std::uint32_t           structSize = sizeof(AtlasLease001);
		std::uint32_t           width = 0;
		std::uint32_t           height = 0;
		std::uint32_t           format = 28;
		std::uint64_t           contentRevision = 0;
		std::uint64_t           worldEpoch = 0;
		std::uint64_t           resourceGeneration = 0;
		ID3D11RenderTargetView* target = nullptr;
	};
	/** Ordered camera-facing quad. Center and dimensions use Skyrim world units.
	 * The stereo pair shares one billboard basis. UVs have a top-left origin.
	 */
	struct Quad001
	{
		std::uint64_t id = 0;
		double        center[3]{};
		float         width = 0;
		float         height = 0;
		float         uv[4]{ 0, 0, 1, 1 };
		float         opacity = 1;
		Occlusion     occlusion = Occlusion::SceneDepth;
	};
	/** Atomic pixels + ordered metadata publication. A new revision consumes its lease;
	 * the last committed revision allows placement-only updates. Sequence must increase.
	 * Content expires after two scene frames without publication, including while paused:
	 * a visible producer must refresh placement; hiding/loading must ClearBatch explicitly.
	 */
	struct Batch001
	{
		std::uint32_t  structSize = sizeof(Batch001);
		std::uint32_t  count = 0;
		std::uint64_t  sequence = 0;
		std::uint64_t  contentRevision = 0;
		std::uint64_t  worldEpoch = 0;
		std::uint64_t  resourceGeneration = 0;
		const Quad001* quads = nullptr;
	};
	/** Windows x64 function table; no exceptions cross this boundary.
	 * Register/unregister, clear and status may run on any thread. Begin/update/publish/
	 * cancel run on the current scene-render thread, after completed opaque rendering
	 * (e.g. HUDMenu::PostDisplay), using the game's immediate context. No client callback
	 * is invoked. A Busy/NotReady result requires no waiting. Unregister stops admission;
	 * CSX retains any already frozen pair until its commands have been queued. Clear invalidates
	 * publication but does not release a live write lease; cancel it. Unregister returns Busy
	 * while a write lease exists. Resource invalidation is serialized on the render thread.
	 */
	struct Interface001
	{
		std::uint32_t structSize = sizeof(Interface001);
		std::uint32_t major = Major;
		Status (*RegisterClient)(const char* name, Client* output) = nullptr;
		Status (*GetCapabilities)(Capabilities001* output) = nullptr;
		Status (*BeginAtlasUpdate)(Client client, const AtlasRequest001* request, AtlasLease001* output, Capabilities001* status) = nullptr;
		Status (*PublishBatch)(Client client, const Batch001* batch, Capabilities001* status) = nullptr;
		Status (*CancelAtlasUpdate)(Client client, std::uint64_t revision) = nullptr;
		Status (*ClearBatch)(Client client) = nullptr;
		Status (*UnregisterClient)(Client client) = nullptr;
	};
#pragma pack(pop)
	static_assert(sizeof(void*) == 8, "The world-overlay ABI requires Windows x64");
	static_assert(sizeof(Quad001) == 64);
	static_assert(sizeof(Batch001) == 48);
}
