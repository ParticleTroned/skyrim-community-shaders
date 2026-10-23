#include "Diagnostics/ReflexMeasurement.h"
#include "Diagnostics/ReflexMeasurementInternal.h"
#include "Diagnostics/ReflexMeasurementState.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	error Production exclusion test requires DevBench disabled
#endif
// These names must remain unoccupied when diagnostics are compiled out.
namespace CSX::Diagnostics::ReflexMeasurement
{
	struct Journal
	{};
	struct Service
	{};
	struct UpdateScope
	{};
}
int main() { return 0; }
