#pragma once

#include "MeshBlendingLandscapePolicy.h"

#include <atomic>
#include <shared_mutex>

namespace RE
{
	class TESObjectLAND;
	class BSRenderPass;
}

/** LAND identity capture owned by Mesh Blending; it has no feature or rule-file lifecycle. */
class MeshBlendingLandscape
{
public:
	using Policy = CSX::MeshBlendingLandscapePolicy::CompiledPolicy;
	using Texture = CSX::MeshBlendingLandscapePolicy::CapturedTexture;
	using Kind = CSX::MeshBlendingLandscapePolicy::MaterialKind;

	struct DiscoverySink
	{
		void* context = nullptr;
		void (*capture)(void*, const Texture&, Kind) = nullptr;
	};

	MeshBlendingLandscape();

	/** Publish a fully staged policy without allocating or discarding current LAND identities. */
	void PublishPolicy(std::shared_ptr<const Policy> a_policy) noexcept;
	/** Return the expanded pair count for the existing Mesh Blending policy UI. */
	[[nodiscard]] std::size_t GetBlendPairCount() const noexcept;

	/** Run after the final vanilla/Terrain Helper/True PBR material setup, including failed recaptures. */
	void Capture(RE::TESObjectLAND* a_land, bool a_materialSetupSucceeded, DiscoverySink a_discovery = { nullptr, nullptr });

	/** Validate the current geometry, owner publication and material before resolving six classes. */
	[[nodiscard]] bool TryGetClasses(RE::BSRenderPass* a_pass, std::uint32_t& a_classes);

private:
	bool CaptureQuadrant(RE::TESObjectLAND* a_land, std::size_t a_quadrant, const DiscoverySink& a_discovery);

	std::atomic<std::shared_ptr<const Policy>> policy;
	std::shared_mutex metadataMutex;
	std::uint64_t session = 0u;
	std::uint64_t nextPublication = 1u;
};
