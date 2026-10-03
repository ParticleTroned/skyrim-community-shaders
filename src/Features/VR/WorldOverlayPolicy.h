#pragma once

#include "VRAPI/CSworldoverlayapi.h"
#include <cmath>
#include <cstdint>

#ifdef _MSC_VER
#	pragma float_control(precise, on, push)
#endif

namespace CSX::WorldOverlays::Policy
{
	inline bool ValidAtlas(std::uint32_t width, std::uint32_t height) noexcept
	{
		return width && height && width <= WorldOverlayAPI::MaxAtlasDimension &&
		       height <= WorldOverlayAPI::MaxAtlasDimension &&
		       std::uint64_t(width) * height <= WorldOverlayAPI::MaxAtlasPixels;
	}
	inline bool ValidQuad(const WorldOverlayAPI::Quad001& q) noexcept
	{
		for (auto value : q.center)
			if (!std::isfinite(value) || std::abs(value) > WorldOverlayAPI::MaxWorldCoordinate)
				return false;
		for (auto value : q.uv)
			if (!std::isfinite(value) || value < 0 || value > 1)
				return false;
		return q.id && std::isfinite(q.width) && q.width > 0 && q.width <= WorldOverlayAPI::MaxQuadDimension &&
		       std::isfinite(q.height) && q.height > 0 && q.height <= WorldOverlayAPI::MaxQuadDimension &&
		       std::isfinite(q.opacity) && q.opacity >= 0 && q.opacity <= 1 &&
		       q.uv[0] < q.uv[2] && q.uv[1] < q.uv[3] &&
		       (q.occlusion == WorldOverlayAPI::Occlusion::SceneDepth || q.occlusion == WorldOverlayAPI::Occlusion::Disabled);
	}
	inline bool Fresh(std::uint32_t published, std::uint32_t current) noexcept
	{
		return current - published <= 2;
	}
	inline constexpr int AtlasPageCount = 3;
	struct PublicationState
	{
		int committed = -1, leased = -1, pinned = -1;
		bool leaseCanceled = false;
		std::uint64_t sequence = 0, epoch = 0, generation = 0, clearSerial = 0;
		std::uint32_t frame = 0, count = 0, presentedFrame = 0;
		std::uint64_t presentedSequence = 0;
		void Clear() noexcept
		{
			count = 0;
			committed = -1;
			leaseCanceled = true;
			presentedSequence = 0;
			++clearSerial;
		}
		bool CanWrite(int page) const noexcept
		{
			return leased < 0 && page >= 0 && page < AtlasPageCount && page != committed && page != pinned;
		}
		bool ReceiptReady(std::uint64_t worldEpoch, std::uint64_t resourceGeneration, std::uint32_t currentFrame) const noexcept
		{
			return count && presentedSequence && epoch == worldEpoch && generation == resourceGeneration && Fresh(presentedFrame, currentFrame);
		}
		void Accept(std::uint64_t frozenClearSerial, std::uint64_t frozenSequence, std::uint64_t worldEpoch,
			std::uint64_t resourceGeneration, std::uint32_t completedFrame) noexcept
		{
			if (count && clearSerial == frozenClearSerial && epoch == worldEpoch && generation == resourceGeneration) {
				presentedSequence = frozenSequence;
				presentedFrame = completedFrame;
			}
		}
	};
	struct Rect
	{
		std::uint32_t x = 0, y = 0, width = 0, height = 0;
	};
	inline bool Contains(Rect r, std::uint32_t width, std::uint32_t height) noexcept
	{
		return r.width && r.height && r.x <= width && r.y <= height &&
		       r.width <= width - r.x && r.height <= height - r.y;
	}
}

#ifdef _MSC_VER
#	pragma float_control(pop)
#endif
