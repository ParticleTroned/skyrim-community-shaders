#define NOMINMAX
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/upscalers/include/ffx_upscale.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>

struct FidelityFX
{
#include "fsr_sdk_versions.h"
};
struct Upscaling
{
#include "fsr_selection_schema.h"

	struct Settings
	{
		bool fsr4RuntimeEnable = false;
		uint32_t fsr4RuntimeSelectionSchemaVersion = 0;
	};
};
#include "fsr_migration_under_test.h"
#include "fsr_version_under_test.h"

void Check(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}

int main()
{
	for (uint32_t schema : { 0u, 1u, 2u, 3u }) {
		for (bool enabled : { false, true }) {
			Upscaling::Settings settings{ enabled, schema };
			ApplyLegacyFsr4RuntimeSelectionMigration(settings);
			Check(settings.fsr4RuntimeEnable == enabled, "migration must preserve explicit FSR3 and automatic choices");
			Check(settings.fsr4RuntimeSelectionSchemaVersion == std::max(schema, Upscaling::kFsr4RuntimeSelectionSchemaVersion), "migration must advance old schemas without downgrading new ones");
			settings.fsr4RuntimeEnable = !enabled;
			ApplyLegacyFsr4RuntimeSelectionMigration(settings);
			Check(settings.fsr4RuntimeEnable != enabled, "repeated migration must preserve subsequent user changes");
		}
	}
	constexpr auto fsr412 = FFX_UPSCALER_MAKE_VERSION(4, 1, 2);
	constexpr uint64_t sdkId = (0xF5A5CA1Eull << 32) | fsr412;
	Check(FFX_UPSCALER_VERSION == fsr412, "headers must describe FSR 4.1.2");
	Check(FidelityFX::HostFsr3Version == FFX_UPSCALER_MAKE_VERSION(3, 1, 5), "DX11 host implementation remains FSR 3.1.5");
	Check(FidelityFX::RuntimeFsr3Version == FFX_UPSCALER_MAKE_VERSION(3, 1, 6), "runtime fallback must select FSR 3.1.6");
	Check(RuntimeProviderMatchesVersion(sdkId, fsr412), "SDK provider ID must match");
	Check(RuntimeProviderMatchesVersion(sdkId | 0x80000000ull, fsr412), "driver version flag must not change the effect version");
	Check(!RuntimeProviderMatchesVersion(0, fsr412), "unknown provider cannot claim a version match");
	Check(!RuntimeProviderMatchesVersion(sdkId, FidelityFX::RuntimeFsr3Version), "an FSR4 ID cannot satisfy explicit FSR3");
	Check(!RuntimeProviderMatchesVersion(sdkId, FFX_UPSCALER_MAKE_VERSION(4, 1, 1)), "older versions must not match");
	std::cout << "FSR SDK selection checks passed\n";
}
