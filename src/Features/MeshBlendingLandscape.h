#pragma once

#include "MeshBlendingLandscapePolicy.h"

#include <atomic>
#include <mutex>
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
	using Texture = CSX::MeshBlendingLandscapePolicy::TextureView;
	using Kind = CSX::MeshBlendingLandscapePolicy::MaterialKind;
	enum class Rejection : std::uint8_t
	{
		None,
		MissingLand,
		MaterialSetup,
		ChildLayout,
		MissingGeometry,
		MultipleGeometries,
		ShaderProperty,
		MaterialLayout,
		LayerCount,
		MissingTexture,
		TextureIdentity,
		SnapshotEncoding,
		MetadataAllocation,
		MetadataPublication,
		MissingSnapshot,
		SnapshotDecoding,
		StalePublication,
		Exception,
		Count
	};
	static constexpr std::array<std::string_view, static_cast<std::size_t>(Rejection::Count)> RejectionNames{
		"none", "missingLand", "materialSetup", "childLayout", "missingGeometry", "multipleGeometries",
		"shaderProperty", "materialLayout", "layerCount", "missingTexture", "textureIdentity",
		"snapshotEncoding", "metadataAllocation", "metadataPublication", "missingSnapshot", "snapshotDecoding", "stalePublication", "exception"
	};

	struct DiscoverySink
	{
		void* context = nullptr;
		void (*capture)(void*, const Texture&, Kind) = nullptr;
		std::uint64_t epoch = 0u;
	};

	MeshBlendingLandscape();

	/** Publish a fully staged policy without allocating or discarding current LAND identities. */
	void PublishPolicy(std::shared_ptr<const Policy> a_policy) noexcept;
	/** Return the expanded pair count for the existing Mesh Blending policy UI. */
	[[nodiscard]] std::size_t GetBlendPairCount() const noexcept;
	/** A recording restart revisits resident snapshots while keeping storage bounded. */
	void BeginDiscoverySession() noexcept;
	[[nodiscard]] std::uint64_t GetDiscoveryEpoch() const noexcept { return discoveryEpoch.load(std::memory_order_acquire); }

	/** Run after the final vanilla/Terrain Helper/True PBR material setup, including failed recaptures. */
	void Capture(RE::TESObjectLAND* a_land, bool a_materialSetupSucceeded, DiscoverySink a_discovery = { nullptr, nullptr, 0u });

	/** Validate the current geometry, owner publication and material before resolving six classes. */
	[[nodiscard]] bool TryGetClasses(RE::BSRenderPass* a_pass, std::uint32_t& a_classes,
		DiscoverySink a_discovery = { nullptr, nullptr, 0u }, bool a_classify = true);

#ifdef DEVBENCH_BRIDGE_ENABLED
	struct RejectionDetail
	{
		Rejection reason = Rejection::None;
		std::uint32_t quadrant = 4u;
		std::uint32_t layer = 6u;
		std::string expectedTexture;
		std::string boundTexture;
	};
	struct Diagnostics
	{
		std::uint64_t captureAttempts = 0u;
		std::uint64_t captureSuccesses = 0u;
		std::uint64_t drawAttempts = 0u;
		std::uint64_t drawSuccesses = 0u;
		std::uint64_t unknownLayerDraws = 0u;
		std::uint64_t rejectedPairDraws = 0u;
		std::uint64_t zeroClassDraws = 0u;
		std::uint64_t seamBypasses = 0u;
		std::array<std::uint64_t, static_cast<std::size_t>(Rejection::Count)> captureRejects{};
		std::array<std::uint64_t, static_cast<std::size_t>(Rejection::Count)> drawRejects{};
		RejectionDetail lastCaptureRejection;
		RejectionDetail lastDrawRejection;
	};
	[[nodiscard]] Diagnostics GetDiagnostics() const;
	void ResetDiagnostics();
	void RecordSeamBypass();
#endif

private:
	Rejection CaptureQuadrant(RE::TESObjectLAND* a_land, std::size_t a_quadrant,
		bool a_materialSetupSucceeded, const DiscoverySink& a_discovery);
#ifdef DEVBENCH_BRIDGE_ENABLED
	void RecordRejection(bool a_capture, Rejection a_reason, std::uint32_t a_quadrant = 4u,
		std::uint32_t a_layer = 6u, std::string_view a_expected = {}, std::string_view a_bound = {});
	mutable std::mutex diagnosticsMutex;
	Diagnostics diagnostics;
#endif

	std::atomic<std::shared_ptr<const Policy>> policy;
	std::shared_mutex metadataMutex;
	std::uint64_t session = 0u;
	std::uint64_t nextPublication = 1u;
	std::atomic_uint64_t discoveryEpoch = 1u;
};
