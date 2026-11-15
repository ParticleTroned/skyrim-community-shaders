#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Tracy/Tracy.hpp>
#include <Tracy/TracyD3D11.hpp>

#include "Diagnostics/D3DTextureLifetimeTracker.h"
#include "Diagnostics/VRPipelineDiagnostics.h"
#include "GpuPass.h"
#include "Utils/TracyZones.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

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

namespace
{
	std::string_view passName;
	unsigned openedPasses = 0, closedPasses = 0;
#if EXPECT_TRACY
	const tracy::SourceLocationData* passSource = nullptr;
#endif

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

// Keep profiling backends inert while exercising the production scope macros.
ScopedGpuPass::ScopedGpuPass(std::string_view name, const Util::PassTimingHandle&, bool)
{
	passName = name;
	++openedPasses;
#if EXPECT_TRACY
	passSource = nullptr;
#endif
}

#if EXPECT_TRACY
ScopedGpuPass::ScopedGpuPass(const tracy::SourceLocationData* source, std::string_view name, const Util::PassTimingHandle&, bool) :
	ScopedGpuPass(name)
{
	passSource = source;
}
#endif

ScopedGpuPass::~ScopedGpuPass() { ++closedPasses; }

namespace
{
	void VerifyIndexedPasses()
	{
		constexpr std::array names{ std::string_view("BuildBoundary::Pass"), std::string_view("BuildBoundary::Left"), std::string_view("BuildBoundary::Right") };
		constexpr std::array<std::pair<std::size_t, std::size_t>, 6> cases{ { { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 2 }, { std::numeric_limits<std::size_t>::max(), 2 }, { 0, 0 } } };
#if EXPECT_TRACY
		std::array<const tracy::SourceLocationData*, names.size()> sources{};
#endif
		for (const auto& [index, expected] : cases) {
			unsigned evaluations = 0;
			const auto previousPasses = openedPasses;
			{
				CS_GPU_PASS_INDEXED(([&] { ++evaluations; return index; }()),
					"BuildBoundary::Pass", "BuildBoundary::Left", "BuildBoundary::Right");
				Require(evaluations == 1, "Indexed pass evaluated its index more than once");
				Require(openedPasses == previousPasses + 1 && closedPasses == previousPasses, "Indexed pass did not open one scope");
				Require(passName == names[expected], "Indexed pass selected the wrong name");
#if EXPECT_TRACY
				Require(passSource && std::string_view(passSource->name) == names[expected], "Indexed pass lost its static Tracy source");
				Require(!sources[expected] || sources[expected] == passSource, "Indexed pass did not reuse its static Tracy source");
				sources[expected] = passSource;
#endif
			}
			Require(closedPasses == openedPasses, "Indexed pass did not close its scope");
		}
	}
}

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
	try {
		VerifyIndexedPasses();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
