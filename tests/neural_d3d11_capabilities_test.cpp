#include <d3d11_4.h>
#include <wrl/client.h>

#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	constexpr UINT kSample = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
	constexpr UINT kUav = D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
	constexpr auto kColor = DXGI_FORMAT_R16G16B16A16_FLOAT;
	constexpr auto kDepth = DXGI_FORMAT_R32_FLOAT;

	struct DeviceFixture
	{
		ULONG references = 1, retains = 0, releases = 0;
		UINT formatQueries = 0, support2Queries = 0, optionsQueries = 0, removalQueries = 0;
		UINT support = kSample | kUav;
		UINT support2 = D3D11_FORMAT_SUPPORT2_SHAREABLE | D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
		D3D11_SHARED_RESOURCE_TIER tier = D3D11_SHARED_RESOURCE_TIER_1;
		HRESULT formatResult = S_OK, support2Result = S_OK, optionsResult = S_OK, removalReason = S_OK;
		DXGI_FORMAT lastFormat = DXGI_FORMAT_UNKNOWN, lastSupport2Format = DXGI_FORMAT_UNKNOWN;
		ULONG AddRef()
		{
			++retains;
			return ++references;
		}
		ULONG Release()
		{
			++releases;
			return --references;
		}
		HRESULT CheckFormatSupport(DXGI_FORMAT format, UINT* result)
		{
			++formatQueries;
			lastFormat = format;
			*result = support;
			return std::exchange(formatResult, S_OK);
		}
		HRESULT CheckFeatureSupport(D3D11_FEATURE feature, void* result, UINT size)
		{
			if (feature == D3D11_FEATURE_FORMAT_SUPPORT2) {
				Require(size == sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT2), "wrong FORMAT_SUPPORT2 query size");
				++support2Queries;
				auto& data = *static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT2*>(result);
				lastSupport2Format = data.InFormat;
				data.OutFormatSupport2 = support2;
				return std::exchange(support2Result, S_OK);
			}
			Require(feature == D3D11_FEATURE_D3D11_OPTIONS5 && size == sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS5),
				"unexpected device feature query");
			++optionsQueries;
			static_cast<D3D11_FEATURE_DATA_D3D11_OPTIONS5*>(result)->SharedResourceTier = tier;
			return std::exchange(optionsResult, S_OK);
		}
		HRESULT GetDeviceRemovedReason()
		{
			++removalQueries;
			return removalReason;
		}
	};

#include "neural_device_loss_under_test.h"
#define ID3D11Device DeviceFixture
#include "neural_d3d11_capabilities_under_test.h"
#undef ID3D11Device

	struct Renderer
	{
		struct State
		{
			D3D11CapabilityCache d3d11Capabilities_;
			ComPtr<DeviceFixture> device_;
			struct
			{
				DeviceFixture* device = nullptr;
				DeviceFixture* Device() { return device; }
			} interop_;
			HRESULT GetDeviceRemovalReasonLocked(HRESULT) noexcept;
		};
	};
