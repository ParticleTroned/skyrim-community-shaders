#include "VRAPI/CSprofilerapi.h"

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
		Check(ServiceMajor == 1 && ServiceMinor == 0 && SchemaRevision == 1, "unexpected profiler contract version");
		Check((kCapabilityBoundedCapture & kCapabilityHistory) == 0, "capability bits overlap");
		Check(static_cast<std::uint32_t>(CaptureState::kCompleted) != static_cast<std::uint32_t>(CaptureState::kRunning), "capture states overlap");
		Check(CaptureRequest001{}.frameCount == 1, "default capture is not bounded to one frame");
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
