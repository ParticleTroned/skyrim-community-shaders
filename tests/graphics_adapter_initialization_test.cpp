#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <SKSE/Impl/PCH.h>
#include <Windows.h>

#include <dxgi.h>
#include <iostream>
#include <mutex>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
#include <stdexcept>
#include <winrt/base.h>

#include "Utils/GraphicsDriverInfo.h"

namespace stl = SKSE::stl;
namespace logger = spdlog;

struct State
{
	std::string adapterDescription = "previous adapter";
	void SetAdapterDescription(const std::wstring& description);
};

#include "graphics_adapter_name_under_test.h"

namespace globals
{
	State stateValue;
	State* state = &stateValue;
}

namespace
{
	unsigned publicDriverQueries = 0;

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	struct ComObject : IUnknown
	{
		ULONG references = 1;
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** a_result) override
		{
			*a_result = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
		ULONG STDMETHODCALLTYPE Release() override { return --references; }
	};

	struct Adapter : ComObject
	{
		DXGI_ADAPTER_DESC description{};
		HRESULT descriptionResult = S_OK, driverResult = S_OK;
		unsigned descriptionQueries = 0, driverQueries = 0;

		HRESULT GetDesc(DXGI_ADAPTER_DESC* a_description)
		{
			++descriptionQueries;
			if (SUCCEEDED(descriptionResult))
				*a_description = description;
			return descriptionResult;
		}
		HRESULT CheckInterfaceSupport(REFIID, LARGE_INTEGER* a_version)
		{
			++driverQueries;
			a_version->QuadPart = 0;
			return driverResult;
		}
	};

	struct __declspec(uuid("19E356BF-15DD-4C51-8723-FD4A18915BBC")) DxgiDevice : ComObject
	{
		Adapter* adapter = nullptr;
		HRESULT result = S_OK;
		unsigned queries = 0;

		HRESULT GetAdapter(Adapter** a_adapter)
		{
			++queries;
			if (FAILED(result))
				return result;
			adapter->AddRef();
			*a_adapter = adapter;
			return S_OK;
		}
	};

	struct Device
	{
		DxgiDevice* dxgiDevice = nullptr;
		HRESULT result = S_OK;
		unsigned queries = 0;

		HRESULT QueryInterface(REFIID a_interface, void** a_result)
		{
			++queries;
			Require(a_interface == __uuidof(DxgiDevice), "Query the rendering device's DXGI interface");
			if (FAILED(result))
				return result;
			dxgiDevice->AddRef();
			*a_result = dxgiDevice;
			return S_OK;
		}
	};
}

namespace Util
{
	GraphicsDriverVersion QueryGraphicsDriverVersion(const DXGI_ADAPTER_DESC&, std::optional<std::uint32_t> a_version)
	{
		++publicDriverQueries;
		return a_version ? GraphicsDriverVersion{ "Test vendor", "1.2.3", {} } :
		                   GraphicsDriverVersion{ "Test vendor", {}, "Windows driver version unavailable" };
	}
}

namespace
{
#define ID3D11Device Device
#define IDXGIDevice DxgiDevice
#define IDXGIAdapter Adapter
#include "graphics_adapter_initialization_under_test.h"
#undef ID3D11Device
#undef IDXGIDevice
#undef IDXGIAdapter
}

int main(int a_argc, char** a_argv)
{
	try {
		const std::string_view scenario = a_argc == 2 ? a_argv[1] : "quiet_off";
		std::ostringstream output;
		auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
		spdlog::set_default_logger(std::make_shared<spdlog::logger>("adapter-test", sink));
		spdlog::set_level(scenario == "quiet_off"   ? spdlog::level::off :
						  scenario == "quiet_error" ? spdlog::level::err :
						  scenario == "quiet_warn"  ? spdlog::level::warn :
													  spdlog::level::info);
		Adapter adapter;
		wcscpy_s(adapter.description.Description, scenario == "unicode" ? L"GPU \U0001F680" : L"Rendering GPU");
		if (scenario == "empty")
			adapter.description.Description[0] = L'\0';
		adapter.descriptionResult = scenario == "description_failure" ? E_FAIL : S_OK;
		adapter.driverResult = scenario == "driver_failure" ? E_FAIL : S_OK;
		DxgiDevice dxgiDevice;
		dxgiDevice.adapter = &adapter;
		dxgiDevice.result = scenario == "adapter_failure" ? E_FAIL : S_OK;
		Device device;
		device.dxgiDevice = &dxgiDevice;
		device.result = scenario == "device_failure" ? E_FAIL : S_OK;

		InitializeGraphicsAdapterInfoOnce(scenario == "null_device" ? nullptr : &device);
		const bool discoveryFailed = scenario == "null_device" || scenario == "device_failure" ||
		                             scenario == "adapter_failure" || scenario == "description_failure";
		if (discoveryFailed) {
			Require(globals::state->adapterDescription == "previous adapter", "Failed discovery must retain known GPU identity");
			Require(publicDriverQueries == 0 && adapter.driverQueries == 0, "Failed discovery must not query drivers");
			Require(output.str().find("Adapter information unavailable") != std::string::npos, "Failed discovery must be diagnosed");
		} else {
			const std::string expected = scenario == "unicode" ? "GPU \xF0\x9F\x9A\x80" :
			                             scenario == "empty"   ? "<GPU name conversion failed>" :
			                                                     "Rendering GPU";
			Require(globals::state->adapterDescription == expected, "UI GPU identity must be populated without optional backends");
			if (scenario.starts_with("quiet_")) {
				Require(output.str().empty(), "Quiet log levels must remain quiet");
				Require(publicDriverQueries == 0 && adapter.driverQueries == 0, "Quiet logging must skip optional driver diagnostics");
			} else {
				Require(publicDriverQueries == 1 && adapter.driverQueries == 1, "Info logging must retain driver diagnostics");
				Require(output.str().find(expected) != std::string::npos, "Log and UI must share the converted GPU name");
				if (scenario == "driver_failure")
					Require(output.str().find("Windows driver version unavailable") != std::string::npos, "Driver failure must remain explicit");
			}
		}
		const auto deviceQueries = device.queries;
		const auto dxgiQueries = dxgiDevice.queries;
		const auto descriptionQueries = adapter.descriptionQueries;
		const auto logged = output.str();
		InitializeGraphicsAdapterInfoOnce(&device);
		Require(device.queries == deviceQueries && dxgiDevice.queries == dxgiQueries &&
					adapter.descriptionQueries == descriptionQueries && output.str() == logged,
			"Repeated initialization must not repeat discovery or diagnostics");
		Require(dxgiDevice.references == 1 && adapter.references == 1, "Discovery must release borrowed COM references on every path");
		std::cout << "Graphics adapter initialization passed: " << scenario << '\n';
		return 0;
	} catch (const std::exception& a_error) {
		std::cerr << a_error.what() << '\n';
		return 1;
	}
}
