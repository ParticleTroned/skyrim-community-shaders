#include "Api/AcceptedDrawRegistry.h"
#include "Diagnostics/DevBenchCpuSnapshot.h"
#include "ProfilerDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	error The production bridge exclusion test must compile without DevBench.
#endif

template <class T>
concept HasDevBenchObserverInspection = requires { &T::InspectObservers; };
static_assert(!HasDevBenchObserverInspection<CSX::Api::AcceptedDrawRegistry>);

int main()
{
	ProfilerDevBenchBridge::Install();
	return ProfilerDevBenchBridge::IsBuilt() || ProfilerDevBenchBridge::IsRegistered() ? 1 : 0;
}
