#pragma once

#include "ProfileExchange.h"

namespace MenuUI
{
	/** Shared navigation state for the visible exchange controls and automation. */
	ProfileExchangeState& VisualsProfileExchange();
	/** Selects and exchanges complete appearance profiles; legacy files are import-only. */
	void DrawVisualsProfiles();
}
