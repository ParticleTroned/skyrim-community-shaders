#include "VRAPI/CSprofilerapi.h"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace
{
	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw std::runtime_error(std::string(a_message));
	}
}

int main()
{
	try {
		using namespace CSX::ProfilerAPI;
		static_assert(std::is_standard_layout_v<Snapshot001>);
		static_assert(std::is_standard_layout_v<TimerDescriptor001>);
		static_assert(std::is_standard_layout_v<CaptureRequest001>);
		static_assert(std::is_standard_layout_v<CaptureProgress001>);
		static_assert(std::is_standard_layout_v<Interface001>);
		static_assert(std::is_standard_layout_v<Interface002>);
		static_assert(std::is_standard_layout_v<CpuSnapshot001>);
		static_assert(offsetof(Interface002, paired) == 0);
		static_assert(offsetof(Interface002, RequestCapture) == sizeof(Interface001));
		Check(ServiceMajor == 1 && ServiceMinor == 1 && SchemaRevision == 2, "unexpected profiler contract version");
		Check((ServiceCapabilities & kCapabilitySnapshot) != 0 &&
				  (ServiceCapabilities & kCapabilityHistoryReset) != 0 &&
				  (ServiceCapabilities & kCapabilitySelfTime) != 0,
			"published profiler capability mask is incomplete");
		Check((kCapabilityBoundedCapture & kCapabilityHistory) == 0, "capability bits overlap");
		Check(static_cast<std::uint32_t>(CaptureState::kCompleted) != static_cast<std::uint32_t>(CaptureState::kRunning), "capture states overlap");
		Check(CaptureRequest001{}.frameCount == 1, "default capture is not bounded to one frame");
		Check(SourceServiceMinor == 2 && SourceSchemaRevision == 3, "unexpected independent CPU contract version");
		Check((ServiceCapabilities & kCapabilityIndependentCpu) == 0, "legacy interface advertises unavailable extension methods");
		Check(CpuSnapshot001{}.publicationCount == 0, "default CPU snapshot falsely claims a captured frame");
		Check(static_cast<std::uint32_t>(CaptureMode::kBoth) ==
				  (static_cast<std::uint32_t>(CaptureMode::kGpu) | static_cast<std::uint32_t>(CaptureMode::kCpu)),
			"capture source mask is inconsistent");
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
