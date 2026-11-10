#include <Tracy/Tracy.hpp>
#include <Tracy/TracyD3D11.hpp>

#include "Diagnostics/D3DTextureLifetimeTracker.h"
#include "Diagnostics/VRPipelineDiagnostics.h"
#include "Utils/TracyZones.h"

// This target deliberately has no PCH to verify CMake's dependency boundary.
#if EXPECT_TRACY
#	if !defined(TRACY_ENABLE) || !defined(TRACY_SUPPORT)
#		error Tracy builds must enable the client and instrumentation.
#	endif
#else
#	if defined(TRACY_ENABLE) || defined(TRACY_SUPPORT) || defined(TRACY_ON_DEMAND)
#		error Production builds must not inherit Tracy client definitions.
#	endif
#endif

int main()
{
	ZoneScoped;
	TracyPlot("BuildBoundary", 1.0);
#if EXPECT_TRACY
	const bool emitDrawZones = Util::TracyZones::PerDrawZonesEnabled();
	CS_DRAW_ZONE(drawZone, "BuildBoundary::Draw", emitDrawZones);
#else
	// Disabled instrumentation must discard arguments, including unavailable state.
	CS_DRAW_ZONE(drawZone, "BuildBoundary::Draw", unavailableDrawZoneState);
#endif
	return 0;
}