#include "neural_device_loss_invalidation_under_test.h"

	void CheckQueries()
	{
		DeviceFixture device;
		D3D11CapabilityCache cache;
		Require(!cache.SupportsFormat(nullptr, kColor, kSample) && !cache.SupportsSharedFormat(nullptr, kColor) &&
					!cache.SupportsSharedResources(nullptr),
			"null devices were accepted");
		Require(!cache.SupportsFormat(&device, DXGI_FORMAT_UNKNOWN, 0) && !cache.SupportsSharedFormat(&device, DXGI_FORMAT_UNKNOWN),
			"unknown formats were accepted");
		Require(device.retains == 0 && device.formatQueries == 0 && device.support2Queries == 0 && device.optionsQueries == 0,
			"invalid requests queried or retained a device");
		device.support = kSample;
		for (unsigned index = 0; index < 1000; ++index) {
			Require(cache.SupportsFormat(&device, kColor, kSample), "supported subset was rejected");
			Require(!cache.SupportsFormat(&device, kColor, kSample | kUav), "sampleability implied UAV support");
			Require(cache.SupportsSharedFormat(&device, kColor) && cache.SupportsSharedResources(&device), "shared support was rejected");
		}
		Require(device.formatQueries == 1 && device.support2Queries == 1 && device.optionsQueries == 1,
			"warm validation still queries capabilities");
		Require(device.retains == 1 && device.releases == 0 && device.references == 2, "cache hits changed COM ownership");
		device.support = kUav;
		Require(cache.SupportsFormat(&device, kDepth, kUav) && !cache.SupportsFormat(&device, kDepth, kSample),
			"different formats shared a capability entry");
		Require(device.lastFormat == kDepth && cache.SupportsFormat(&device, kColor, kSample), "queried format or previous entry changed");
		device.support2 = 0;
		Require(!cache.SupportsSharedFormat(&device, kDepth) && !cache.SupportsSharedFormat(&device, kDepth) && device.support2Queries == 2,
			"successful unsupported shareability was not cached");
		cache.Reset();
		device.tier = D3D11_SHARED_RESOURCE_TIER_0;
		Require(!cache.SupportsSharedResources(&device), "tier zero was accepted");
		device.tier = D3D11_SHARED_RESOURCE_TIER_1;
		Require(!cache.SupportsSharedResources(&device) && device.optionsQueries == 2, "successful unsupported tier was not cached");
	}

	void CheckFailures()
	{
		DeviceFixture device;
		D3D11CapabilityCache cache;
		device.formatResult = E_FAIL;
		Require(!cache.SupportsFormat(&device, kColor, 0), "failed query accepted an empty required mask");
		Require(cache.SupportsFormat(&device, kColor, kSample) && device.formatQueries == 2, "format failure did not retry");
		device.support2Result = E_FAIL;
		Require(!cache.SupportsSharedFormat(&device, kColor), "format success hid support2 failure");
		Require(cache.SupportsSharedFormat(&device, kColor) && device.support2Queries == 2, "support2 failure did not retry");
		Require(cache.SupportsFormat(&device, kColor, kSample) && device.formatQueries == 2, "support2 failure discarded successful format support");
		device.optionsResult = E_FAIL;
		Require(!cache.SupportsSharedResources(&device) && cache.SupportsSharedResources(&device) && device.optionsQueries == 2,
			"options failure became permanent");
		Require(cache.SupportsSharedFormat(&device, kDepth), "independent support2 setup failed");
		device.formatResult = E_FAIL;
		Require(!cache.SupportsFormat(&device, kDepth, kSample) && cache.SupportsFormat(&device, kDepth, kSample),
			"support2 success hid format failure");
	}

	void CheckQueryDeviceLoss()
	{
		for (auto result : { DXGI_ERROR_DEVICE_REMOVED, DXGI_ERROR_DEVICE_RESET, DXGI_ERROR_DEVICE_HUNG, DXGI_ERROR_DRIVER_INTERNAL_ERROR }) {
			for (unsigned family = 0; family < 3; ++family) {
				DeviceFixture device;
				D3D11CapabilityCache cache;
				Require(cache.SupportsFormat(&device, kColor, kSample) && cache.SupportsSharedFormat(&device, kColor), "cache warm-up failed");
				if (family != 2)
					Require(cache.SupportsSharedResources(&device), "options warm-up failed");
				const auto formats = device.formatQueries, shares = device.support2Queries, options = device.optionsQueries;
				if (family == 0) {
					device.formatResult = result;
					Require(!cache.SupportsFormat(&device, kDepth, kSample), "format query accepted device loss");
				} else if (family == 1) {
					device.support2Result = result;
					Require(!cache.SupportsSharedFormat(&device, kDepth), "support2 query accepted device loss");
				} else {
					device.optionsResult = result;
					Require(!cache.SupportsSharedResources(&device), "options query accepted device loss");
				}
				Require(cache.SupportsFormat(&device, kColor, kSample) && cache.SupportsSharedFormat(&device, kColor) && cache.SupportsSharedResources(&device),
					"invalidated entries did not recover through queries");
				Require(device.formatQueries == formats + 1 + (family == 0) && device.support2Queries == shares + 1 + (family == 1) &&
							device.optionsQueries == options + 1 + (family == 2),
					"query-reported device loss retained capability results");
				Require(device.references == 2 && device.releases == 0, "device loss released ownership prematurely");
			}
		}
	}

	void CheckLifetimeAndCapacity()
	{
		DeviceFixture first, second;
		second.support = 0;
		second.support2 = 0;
		second.tier = D3D11_SHARED_RESOURCE_TIER_0;
		{
			D3D11CapabilityCache cache;
			Require(cache.SupportsFormat(&first, kColor, kSample), "first device failed");
			Require(cache.SupportsSharedFormat(&first, kColor) && cache.SupportsSharedResources(&first), "first device sharing failed");
			Require(!cache.SupportsFormat(&second, kColor, kSample) && second.formatQueries == 1, "replacement reused old device capabilities");
			Require(!cache.SupportsSharedFormat(&second, kColor) && !cache.SupportsSharedResources(&second) &&
						second.support2Queries == 1 && second.optionsQueries == 1,
				"replacement reused sharing capabilities");
			Require(first.references == 1 && second.references == 2, "device replacement leaked ownership");
			cache.Invalidate();
			Require(second.references == 2, "scalar invalidation released the device");
			cache.Reset();
			Require(second.references == 1, "validation-only reset retained the device");
			Require(cache.SupportsFormat(&first, kColor, kSample) && first.formatQueries == 2, "reset retained cached values");
		}
		Require(first.references == 1 && second.references == 1, "destruction retained the device");
		D3D11CapabilityCache cache;
		for (unsigned index = 1; index <= 64; ++index) {
			const auto format = static_cast<DXGI_FORMAT>(index);
			Require(cache.SupportsFormat(&first, format, kSample) && cache.SupportsSharedFormat(&first, format), "capacity changed a supported result");
		}
		const auto format = static_cast<DXGI_FORMAT>(64);
		const auto formats = first.formatQueries, shares = first.support2Queries;
		first.support = first.support2 = 0;
		for (unsigned index = 0; index < 2; ++index)
			Require(!cache.SupportsFormat(&first, format, kSample) && !cache.SupportsSharedFormat(&first, format), "uncached request skipped required bits");
		Require(first.formatQueries == formats + 2 && first.support2Queries == shares + 2, "capacity fallback did not query each request");
		first.formatResult = E_FAIL;
		first.support = kSample;
		Require(!cache.SupportsFormat(&first, format, kSample) && cache.SupportsFormat(&first, format, kSample), "capacity fallback changed retry behavior");
		const auto options = first.optionsQueries;
		Require(cache.SupportsSharedResources(&first) && cache.SupportsSharedResources(&first) && first.optionsQueries == options + 1,
			"format capacity disabled the independent options cache");
	}

	void CheckObservedDeviceLoss()
	{
		DeviceFixture device11, device12;
		Renderer::State state;
		state.device_ = &device11;
		state.interop_.device = &device12;
		Require(state.d3d11Capabilities_.SupportsFormat(&device11, kColor, kSample), "initial cache warm-up failed");
		Require(state.GetDeviceRemovalReasonLocked(S_OK) == S_OK && state.GetDeviceRemovalReasonLocked(DXGI_ERROR_INVALID_CALL) == S_OK,
			"ordinary invalid-call result was treated as device loss");
		Require(state.d3d11Capabilities_.SupportsFormat(&device11, kColor, kSample) && device11.formatQueries == 1,
			"healthy observation discarded cached capabilities");
		for (unsigned source = 0; source < 3; ++source) {
			Require(state.d3d11Capabilities_.SupportsFormat(&device11, kColor, kSample), "cache warm-up failed");
			const auto queries = device11.formatQueries;
			device11.removalReason = source == 0 ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
			device12.removalReason = source == 1 ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
			const auto candidate = source == 2 ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
			Require(state.GetDeviceRemovalReasonLocked(candidate) == DXGI_ERROR_DEVICE_REMOVED, "device removal detection changed");
			Require(device11.references == 3, "health observation released cached ownership");
			Require(state.d3d11Capabilities_.SupportsFormat(&device11, kColor, kSample) && device11.formatQueries == queries + 1,
				"health observation retained cached capabilities");
		}
		state.device_.Reset();
		state.interop_.device = nullptr;
		const auto queries = device11.formatQueries;
		Require(state.GetDeviceRemovalReasonLocked(DXGI_ERROR_DEVICE_REMOVED) == DXGI_ERROR_DEVICE_REMOVED,
			"device loss before backend initialization was ignored");
		Require(state.d3d11Capabilities_.SupportsFormat(&device11, kColor, kSample) && device11.formatQueries == queries + 1 && device11.references == 2,
			"validation-only device loss retained values or released ownership");
	}
}

int main()
{
	try {
		CheckQueries();
		CheckFailures();
		CheckQueryDeviceLoss();
		CheckLifetimeAndCapacity();
		CheckObservedDeviceLoss();
		std::cout << "D3D11 capability caching, retries, ownership and device-loss invalidation passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
