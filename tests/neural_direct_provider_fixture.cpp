#include <Windows.h>
#include <intrin.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_params.h>
struct ID3D12Device;
struct ID3D12GraphicsCommandList;
#include <array>
#include <cwchar>

namespace
{
	unsigned calls = 0;
	unsigned failure = 0;
	std::array<unsigned, 4> parameterTokens{};
	std::array<bool, 4> parameterUsed{};
	bool CorrectCaller(void* address)
	{
		HMODULE module = nullptr;
		wchar_t path[MAX_PATH]{};
		return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				   reinterpret_cast<LPCWSTR>(address), &module) &&
		       GetModuleFileNameW(module, path, MAX_PATH) && std::wcsstr(path, L"nvngx.dll");
	}
}

extern "C"
{
	__declspec(dllexport) unsigned FixtureCalls() { return calls; }
	__declspec(dllexport) unsigned FixtureParameterCount()
	{
		unsigned count = 0;
		for (const bool used : parameterUsed)
			count += used;
		return count;
	}
	__declspec(dllexport) void FixtureSetFailure(unsigned value)
	{
		failure = value;
		calls = 0;
	}
	__declspec(dllexport) unsigned NVSDK_CONV NVSDK_NGX_GetApplicationId() { return 42; }
	__declspec(dllexport) unsigned NVSDK_CONV NVSDK_NGX_GetAPIVersion() { return 0x13; }
#ifndef FIXTURE_WITHOUT_PARAMETERS
	__declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_AllocateParameters(NVSDK_NGX_Parameter** parameters)
	{
		calls |= 128;
		*parameters = nullptr;
		if (failure == 6)
			return NVSDK_NGX_Result_Success;
		if (failure == 7 || failure == 8) {
			for (std::size_t i = 0; i < parameterTokens.size(); ++i) {
				if (parameterUsed[i]) {
					*parameters = reinterpret_cast<NVSDK_NGX_Parameter*>(&parameterTokens[i]);
					return failure == 8 ? NVSDK_NGX_Result_FAIL_PlatformError : NVSDK_NGX_Result_Success;
				}
			}
		}
		for (std::size_t i = 0; i < parameterTokens.size(); ++i) {
			if (!parameterUsed[i]) {
				parameterUsed[i] = true;
				*parameters = reinterpret_cast<NVSDK_NGX_Parameter*>(&parameterTokens[i]);
				return failure == 3 ? NVSDK_NGX_Result_FAIL_PlatformError : NVSDK_NGX_Result_Success;
			}
		}
		return NVSDK_NGX_Result_FAIL_PlatformError;
	}
	__declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_DestroyParameters(NVSDK_NGX_Parameter* parameters)
	{
		calls |= 256;
		if (failure == 4)
			return NVSDK_NGX_Result_FAIL_PlatformError;
		for (std::size_t i = 0; i < parameterTokens.size(); ++i) {
			if (parameterUsed[i] && parameters == reinterpret_cast<NVSDK_NGX_Parameter*>(&parameterTokens[i])) {
				parameterUsed[i] = false;
				return NVSDK_NGX_Result_Success;
			}
		}
		return NVSDK_NGX_Result_FAIL_InvalidParameter;
	}
#endif
	__declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init_Ext(unsigned long long application,
		const wchar_t*, ID3D12Device*, NVSDK_NGX_Version api, const NVSDK_NGX_Parameter*)
	{
		if (!CorrectCaller(_ReturnAddress()) || application != 42 || api != static_cast<NVSDK_NGX_Version>(0x13))
			return NVSDK_NGX_Result_FAIL_PlatformError;
		calls |= 1;
		if (failure == 1)
			return NVSDK_NGX_Result_FAIL_PlatformError;
		return NVSDK_NGX_Result_Success;
	}
	__declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter*)
	{
		if (!CorrectCaller(_ReturnAddress()))
			return NVSDK_NGX_Result_FAIL_PlatformError;
		calls |= 2;
		if (failure == 2)
			return NVSDK_NGX_Result_FAIL_PlatformError;
		return NVSDK_NGX_Result_Success;
	}
	__declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_GetScratchBufferSize(NVSDK_NGX_Feature, const NVSDK_NGX_Parameter*, size_t* bytes)
	{
		if (!CorrectCaller(_ReturnAddress()))
			return NVSDK_NGX_Result_FAIL_PlatformError;
		calls |= 4;
		*bytes = 123;
		return NVSDK_NGX_Result_Success;
	}
	__declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_CreateFeature(ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, const NVSDK_NGX_Parameter*, NVSDK_NGX_Handle** handle)
	{
		if (!CorrectCaller(_ReturnAddress()))
			return NVSDK_NGX_Result_FAIL_PlatformError;
		calls |= 8;
		*handle = reinterpret_cast<NVSDK_NGX_Handle*>(1);
		return NVSDK_NGX_Result_Success;
	}
	__declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_EvaluateFeature(ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback)
	{
		calls |= 16;
		return NVSDK_NGX_Result_Success;
	}
	__declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle*)
	{
		calls |= 32;
		return NVSDK_NGX_Result_Success;
	}
	__declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Shutdown1(ID3D12Device*)
	{
		calls |= 64;
		if (failure == 5 || FixtureParameterCount() != 0)
			return NVSDK_NGX_Result_FAIL_PlatformError;
		return NVSDK_NGX_Result_Success;
	}
}
