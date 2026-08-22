#pragma once

#include "VRAPI/CSprofilerapi.h"

namespace CSX::Api
{
	void InitializeProfilerService();
	const ProfilerAPI::Interface001* GetProfilerService001();
}
