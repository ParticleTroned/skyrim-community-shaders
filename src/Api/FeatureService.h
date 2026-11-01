#pragma once

#include "CSXAPI/CSfeatureapi.h"

namespace CSX::Api
{
	void InitializeFeatureService();
	const FeatureAPI::Interface001* GetFeatureService001();
}
