#pragma once

#include <array>
#include <imgui.h>
#include <span>
#include <string>

namespace MenuUI::AdaptiveBalancePresets
{
	/** Exchange navigation is session-only and never changes rendering settings. */
	enum class Mode
	{
		Export,
		Import
	};
	enum class Collection
	{
		Full,
		Base,
		Locations,
		Worldspace,
		Region,
		City,
		Count
	};
	struct State
	{
		Mode mode = Mode::Export;
		Collection collection = Collection::Full;
	};
	struct Palette
	{
		ImVec4 normal, hovered, active, border;
	};
	/** Current geographic targets, in Worldspace, Region and City order. */
	using Targets = std::array<std::string, 3>;

	/** Draws both modes and the complete, three-row collection diagram. */
	void DrawSelection(State& a_state, const Palette& a_export, const Palette& a_import, const Targets& a_targets);
	/** Draws bounded feedback and the named file action; returns an explicit transfer request. */
	bool DrawTransfer(const State& a_state, const Palette& a_palette, std::string& a_name,
		std::span<const std::string> a_files, const std::string& a_path, const std::string& a_status,
		const char* a_unavailableReason, bool& a_refreshFiles);
	/** Stable labels shared by UI feedback and DevBench navigation. */
	const char* CollectionName(Collection a_collection);
	const char* CollectionId(Collection a_collection);
}
