#pragma once

#include <nlohmann/json_fwd.hpp>

namespace CSX::ScreenshotAPI
{
	struct Interface001;
}

namespace CSX::Api
{
	void InitializeScreenshotService();
	const ScreenshotAPI::Interface001* GetScreenshotService001();
	nlohmann::json DispatchScreenshotServiceRequest(const nlohmann::json& a_request);
}
