#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace NeuralRendering
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	enum class CapacityRejectionKind
	{
		Pressure,
		Unsupported,
		UnsafeProvider
	};

	/** Rejected capacities persist across ordinary reconfiguration; saturation fails closed. */
	template <class Key, std::size_t Capacity>
	struct CapacityRejections
	{
		struct Entry
		{
			Key key{};
			CapacityRejectionKind kind = CapacityRejectionKind::UnsafeProvider;
			std::int32_t result = 0;
			std::uint32_t nativeResult = 0;
		};
		std::array<Entry, Capacity> entries{};
		std::size_t count = 0;
		bool saturated = false;
		[[nodiscard]] bool Rejects(const Key& key) const noexcept
		{
			if (saturated)
				return true;
			for (std::size_t index = 0; index < count; ++index)
				if (entries[index].key == key || entries[index].kind == CapacityRejectionKind::UnsafeProvider)
					return true;
			return false;
		}
		void Record(Entry entry) noexcept
		{
			for (std::size_t index = 0; index < count; ++index)
				if (entries[index].key == entry.key) {
					if (entry.kind == CapacityRejectionKind::UnsafeProvider)
						entries[index] = entry;
					return;
				}
			if (count == entries.size()) {
				saturated = true;
				return;
			}
			entries[count++] = entry;
		}
	};
#endif
	/** Sharing is local to one validated batch with one frozen colour/exposure configuration. */
	template <class Args>
	[[nodiscard]] bool SameSourceTransport(const Args& left, const Args& right) noexcept
	{
		return left.device == right.device && left.context == right.context &&
		       left.featureSlot % 4u == right.featureSlot % 4u &&
		       left.frameId == right.frameId && left.sourceWorldFrame == right.sourceWorldFrame &&
		       left.generation == right.generation && left.insertionPoint == right.insertionPoint &&
		       left.executionContext == right.executionContext &&
		       left.viewportCrop == right.viewportCrop && left.colorInput == right.colorInput &&
		       left.depthGuide == right.depthGuide && left.depthGuideSRV == right.depthGuideSRV &&
		       left.motionVectors == right.motionVectors && left.controlMask == right.controlMask &&
		       left.colorWidth == right.colorWidth && left.colorHeight == right.colorHeight &&
		       left.guideWidth == right.guideWidth && left.guideHeight == right.guideHeight &&
		       left.outputWidth == right.outputWidth && left.outputHeight == right.outputHeight &&
		       left.controlMaskWidth == right.controlMaskWidth && left.controlMaskHeight == right.controlMaskHeight &&
		       left.featureUpscaling == right.featureUpscaling;
	}

	/** Return the first compatible earlier source, or this region for an independent owner. */
	template <class Args, class Resources>
	[[nodiscard]] std::size_t FindSourceTransportOwner(std::span<const Args> args,
		std::span<const Resources> resources, std::size_t index) noexcept
	{
		if (resources[index].resourceKey.sharedSourceTransport)
			for (std::size_t prior = 0; prior < index; ++prior)
				if (SameSourceTransport(args[prior], args[index]) &&
					resources[prior].resourceKey == resources[index].resourceKey &&
					resources[prior].depthViewFormat == resources[index].depthViewFormat &&
					resources[prior].depth.desc.Format == resources[index].depth.desc.Format)
					return prior;
		return index;
	}

	/** A borrowed slot must detach when its next source has no compatible owner. */
	template <class Resource>
	[[nodiscard]] bool CanReuseSourceBinding(std::uint32_t retainedOwner, std::uint32_t requestedOwner,
		const std::array<Resource, 4>& retained, const std::array<Resource, 4>* requested) noexcept
	{
		return retainedOwner == requestedOwner && (!requested || retained == *requested);
	}

	/** Deduplicate only identical native resources with identical required states. */
	template <class Transition, std::size_t Capacity>
	[[nodiscard]] bool AddSourceTransition(std::array<Transition, Capacity>& entries,
		std::size_t& count, Transition value) noexcept
	{
		if (!value.resource || count > entries.size())
			return false;
		for (std::size_t index = 0; index < count; ++index)
			if (entries[index].resource == value.resource)
				return entries[index].featureState == value.featureState;
		if (count == entries.size())
			return false;
		entries[count++] = value;
		return true;
	}
}
