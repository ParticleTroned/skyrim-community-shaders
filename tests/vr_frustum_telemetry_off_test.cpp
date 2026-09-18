#include "Diagnostics/VRFrustumTelemetry.h"
#include "Diagnostics/VRFrustumTelemetryPolicy.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	error This target must compile without the DevBench bridge.
#endif

int main() { return 0; }
