#include "Diagnostics/EngineStutterMonitor.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	error Production gate fixture unexpectedly enables diagnostics
#endif
int main()
{
	// Disabled instrumentation does not evaluate its label or require diagnostic symbols.
	CSX_CODE_EVALUATION_SCOPE(unusedScope, unavailable_diagnostic_label());
	return 0;
}
