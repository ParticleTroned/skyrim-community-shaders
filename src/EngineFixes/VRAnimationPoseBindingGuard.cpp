#include "VRAnimationPoseBindingGuard.h"

#include "RE/B/BSFlattenedBoneTree.h"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace
{
	constexpr std::uintptr_t kApplyPoseCallsiteRVA = 0xAEC1AD;
	constexpr std::uintptr_t kApplyPoseTargetRVA = 0xB3C260;
	constexpr std::uintptr_t kBoneNodesOffset = 0x160;
	constexpr std::array<std::uint8_t, 5> kExpectedCall{ 0xE8, 0xAE, 0x54, 0x01, 0x00 };
	constexpr std::size_t kMaximumSceneObjects = 65536;
	constexpr std::uint64_t kDetailedEventLimit = 64;

	enum class BindingFailure
	{
		kAbsentFromScene,
		kFlattenedTypeMismatch
	};

	struct SceneScratch
	{
		std::vector<std::uintptr_t> liveObjects;
		std::vector<RE::NiAVObject*> pendingObjects;
	};

	thread_local SceneScratch sceneScratch;

	bool IsReadableProtection(DWORD a_protection)
	{
		if ((a_protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
			return false;

		switch (a_protection & 0xFF) {
		case PAGE_READONLY:
		case PAGE_READWRITE:
		case PAGE_WRITECOPY:
		case PAGE_EXECUTE_READ:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
		}
	}

	bool IsReadableRange(const void* a_address, std::size_t a_size)
	{
		if (!a_address || a_size == 0)
			return a_size == 0;

		auto cursor = reinterpret_cast<std::uintptr_t>(a_address);
		if (cursor > (std::numeric_limits<std::uintptr_t>::max)() - a_size)
			return false;
		const auto end = cursor + a_size;
		while (cursor < end) {
			MEMORY_BASIC_INFORMATION memoryInfo{};
			if (VirtualQuery(reinterpret_cast<const void*>(cursor), std::addressof(memoryInfo), sizeof(memoryInfo)) == 0 ||
				memoryInfo.State != MEM_COMMIT || !IsReadableProtection(memoryInfo.Protect)) {
				return false;
			}

			const auto regionBegin = reinterpret_cast<std::uintptr_t>(memoryInfo.BaseAddress);
			if (regionBegin > (std::numeric_limits<std::uintptr_t>::max)() - memoryInfo.RegionSize)
				return false;
			const auto regionEnd = regionBegin + memoryInfo.RegionSize;
			if (cursor < regionBegin || regionEnd <= cursor)
				return false;
			cursor = (std::min)(end, regionEnd);
		}
		return true;
	}

	bool BuildLiveObjectSet(RE::NiAVObject* a_root, std::vector<std::uintptr_t>& a_liveObjects)
	{
		auto& pending = sceneScratch.pendingObjects;
		a_liveObjects.clear();
		pending.clear();
		if (!a_root || !IsReadableRange(a_root, sizeof(RE::NiAVObject)))
			return false;

		pending.push_back(a_root);
		while (!pending.empty()) {
			auto* object = pending.back();
			pending.pop_back();
			const auto objectAddress = reinterpret_cast<std::uintptr_t>(object);
			if (!object)
				continue;
			if (a_liveObjects.size() >= kMaximumSceneObjects || !IsReadableRange(object, sizeof(RE::NiAVObject)))
				return false;

			a_liveObjects.push_back(objectAddress);
			auto* node = object->AsNode();
			if (!node)
				continue;

			const auto& children = node->GetChildren();
			const auto childSlots = children.free_idx();
			if (childSlots > children.capacity())
				return false;
			if (childSlots != 0 &&
				!IsReadableRange(children.begin(), static_cast<std::size_t>(childSlots) * sizeof(children.front()))) {
				return false;
			}
			if (pending.size() > kMaximumSceneObjects - a_liveObjects.size() ||
				childSlots > kMaximumSceneObjects - a_liveObjects.size() - pending.size()) {
				return false;
			}

			for (std::uint16_t index = 0; index < childSlots; ++index) {
				if (auto* child = children[index].get())
					pending.push_back(child);
			}
		}

		std::sort(a_liveObjects.begin(), a_liveObjects.end());
		a_liveObjects.erase(std::unique(a_liveObjects.begin(), a_liveObjects.end()), a_liveObjects.end());
		return true;
	}

	bool Contains(const std::vector<std::uintptr_t>& a_objects, const RE::NiAVObject* a_object)
	{
		return std::binary_search(a_objects.begin(), a_objects.end(), reinterpret_cast<std::uintptr_t>(a_object));
	}

	const char* FailureName(BindingFailure a_failure)
	{
		switch (a_failure) {
		case BindingFailure::kAbsentFromScene:
			return "absent-from-scene";
		case BindingFailure::kFlattenedTypeMismatch:
			return "flattened-type-mismatch";
		default:
			return "unknown";
		}
	}

	void ReportInvalidBinding(
		std::uint64_t a_sequence,
		RE::BShkbAnimationGraph* a_graph,
		std::uint32_t a_index,
		const RE::BShkbAnimationGraph::BoneNodeEntry& a_entry,
		const RE::hkQsTransform& a_pose,
		BindingFailure a_failure,
		std::size_t a_liveObjectCount)
	{
		if (a_sequence > kDetailedEventLimit) {
			if (a_sequence == kDetailedEventLimit + 1) {
				logger::error(
					"[VR pose binding guard] Suppressing detailed telemetry after {} rejected bindings",
					kDetailedEventLimit);
			}
			return;
		}

		alignas(16) std::array<float, 4> translation{};
		alignas(16) std::array<float, 4> rotation{};
		_mm_store_ps(translation.data(), a_pose.translation.quad);
		_mm_store_ps(rotation.data(), a_pose.rotation.vec.quad);
		const auto flattenedOffset = std::bit_cast<std::int32_t>(a_entry.unk08);
		const auto projectName = a_graph->projectName.c_str();

		logger::error(
			"[VR pose binding guard] Rejected stale animation destination: sequence={}, "
			"thread={}, reason={}, graph={}, holder={}, root={}, project='{}', "
			"bindingIndex={}, bindingNode={}, flattenedOffset={}, liveObjects={}, "
			"translation=({}, {}, {}, {}), rotation=({}, {}, {}, {})",
			a_sequence,
			GetCurrentThreadId(),
			FailureName(a_failure),
			fmt::ptr(a_graph),
			fmt::ptr(a_graph->holder),
			fmt::ptr(a_graph->rootNode),
			projectName ? projectName : "<unnamed>",
			a_index,
			fmt::ptr(a_entry.node),
			flattenedOffset,
			a_liveObjectCount,
			translation[0],
			translation[1],
			translation[2],
			translation[3],
			rotation[0],
			rotation[1],
			rotation[2],
			rotation[3]);

		std::array<void*, 16> stack{};
		const auto stackCount = CaptureStackBackTrace(0, static_cast<DWORD>(stack.size()), stack.data(), nullptr);
		for (USHORT index = 0; index < stackCount; ++index)
			logger::error("[VR pose binding guard] Rejection stack[{}]={:p}", index, stack[index]);
	}

	bool MatchesExpectedCall(std::uintptr_t a_callsite)
	{
		std::array<std::uint8_t, kExpectedCall.size()> bytes{};
		SIZE_T read = 0;
		if (!ReadProcessMemory(
				GetCurrentProcess(),
				reinterpret_cast<const void*>(a_callsite),
				bytes.data(),
				bytes.size(),
				std::addressof(read)) ||
			read != bytes.size() || bytes != kExpectedCall) {
			return false;
		}

		const auto displacement = std::bit_cast<std::int32_t>(std::array<std::uint8_t, 4>{ bytes[1], bytes[2], bytes[3], bytes[4] });
		const auto target = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(a_callsite + bytes.size()) + displacement);
		return target == REL::Offset(kApplyPoseTargetRVA).address();
	}
}

bool VRAnimationPoseBindingGuard::TryInstall()
{
	static_assert(offsetof(RE::BShkbAnimationGraph, boneNodes) == kBoneNodesOffset);
	if (!REL::Module::IsVR() || REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15)
		return false;
	if (original)
		return true;

	const auto callsite = REL::Offset(kApplyPoseCallsiteRVA).address();
	if (!MatchesExpectedCall(callsite)) {
		logger::warn(
			"[Engine Fixes] Skyrim VR pose-copy callsite is modified or unsupported; binding guard not installed");
		return false;
	}

	auto& trampoline = SKSE::GetTrampoline();
	original = reinterpret_cast<ApplyPose>(trampoline.write_call<5>(callsite, ApplyPoseGuarded));
	return original != nullptr;
}

void VRAnimationPoseBindingGuard::ApplyPoseGuarded(
	const RE::hkQsTransform* a_pose,
	BoneNodes* a_boneNodes,
	std::int32_t a_boneCount)
{
	if (!a_pose || !a_boneNodes || a_boneCount <= 0) {
		original(a_pose, a_boneNodes, a_boneCount);
		return;
	}

	auto* graph = reinterpret_cast<RE::BShkbAnimationGraph*>(
		reinterpret_cast<std::uintptr_t>(a_boneNodes) - kBoneNodesOffset);
	try {
		auto& liveObjects = sceneScratch.liveObjects;
		if (!BuildLiveObjectSet(graph->rootNode, liveObjects)) {
			const auto sequence = rejectedBindings.fetch_add(1, std::memory_order_relaxed) + 1;
			if (sequence <= kDetailedEventLimit) {
				logger::error(
					"[VR pose binding guard] Skipped pose application because the current scene tree "
					"could not be validated: sequence={}, thread={}, graph={}, holder={}, root={}",
					sequence,
					GetCurrentThreadId(),
					fmt::ptr(graph),
					fmt::ptr(graph->holder),
					fmt::ptr(graph->rootNode));
			}
			return;
		}

		const auto count = (std::min)(a_boneNodes->size(), static_cast<std::uint32_t>(a_boneCount));
		std::vector<std::pair<std::uint32_t, BindingFailure>> invalidBindings;
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto& entry = (*a_boneNodes)[index];
			if (!entry.node)
				continue;

			BindingFailure failure{};
			bool valid = Contains(liveObjects, entry.node);
			const auto flattenedOffset = std::bit_cast<std::int32_t>(entry.unk08);
			if (!valid) {
				failure = BindingFailure::kAbsentFromScene;
			} else if (flattenedOffset >= 0 && !netimmerse_cast<RE::BSFlattenedBoneTree*>(entry.node)) {
				valid = false;
				failure = BindingFailure::kFlattenedTypeMismatch;
			}

			if (!valid)
				invalidBindings.emplace_back(index, failure);
		}

		if (invalidBindings.empty()) {
			original(a_pose, a_boneNodes, a_boneCount);
			return;
		}

		for (const auto& [index, failure] : invalidBindings) {
			const auto sequence = rejectedBindings.fetch_add(1, std::memory_order_relaxed) + 1;
			ReportInvalidBinding(sequence, graph, index, (*a_boneNodes)[index], a_pose[index], failure, liveObjects.size());
		}

		BoneNodes safeBoneNodes(*a_boneNodes);
		for (const auto& [index, failure] : invalidBindings) {
			(void)failure;
			safeBoneNodes[index].node = nullptr;
		}
		original(a_pose, std::addressof(safeBoneNodes), a_boneCount);
	} catch (const std::bad_alloc&) {
		logger::error(
			"[VR pose binding guard] Could not allocate a sanitized binding view; skipped unsafe pose application");
	}
}
