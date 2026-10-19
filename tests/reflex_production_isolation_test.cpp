#include "Features/Upscaling/ReflexCapture.h"
#include "Features/Upscaling/ReflexDiagnosticSuite.h"
#include "Features/Upscaling/ReflexDiagnostics.h"
#include "Features/Upscaling/ReflexPresentJournal.h"
#include "Features/Upscaling/ReflexPresentation.h"
#include "Features/Upscaling/ReflexReport.h"
#include "Features/Upscaling/ReflexRunState.h"
#include "Features/Upscaling/ReflexTestSettings.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	error Production isolation must compile without the DevBench definition.
#endif

// A production translation unit needs neither the SDK nor JSON for diagnostics.
int main() {}
