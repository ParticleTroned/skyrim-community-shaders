#include "Diagnostics/EngineStutterMonitor.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	error Production gate fixture unexpectedly enables diagnostics
#endif
int main() { return 0; }
