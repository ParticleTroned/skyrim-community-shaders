#define NOMINMAX
#include <FidelityFX/api/include/dx12/ffx_api_dx12.hpp>
#include <FidelityFX/api/include/ffx_api_loader.h>
#include <FidelityFX/upscalers/include/ffx_upscale.hpp>
#include <Windows.h>
#include <d3d12.h>

#include "Features/Upscaling/FSRRuntimeLifecyclePolicy.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

struct FidelityFX
{
	static constexpr uint32_t RuntimeAutomaticVersion = 0;
	bool runtimePresent = true;
	bool IsHostFSR3Supported() const { return true; }
	bool IsRuntimeUpscalerPresent() const { return runtimePresent; }
	bool IsAmdAdapterDetected() const { return false; }
	bool ShouldUseRuntimeUpscalerForFSR(std::optional<bool> automatic = std::nullopt) const;
};
namespace logger
{
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
	template <class... Args>
	void debug(const char*, Args&&...)
	{}
}
namespace globals::features
{
	struct
	{
		unsigned preferenceReads = 0;
		bool GetRuntimeFSR4Enabled()
		{
			++preferenceReads;
			return false;
		}
	} upscaling;
}
#include "fsr_runtime_route_under_test.h"
#include "fsr_version_format_under_test.h"
#include "fsr_version_under_test.h"
ffxFunctions ffxModule{};
#include "fsr_runtime_create_calls_under_test.h"
#include "fsr_runtime_create_under_test.h"
#include "fsr_runtime_query_under_test.h"

namespace
{
	constexpr uint32_t fsr3 = FFX_UPSCALER_MAKE_VERSION(3, 1, 6);
	constexpr uint64_t driver = (0xF5A5CA1Eull << 32) | 0x80000000ull | FFX_UPSCALER_MAKE_VERSION(5, 1, 1);
	uint64_t count = 2;
	bool overflow = false;
	bool misleadingName = false;
	bool queryFault = false;
	bool createFault = false;
	bool partialHandle = false;
	bool destroyFault = false;
	bool missingHandle = false;
	int handle;
	unsigned creates = 0;
	unsigned destroys = 0;
	std::vector<uint64_t> descriptorTypes;

	void Check(bool condition, const char* message)
	{
		if (!condition) {
			std::cerr << message << '\n';
			std::exit(1);
		}
	}
	ffxReturnCode_t Query(ffxContext*, ffxQueryDescHeader* header)
	{
		if (queryFault)
			RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
		auto& query = *reinterpret_cast<ffxQueryDescGetVersions*>(header);
		if (!query.versionIds) {
			*query.outputCount = count;
		} else {
			query.versionIds[0] = driver;
			query.versionIds[1] = (0xF5A5CA1Eull << 32) | fsr3;
			query.versionNames[0] = misleadingName ? "FSR 3.1.6 compatibility" : "FSR 5.1.1 *";
			query.versionNames[1] = "FSR 3.1.6";
			*query.outputCount = overflow ? count + 1 : count;
		}
		return FFX_API_RETURN_OK;
	}
	ffxReturnCode_t Create(ffxContext* context, ffxCreateContextDescHeader* header, const ffxAllocationCallbacks*)
	{
		++creates;
		descriptorTypes.clear();
		for (auto* item = header; item; item = item->pNext)
			descriptorTypes.push_back(item->type);
		if (createFault)
			RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
		if (!missingHandle)
			*context = &handle;
		return partialHandle ? FFX_API_RETURN_ERROR : FFX_API_RETURN_OK;
	}
	ffxReturnCode_t Destroy(ffxContext* context, const ffxAllocationCallbacks*)
	{
		++destroys;
		if (destroyFault)
			RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
		*context = nullptr;
		return FFX_API_RETURN_OK;
	}
}

