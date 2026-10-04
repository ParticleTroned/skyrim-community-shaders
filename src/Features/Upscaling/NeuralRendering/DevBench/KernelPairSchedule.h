#pragma once

#include "ProviderKernelChainContract.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace NrReplay::KernelPair
{
	inline constexpr std::size_t kStages = 158, kRegions = 4, kTargetStage = 3;
	inline constexpr char kStageSha256[] = "a4d24509af262c3ad7e455f97240fd8337e2eebd3c8abbdfd75be419523a1467";
	inline constexpr char kStageFamilySha256[] = "ec0927de13d71045c684c7f47b0cbf9d0a3aa3dfb75fd0e52982a194411538a5";
	inline constexpr char kCommandSha256[] = "7ecd5cc71a1a7529fec45da3923f71cef71709795979fbbec46aa53ea3c42245";
	inline constexpr std::size_t kMaximumRetainedSamples = 128;
	inline constexpr std::size_t kMaximumRegionCommands = 512;
	inline constexpr unsigned kMaximumScheduleRepetitions = 4;

	/** The admitted tilesync entry uses 32 FP8 channels per tensor position. */
	inline std::uint64_t TensorByteSpan(std::uint32_t width, std::uint32_t height)
	{
		ProviderFloor::Require(width && height && width <= D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION &&
								   height <= D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION,
			"kernel pair tensor extent is outside the bounded native contract");
		return std::uint64_t{ width } * height * 32;
	}

	/** Completion clears are 32-bit elements inside the same private scratch allocation. */
	inline std::uint64_t CompletionByteSpan(std::uint32_t elements)
	{
		ProviderFloor::Require(elements != 0, "kernel pair completion clear is empty");
		return std::uint64_t{ elements } * sizeof(std::uint32_t);
	}

	/** Repetition is limited to complete, already-qualified static command streams. */
	inline void ValidateRepetitions(unsigned count, std::string_view mode, std::size_t stages)
	{
		ProviderFloor::Require(count >= 1 && count <= kMaximumScheduleRepetitions &&
								   (count == 1 || ((mode == "original" || mode == "layer-control" || mode == "model-batch") && stages == kStages)),
			"schedule repetitions require a complete original, layer-control or model-batch schedule and count1..4");
	}

	inline void ValidateLogicalPermutation(std::span<const std::size_t> ids, std::size_t count)
	{
		ProviderFloor::Require(count > 0 && count <= kStages * kRegions && ids.size() == count,
			"kernel pair logical coverage count differs");
		std::vector<std::size_t> sorted(ids.begin(), ids.end());
		std::sort(sorted.begin(), sorted.end());
		ProviderFloor::Require(sorted.front() == 0 && sorted.back() + 1 == count &&
								   std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(),
			"kernel pair logical permutation omitted or duplicated a stage");
	}
	/** Alternating balanced comparisons share one captured context and complete stage stream. */
	inline void ValidateComparison(std::string_view control, unsigned repetitions, std::string_view mode, std::size_t stages)
	{
		ProviderFloor::Require(control.empty() || ((control == "original" || control == "layer-control") &&
													  repetitions == 4 && mode == "model-batch" && stages == kStages),
			"kernel comparison requires original or layer-control with four complete model-batch repetitions");
	}
	inline bool ComparisonBatch(unsigned iteration, unsigned repetition)
	{
		ProviderFloor::Require(repetition < 4, "kernel comparison repetition out of range");
		return (repetition == 1 || repetition == 2) != ((iteration & 1u) != 0);
	}

	enum class Kind
	{
		Launch,
		Barrier,
		Heaps,
		Batch,
		Join
	};
	struct Command
	{
		Kind kind = Kind::Launch;
		std::size_t descriptor = SIZE_MAX;
		std::vector<D3D12_RESOURCE_BARRIER> barriers;
		std::vector<ID3D12DescriptorHeap*> heaps;
	};
	struct Region
	{
		std::vector<Command> commands;
		std::vector<std::size_t> descriptors;
		std::string stageSignature, stageFamilySignature, commandSignature;
		std::size_t target = SIZE_MAX;
		bool complete = false;
	};
	using Pairing = std::array<std::array<std::size_t, 2>, kRegions / 2>;
	inline constexpr Pairing kSameEyePairs{ std::array<std::size_t, 2>{ 0, 1 }, std::array<std::size_t, 2>{ 2, 3 } };

	/** Every original region occurs once; pairing never changes its eye, slot or packet. */
	inline bool ValidPairing(const Pairing& pairs) noexcept
	{
		std::array<bool, kRegions> used{};
		for (const auto& pair : pairs) {
			if (pair[0] >= pair[1] || pair[1] >= kRegions || used[pair[0]] || used[pair[1]])
				return false;
			used[pair[0]] = used[pair[1]] = true;
		}
		return std::ranges::all_of(used, [](bool value) { return value; });
	}

	/** Exact complete launch signatures permit stereo pairs without enlarging either context. */
	inline std::optional<Pairing> CompatiblePairing(const std::array<std::string, kRegions>& signatures)
	{
		constexpr std::array<Pairing, 3> candidates{ kSameEyePairs,
			Pairing{ std::array<std::size_t, 2>{ 0, 2 }, std::array<std::size_t, 2>{ 1, 3 } },
			Pairing{ std::array<std::size_t, 2>{ 0, 3 }, std::array<std::size_t, 2>{ 1, 2 } } };
		for (const auto& pairs : candidates)
			if (std::ranges::all_of(pairs, [&](const auto& pair) {
					return !signatures[pair[0]].empty() && signatures[pair[0]] == signatures[pair[1]];
				}))
				return pairs;
		return std::nullopt;
	}
	struct Operation
	{
		Kind kind = Kind::Launch;
		std::size_t region = 0, command = 0, secondRegion = SIZE_MAX, secondCommand = SIZE_MAX;
	};
	struct Sample
	{
		struct Repetition
		{
			unsigned ordinal = 0;
			std::string mode;
			std::size_t eventBegin = 0, eventEnd = 0, physicalLaunches = 0, commands = 0, joins = 0;
			std::vector<std::size_t> logicalDescriptors;
			bool beginCompleted = false, endCompleted = false, completed = false;
		};
		std::array<Region, kRegions> regions;
		Pairing pairs = kSameEyePairs;
		std::size_t completed = 0;
		std::vector<Operation> order;
		std::vector<Operation> controlOrder;
		std::array<std::array<std::uint8_t, 192>, 2> batchPackets{};
		std::array<int, 2> batchStatuses{};
		std::array<bool, 2> batchAttempted{};
		std::vector<std::size_t> submittedLogicalDescriptors;
		std::size_t commandsSubmitted = 0, joins = 0, batchedCalls = 0;
		bool submitted = false;
		std::vector<Repetition> repetitions;
		std::size_t repetitionBoundaries = 0;
		std::array<bool, kRegions> descriptorCacheConfirmed{};
	};
	struct Resource
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> owner;
		D3D12_RESOURCE_DESC desc{};
		D3D12_HEAP_PROPERTIES properties{};
		D3D12_HEAP_FLAGS flags{};
		std::uint64_t base = 0, end = 0;
	};
	struct Owners
	{
		std::vector<Resource> resources;
		std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> heaps;
		std::vector<std::unique_ptr<Sample>> samples;
	};
	/** A batched launch has one binding, so same-shaped but distinct heaps are rejected. */
	inline void ValidateHeapBindings(const Sample& sample)
	{
		std::vector<ID3D12DescriptorHeap*> common;
		for (const auto& region : sample.regions) {
			bool found = false;
			for (const auto& command : region.commands) {
				if (command.kind != Kind::Heaps)
					continue;
				found = true;
				if (common.empty())
					common = command.heaps;
				ProviderFloor::Require(!common.empty() && command.heaps == common, "kernel pair requires identical ordered heap bindings for every region");
			}
			ProviderFloor::Require(found, "kernel pair region has no descriptor heap binding");
		}
	}

	/** Validates each original command stream and locates its ordered launch boundaries. */
	inline std::array<std::array<std::size_t, kStages>, kRegions> ValidateRegionCommands(const Sample& sample)
	{
		using ProviderFloor::Require;
		Require(sample.completed == kRegions, "kernel pair requires four complete evaluations");
		std::array<std::array<std::size_t, kStages>, kRegions> launches{};
		for (std::size_t r = 0; r < kRegions; ++r) {
			const auto& region = sample.regions[r];
			Require(region.complete && region.descriptors.size() == kStages && region.commands.size() <= kMaximumRegionCommands &&
						region.target < region.commands.size() &&
						region.commands[region.target].kind == Kind::Launch && region.commands[region.target].descriptor == region.descriptors[kTargetStage],
				"kernel pair region or target boundary incomplete");
			for (std::size_t stage = 0; stage < kStages; ++stage)
				Require(region.descriptors[stage] == r * kStages + stage, "kernel pair region descriptors are out of order");
			std::size_t stage = 0;
			for (std::size_t c = 0; c < region.commands.size(); ++c) {
				const auto& command = region.commands[c];
				switch (command.kind) {
				case Kind::Launch:
					Require(stage < kStages && command.descriptor == region.descriptors[stage] && command.barriers.empty() && command.heaps.empty(),
						"kernel pair launch command differs from the ordered descriptor stream");
					launches[r][stage++] = c;
					break;
				case Kind::Barrier:
					Require(command.descriptor == SIZE_MAX && !command.barriers.empty() && command.barriers.size() <= 4096 && command.heaps.empty(),
						"kernel pair barrier command payload invalid");
					break;
				case Kind::Heaps:
					Require(command.descriptor == SIZE_MAX && command.barriers.empty() && !command.heaps.empty() && command.heaps.size() <= 2 &&
								std::all_of(command.heaps.begin(), command.heaps.end(), [](const auto* heap) { return heap != nullptr; }),
						"kernel pair heap command payload invalid");
					break;
				default:
					throw std::runtime_error("kernel pair original stream contains an unsupported command kind");
				}
			}
			Require(stage == kStages && launches[r][kTargetStage] == region.target, "kernel pair launch boundaries incomplete");
		}
		return launches;
	}

	/** Builds and validates the complete permutation before any deferred command may execute. */
	inline std::vector<Operation> BuildOrder(const Sample& sample, std::string_view mode, std::size_t maximumBatchedStages = kStages)
	{
		using ProviderFloor::Require;
		Require(mode == "original" || mode == "control" || mode == "batch" || mode == "layer-control" || mode == "model-batch", "kernel pair mode invalid");
		Require(maximumBatchedStages >= 1 && maximumBatchedStages <= kStages && (mode == "model-batch" || maximumBatchedStages == kStages),
			"kernel pair stage limit requires model-batch and a prefix within the full stage count");
		const auto launches = ValidateRegionCommands(sample);
		Require(ValidPairing(sample.pairs), "kernel pair matching omits or duplicates an original region");
		std::vector<Operation> result;
		const auto append = [&](std::size_t region, std::size_t begin, std::size_t end) {
			for (auto i = begin; i < end; ++i)
				result.push_back({ sample.regions[region].commands[i].kind, region, i });
		};
		if (mode == "original") {
			for (std::size_t region = 0; region < kRegions; ++region)
				append(region, 0, sample.regions[region].commands.size());
		} else {
			for (const auto& pair : sample.pairs) {
				const auto first = pair[0], second = pair[1];
				const auto& a = sample.regions[first];
				const auto& b = sample.regions[second];
				if (mode == "layer-control" || mode == "model-batch") {
					std::array<std::size_t, 2> next{};
					for (std::size_t stage = 0; stage < kStages; ++stage) {
						const bool batch = mode == "model-batch" && stage < maximumBatchedStages;
						for (std::size_t region = 0; region < 2; ++region) {
							const auto identity = pair[region];
							const auto launch = launches[identity][stage];
							append(identity, next[region], launch + (batch ? 0 : 1));
							next[region] = launch + 1;
						}
						if (batch)
							result.push_back({ Kind::Batch, first, launches[first][stage], second, launches[second][stage] });
						result.push_back({ Kind::Join, first });
					}
					append(first, next[0], a.commands.size());
					append(second, next[1], b.commands.size());
					continue;
				}
				append(first, 0, a.target);
				append(second, 0, b.target);
				if (mode == "batch")
					result.push_back({ Kind::Batch, first, a.target, second, b.target });
				else {
					append(first, a.target, a.target + 1);
					append(second, b.target, b.target + 1);
				}
				result.push_back({ Kind::Join, first });
				append(first, a.target + 1, a.commands.size());
				append(second, b.target + 1, b.commands.size());
			}
		}
		std::vector<std::size_t> logical;
		std::array<std::size_t, kRegions> nextCommand{};
		for (const auto& operation : result) {
			if (operation.kind == Kind::Join)
				continue;
			Require(operation.region < kRegions && operation.command < sample.regions[operation.region].commands.size() &&
						operation.command == nextCommand[operation.region] &&
						sample.regions[operation.region].commands[operation.command].kind == (operation.kind == Kind::Batch ? Kind::Launch : operation.kind),
				"kernel pair permutation changes an original command boundary");
			++nextCommand[operation.region];
			if (operation.kind == Kind::Launch || operation.kind == Kind::Batch)
				logical.push_back(sample.regions[operation.region].commands[operation.command].descriptor);
			if (operation.kind == Kind::Batch) {
				const bool matchingPair = std::ranges::any_of(sample.pairs, [&](const auto& pair) {
					return pair[0] == operation.region && pair[1] == operation.secondRegion;
				});
				Require(matchingPair && operation.secondRegion < kRegions &&
							operation.secondCommand < sample.regions[operation.secondRegion].commands.size() && operation.secondCommand == nextCommand[operation.secondRegion] &&
							sample.regions[operation.secondRegion].commands[operation.secondCommand].kind == Kind::Launch &&
							sample.regions[operation.region].commands[operation.command].descriptor % kStages ==
								sample.regions[operation.secondRegion].commands[operation.secondCommand].descriptor % kStages,
					"kernel pair batch changes the second original command boundary");
				++nextCommand[operation.secondRegion];
				logical.push_back(sample.regions[operation.secondRegion].commands[operation.secondCommand].descriptor);
			}
		}
		for (std::size_t r = 0; r < kRegions; ++r)
			Require(nextCommand[r] == sample.regions[r].commands.size(), "kernel pair permutation omits original commands");
		std::sort(logical.begin(), logical.end());
		Require(logical.size() == kRegions * kStages, "kernel pair logical stage count differs");
		for (std::size_t i = 0; i < logical.size(); ++i)
			Require(logical[i] == i, "kernel pair plan omitted or duplicated a logical stage");
		return result;
	}

	template <class Value>
	Value Read(std::span<const std::uint8_t> bytes, std::size_t offset)
	{
		ProviderFloor::Require(offset <= bytes.size() && sizeof(Value) <= bytes.size() - offset, "kernel pair packet read out of bounds");
		Value value{};
		std::memcpy(&value, bytes.data() + offset, sizeof(value));
		return value;
	}

	/** Requires complete address spans in one retained buffer, never inferred adjacent extents. */
	inline std::size_t FindBuffer(const Owners& owners, std::uint64_t pointer, std::uint64_t bytes)
	{
		ProviderFloor::Require(pointer && bytes && bytes <= UINT64_MAX - pointer, "kernel pair address span invalid");
		std::size_t found = SIZE_MAX;
		for (std::size_t i = 0; i < owners.resources.size(); ++i) {
			const auto& resource = owners.resources[i];
			if (resource.base && pointer >= resource.base && pointer + bytes <= resource.end) {
				ProviderFloor::Require(found == SIZE_MAX, "kernel pair buffer range is ambiguous");
				found = i;
			}
		}
		ProviderFloor::Require(found != SIZE_MAX, "kernel pair address lacks retained buffer ownership");
		return found;
	}
}
