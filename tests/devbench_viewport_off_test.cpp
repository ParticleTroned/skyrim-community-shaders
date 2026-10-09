#include "Menu/DevBenchViewport.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	error This test must compile with DevBench disabled.
#endif
// Redefinition would fail if the production header emitted its diagnostic type.
namespace MenuUI
{
	struct DevBenchViewport
	{};
}
int main() { return 0; }
