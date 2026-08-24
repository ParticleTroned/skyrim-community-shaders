#pragma once

#include "VRAPI/CSfeatureapi.h"

namespace CSX::Api
{
	void InitializeFeatureService();
	const FeatureAPI::Interface001* GetFeatureService001();
}
