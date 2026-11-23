#pragma once

#include "VRAPI/CSperformanceapi.h"

namespace CSX::Api
{
	/** Publish only the on-demand API; this installs no hooks or capture consumers. */
	void InitializePerformanceService();
}
