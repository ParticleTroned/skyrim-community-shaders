#pragma once

#include "RememberedSizes.h"
#include <memory>
#include <optional>

namespace RE
{
	class NiSourceTexture;
}
namespace StreamingTextures::LoadHooks
{
	struct ReducedLoad
	{
		RE::NiSourceTexture* source = nullptr;
		std::string path;
		RememberedSize size;
		std::shared_ptr<void> lifetime;
		std::uint64_t serial = 0;
	};

	/** Install path tracking and open the bounded persistent size catalog. */
	void Install();
	/** Update load-time policy without accessing render-thread records from loader threads. */
	void Configure(bool enabled, const Modes& modes, const Options& options, const Categories& categories, std::uint32_t maximumDrop);
	/** Set independent preview-owner bits; reduced loads resume only after every owner clears. */
	void SetPreviewActive(bool active, std::uint32_t sourceMask = 1);
	/** Inspect the combined preview guard without locking the size catalog. */
	bool IsPreviewActive();
	/** Reserve bookkeeping before changing a DDS loader request; other mods' caps retain precedence. */
	std::optional<ReducedLoad> Prepare(ID3D11Device* device, std::uint64_t originalMaximum);
	/** Attach a successful reduced load to its source's creation scope. */
	void Complete(ReducedLoad load, ID3D11Resource* resource, bool success);
	/** Drain completed sources while retaining ownership until their normal restoration lifecycle takes over. */
	std::optional<ReducedLoad> Poll();
	/** Report queued ownership transfers without consuming their source references. */
	bool HasPending();
	/** Keep normal learning cadence while the catalog is unknown or can admit remembered reductions. */
	bool NeedsLearningRefresh();
	/** Store validated distance demand independently of the optional size cap. */
	void Remember(const std::string& path, const RememberedSize& size);
	/** Revoke eligibility for the rest of this session and remove the persistent entry. */
	void Forget(const std::string& path);
	/** Thread-safe state, persistence errors and load-time counters. */
	nlohmann::json Status();
}
