#include "VRAPI/CSscreenshotapi.h"

#include <stdexcept>
#include <string_view>
#include <type_traits>

static_assert(std::is_standard_layout_v<CSX::ScreenshotAPI::Request001>);
static_assert(std::is_standard_layout_v<CSX::ScreenshotAPI::Response001>);
static_assert(std::is_standard_layout_v<CSX::ScreenshotAPI::Interface001>);
static_assert(CSX::ScreenshotAPI::MaximumRequestBytes == 256u * 1024u);
static_assert(CSX::ScreenshotAPI::Status::kRequestTooLarge != CSX::ScreenshotAPI::Status::kSuccess);

int main()
{
	CSX::ScreenshotAPI::Interface001 service;
	if (service.structSize != sizeof(service) ||
		service.major != 1 || service.minor != 0 || service.schemaRevision != 1)
		throw std::runtime_error("screenshot service metadata defaults are invalid");
	if (std::string_view(CSX::ScreenshotAPI::ServiceName) != "csx.screenshot")
		throw std::runtime_error("screenshot service identity is invalid");
	return 0;
}
