#pragma once

#include <vector>

namespace StreamingTextures::Preview
{
	/** Track native preview loading before the DDS loader applies remembered dimensions. */
	void Install();
	struct RootSnapshot
	{
		std::vector<RE::NiPointer<RE::NiAVObject>> roots;
		bool complete = true;
	};
	/** Retain bounded preview roots, rejecting implausible addresses; incomplete inventory suspends streaming. */
	RootSnapshot Roots();
	bool Active();
}
