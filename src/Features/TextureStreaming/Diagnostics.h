#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace StreamingTextures
{
	/** Counts observations, not unique textures; increments need no allocation or formatting. */
	struct Diagnostics
	{
		enum class Exclusion : std::uint8_t
		{
			MissingTexture,
			MissingProvenance,
			ProtectedConsumer,
			InventoryLimit,
			ProtectedPath,
			TextureContract,
			CategoryDisabled,
			Count
		};
		enum class Block : std::uint8_t
		{
			WorldLoading,
			Transition,
			OriginUnavailable,
			StaleBudget,
			InventoryIncomplete,
			ProtectedConsumer,
			ConsumerContract,
			FullDetailDemand,
			DemandSettling,
			RetryBackoff,
			PriorityWork,
			ReplacementHeadroom,
			RecoveryHeadroom,
			ReducedLimit,
			SourceChanged,
			ReaderPending,
			UploadPending,
			RetirementPending,
			RetirementFailed,
			CategoryDisabled,
			Count
		};
		static constexpr std::array exclusionNames{
			std::string_view("missingTexture"), std::string_view("missingProvenance"), std::string_view("protectedConsumer"),
			std::string_view("inventoryLimit"), std::string_view("protectedPath"), std::string_view("textureContract"), std::string_view("categoryDisabled")
		};
		static constexpr std::array blockNames{
			std::string_view("worldLoading"), std::string_view("transition"), std::string_view("originUnavailable"),
			std::string_view("staleBudget"), std::string_view("inventoryIncomplete"), std::string_view("protectedConsumer"),
			std::string_view("consumerContract"), std::string_view("fullDetailDemand"), std::string_view("demandSettling"),
			std::string_view("retryBackoff"), std::string_view("priorityWork"), std::string_view("replacementHeadroom"),
			std::string_view("recoveryHeadroom"), std::string_view("reducedLimit"), std::string_view("sourceChanged"),
			std::string_view("readerPending"), std::string_view("uploadPending"), std::string_view("retirementPending"), std::string_view("retirementFailed"), std::string_view("categoryDisabled")
		};
		static_assert(exclusionNames.size() == static_cast<std::size_t>(Exclusion::Count));
		static_assert(blockNames.size() == static_cast<std::size_t>(Block::Count));
		std::array<std::uint64_t, exclusionNames.size()> excluded{};
		std::array<std::uint64_t, blockNames.size()> blocked{};
		std::uint64_t nodesVisited = 0, geometryObservations = 0, textureObservations = 0, candidateRegistrations = 0;
		std::uint64_t staticGeometryObservations = 0, supportedMaterialObservations = 0, measurableGeometryObservations = 0;
		std::uint64_t scansCompleted = 0, demandChecks = 0, shrinkCandidates = 0, requiredRestoreCandidates = 0;
		std::uint64_t retiredReplacements = 0, retiredReductions = 0, retiredLogicalReductionBytes = 0;
		std::uint64_t retiredCancelledUploads = 0;
		Block lastBlock = Block::Count;

		void Exclude(Exclusion reason) noexcept { ++excluded[static_cast<std::size_t>(reason)]; }
		void Defer(Block reason) noexcept
		{
			++blocked[static_cast<std::size_t>(reason)];
			lastBlock = reason;
		}
		[[nodiscard]] std::string_view LastBlock() const noexcept
		{
			return lastBlock == Block::Count ? "none" : blockNames[static_cast<std::size_t>(lastBlock)];
		}
	};

	/** A pressure episode emits at most one delayed summary in addition to its end summary. */
	struct PressureEpisode
	{
		static constexpr std::uint64_t SummaryDelayMs = 10000;
		bool active = false, summaryReported = false, retirementReported = false;
		std::uint64_t startedAtMs = 0;
		Diagnostics initial;
		std::uint64_t initialShrinks = 0, initialRestores = 0, initialFailures = 0, initialCancelled = 0;

		[[nodiscard]] bool SummaryDue(std::uint64_t now) noexcept
		{
			if (!active || summaryReported || now < startedAtMs || now - startedAtMs < SummaryDelayMs)
				return false;
			summaryReported = true;
			return true;
		}
	};
}