int main()
{
	FidelityFX routing;
	Check(routing.ShouldUseRuntimeUpscalerForFSR(true) && !routing.ShouldUseRuntimeUpscalerForFSR(false) &&
			  globals::features::upscaling.preferenceReads == 0,
		"targeted NVIDIA planning must use its supplied preference without reading locked live state");
	Check(!routing.ShouldUseRuntimeUpscalerForFSR() && globals::features::upscaling.preferenceReads == 1,
		"ordinary dispatch must read the live preference once");
	Check(UpscalerVersionToString(0) == "Automatic", "automatic requests must not be reported as version 0.0.0");
	Check(UpscalerVersionToString(fsr3) == "3.1.6", "version reporting must decode the SDK version layout");
	ffxModule.Query = &Query;
	ffxModule.CreateContext = &Create;
	ffxModule.DestroyContext = &Destroy;
	uint64_t id = 0;
	std::string name;
	bool crashed = false;
	auto* device = reinterpret_cast<ID3D12Device*>(&handle);
	Check(QueryRuntimeUpscalerVersionId(device, fsr3, id, name, crashed) && !crashed && (id & 0x7FFFFFFF) == fsr3,
		"explicit FSR3 discovery must skip a newer driver provider");
	misleadingName = true;
	Check(QueryRuntimeUpscalerVersionId(device, fsr3, id, name, crashed) && (id & 0x7FFFFFFF) == fsr3,
		"discovery must ignore a misleading display name when the provider ID conflicts");
	misleadingName = false;
	Check(!QueryRuntimeUpscalerVersionId(device, 0, id, name, crashed) && id == 0,
		"automatic discovery must not turn an enumerated provider into a version override");
	count = 1000000;
	Check(!QueryRuntimeUpscalerVersionId(device, fsr3, id, name, crashed), "unbounded provider counts must be rejected");
	count = 2;
	overflow = true;
	Check(!QueryRuntimeUpscalerVersionId(device, fsr3, id, name, crashed), "growing enumeration must not consume incomplete output");
	overflow = false;
	queryFault = true;
	Check(!QueryRuntimeUpscalerVersionId(device, fsr3, id, name, crashed) && crashed, "enumeration faults must be contained for quarantine");

	for (bool automatic : { false, true }) {
		for (bool hasOverride : { false, true }) {
			const auto attempts = RuntimeUpscalerCreateAttempts(automatic, hasOverride);
			ffx::Context context = nullptr;
			ffx::CreateContextDescUpscale create{};
			ffx::CreateBackendDX12Desc backend{};
			ffx::CreateContextDescUpscaleVersion version{};
			version.version = fsr3;
			ffx::CreateContextDescOverrideVersion overrideVersion{};
			overrideVersion.versionId = driver;
			bool indeterminate = false;
			const auto before = creates;
			for (const auto& attempt : attempts) {
				if (!attempt.enabled)
					continue;
				Check(automatic == (attempt.attempt == RuntimeUpscalerCreateAttempt::kDefaultProvider),
					"only automatic mode may create an unpinned context");
				Check(TryCreateRuntimeUpscalerContext(context, attempt.attempt, create, backend, version, overrideVersion, crashed, indeterminate) == FFX_API_RETURN_OK,
					"selected creation path must succeed");
				break;
			}
			Check(creates == before + 1 && !crashed && !indeterminate, "successful selection must create exactly once");
			Check(descriptorTypes.size() == (automatic ? 2u : hasOverride ? 4u :
																			3u),
				"automatic contexts must carry only effect and backend descriptors");
			Destroy(&context, nullptr);
		}
	}
	for (int fault = 0; fault < 4; ++fault) {
		ffx::Context context = nullptr;
		ffx::CreateContextDescUpscale create{};
		ffx::CreateBackendDX12Desc backend{};
		ffx::CreateContextDescUpscaleVersion version{};
		ffx::CreateContextDescOverrideVersion overrideVersion{};
		bool indeterminate = false;
		createFault = fault == 0;
		partialHandle = fault == 1 || fault == 2;
		destroyFault = fault == 2;
		missingHandle = fault == 3;
		const auto before = destroys;
		Check(TryCreateRuntimeUpscalerContext(context, RuntimeUpscalerCreateAttempt::kDefaultProvider, create, backend, version, overrideVersion, crashed, indeterminate) != FFX_API_RETURN_OK,
			"bad providers must not publish usable automatic contexts");
		Check(indeterminate == (fault != 1), "only proven partial-handle cleanup permits recovery without quarantine");
		Check(destroys == before + (partialHandle ? 1u : 0u), "partial ownership must receive exactly one release attempt");
	}
	std::cout << "FSR runtime selection checks passed\n";
}
