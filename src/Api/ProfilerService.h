#pragma once

#include "VRAPI/CSprofilerapi.h"

namespace CSX::Api
{
	void InitializeProfilerService();
	const ProfilerAPI::Interface001* GetProfilerService001();
	/** @brief Returns the additive independent CPU interface alongside the paired v1 service. */
	const ProfilerAPI::Interface002* GetProfilerService002();
}
