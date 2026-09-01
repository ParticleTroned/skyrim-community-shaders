#include "NvApiDrs.h"

#include <nvapi.h>

// The generated settings declarations depend on base NvAPI types.
#include <NvApiDriverSettings.h>

#include <algorithm>
#include <bit>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace
{
	constexpr wchar_t kNvApiLibrary[] = L"nvapi64.dll";
	constexpr wchar_t kSkyrimSEProfileName[] = L"The Elder Scrolls V: Skyrim Special Edition";

	constexpr NvU32 kInitializeId = 0x0150E828;
	constexpr NvU32 kUnloadId = 0xD22BDD7E;
	constexpr NvU32 kGetErrorMessageId = 0x6C2D048C;
	constexpr NvU32 kCreateSessionId = 0x0694D52E;
	constexpr NvU32 kDestroySessionId = 0xDAD9CFF8;
	constexpr NvU32 kLoadSettingsId = 0x375DBD6B;
	constexpr NvU32 kSaveSettingsId = 0xFCBC7E14;
	constexpr NvU32 kFindApplicationByNameId = 0xEEE566B2;
	constexpr NvU32 kFindProfileByNameId = 0x7E4A9A0B;
	constexpr NvU32 kGetSettingId = 0x73BF8338;
	constexpr NvU32 kSetSettingId = 0x577DD202;

	using QueryInterface = void*(__cdecl*)(NvU32);

	template <class T>
	[[nodiscard]] T Resolve(QueryInterface a_queryInterface, NvU32 a_interfaceId)
	{
		static_assert(sizeof(T) == sizeof(void*));
		return std::bit_cast<T>(a_queryInterface(a_interfaceId));
	}

	class NvApi
	{
	public:
		using Initialize = decltype(&NvAPI_Initialize);
		using Unload = decltype(&NvAPI_Unload);
		using GetErrorMessage = decltype(&NvAPI_GetErrorMessage);
		using CreateSession = decltype(&NvAPI_DRS_CreateSession);
		using DestroySession = decltype(&NvAPI_DRS_DestroySession);
		using LoadSettings = decltype(&NvAPI_DRS_LoadSettings);
		using SaveSettings = decltype(&NvAPI_DRS_SaveSettings);
		using FindApplicationByName = decltype(&NvAPI_DRS_FindApplicationByName);
		using FindProfileByName = decltype(&NvAPI_DRS_FindProfileByName);
		using GetSetting = decltype(&NvAPI_DRS_GetSetting);
		using SetSetting = decltype(&NvAPI_DRS_SetSetting);

		NvApi() = default;
		NvApi(const NvApi&) = delete;
		NvApi(NvApi&&) = delete;
		NvApi& operator=(const NvApi&) = delete;
		NvApi& operator=(NvApi&&) = delete;

		~NvApi()
		{
			if (initialized && unload)
				(void)unload();

			if (module)
				(void)FreeLibrary(module);
		}

		[[nodiscard]] bool Load()
		{
			module = LoadLibraryExW(kNvApiLibrary, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (!module)
				return false;

			const auto queryAddress = GetProcAddress(module, "nvapi_QueryInterface");
			if (!queryAddress)
				return false;

			const auto queryInterface = std::bit_cast<QueryInterface>(queryAddress);
			initialize = Resolve<Initialize>(queryInterface, kInitializeId);
			unload = Resolve<Unload>(queryInterface, kUnloadId);
			getErrorMessage = Resolve<GetErrorMessage>(queryInterface, kGetErrorMessageId);
			createSession = Resolve<CreateSession>(queryInterface, kCreateSessionId);
			destroySession = Resolve<DestroySession>(queryInterface, kDestroySessionId);
			loadSettings = Resolve<LoadSettings>(queryInterface, kLoadSettingsId);
			saveSettings = Resolve<SaveSettings>(queryInterface, kSaveSettingsId);
			findApplicationByName = Resolve<FindApplicationByName>(queryInterface, kFindApplicationByNameId);
			findProfileByName = Resolve<FindProfileByName>(queryInterface, kFindProfileByNameId);
			getSetting = Resolve<GetSetting>(queryInterface, kGetSettingId);
			setSetting = Resolve<SetSetting>(queryInterface, kSetSettingId);

			if (!initialize || !unload || !createSession || !destroySession || !loadSettings ||
				!saveSettings || !findProfileByName || !getSetting || !setSetting) {
				return false;
			}

			const auto result = initialize();
			if (result != NVAPI_OK) {
				logger::debug("[NVAPI DRS] NvAPI_Initialize failed: {}", Describe(result));
				return false;
			}

			initialized = true;
			return true;
		}

		[[nodiscard]] std::string Describe(NvAPI_Status a_status) const
		{
			NvAPI_ShortString message{};
			if (getErrorMessage && getErrorMessage(a_status, message) == NVAPI_OK)
				return std::format("{} ({})", message, std::to_underlying(a_status));
			return std::to_string(std::to_underlying(a_status));
		}

		Initialize initialize{};
		Unload unload{};
		GetErrorMessage getErrorMessage{};
		CreateSession createSession{};
		DestroySession destroySession{};
		LoadSettings loadSettings{};
		SaveSettings saveSettings{};
		FindApplicationByName findApplicationByName{};
		FindProfileByName findProfileByName{};
		GetSetting getSetting{};
		SetSetting setSetting{};

	private:
		HMODULE module{};
		bool initialized{};
	};

	[[nodiscard]] bool CopyUnicodeString(std::wstring_view a_source, NvAPI_UnicodeString& a_destination)
	{
		static_assert(sizeof(wchar_t) == sizeof(NvU16));
		if (a_source.size() >= std::size(a_destination))
			return false;

		std::ranges::fill(a_destination, NvU16{});
		for (std::size_t i = 0; i < a_source.size(); ++i)
			a_destination[i] = static_cast<NvU16>(a_source[i]);
		return true;
	}
}

namespace Util::NvApiDrs
{
	void EnsureSkyrimSEDLSSGAllowed()
	{
		NvApi api;
		if (!api.Load())
			return;

		NvDRSSessionHandle session{};
		auto result = api.createSession(&session);
		if (result != NVAPI_OK) {
			logger::warn("[NVAPI DRS] Could not create a driver-settings session: {}", api.Describe(result));
			return;
		}

		const SKSE::stl::scope_exit destroySession([&]() noexcept {
			(void)api.destroySession(session);
		});

		result = api.loadSettings(session);
		if (result != NVAPI_OK) {
			logger::warn("[NVAPI DRS] Could not load NVIDIA driver settings: {}", api.Describe(result));
			return;
		}

		NvDRSProfileHandle profile{};
		result = NVAPI_EXECUTABLE_NOT_FOUND;
		if (api.findApplicationByName) {
			wchar_t executablePath[NVAPI_UNICODE_STRING_MAX]{};
			const DWORD executablePathLength = GetModuleFileNameW(
				nullptr,
				executablePath,
				static_cast<DWORD>(std::size(executablePath)));
			NvAPI_UnicodeString applicationName{};
			if (executablePathLength > 0 &&
				executablePathLength < std::size(executablePath) &&
				CopyUnicodeString(
					std::wstring_view(executablePath, executablePathLength),
					applicationName)) {
				NVDRS_APPLICATION application{};
				application.version = NVDRS_APPLICATION_VER;
				result = api.findApplicationByName(
					session,
					applicationName,
					&profile,
					&application);
			}
		}

		if (result != NVAPI_OK) {
			NvAPI_UnicodeString profileName{};
			if (!CopyUnicodeString(kSkyrimSEProfileName, profileName)) {
				logger::warn("[NVAPI DRS] Skyrim SE driver profile name exceeds the NVAPI limit");
				return;
			}
			result = api.findProfileByName(session, profileName, &profile);
		}
		if (result == NVAPI_PROFILE_NOT_FOUND) {
			logger::debug("[NVAPI DRS] Skyrim SE has no named NVIDIA driver profile");
			return;
		}
		if (result != NVAPI_OK) {
			logger::warn("[NVAPI DRS] Could not inspect the Skyrim SE driver profile: {}", api.Describe(result));
			return;
		}

		NVDRS_SETTING setting{};
		setting.version = NVDRS_SETTING_VER;
		result = api.getSetting(session, profile, static_cast<NvU32>(NGX_DLSSG_MODE_ID), &setting);
		if (result == NVAPI_SETTING_NOT_FOUND) {
			logger::debug("[NVAPI DRS] Skyrim SE uses the default DLSS-G driver setting");
			return;
		}
		if (result != NVAPI_OK) {
			logger::warn("[NVAPI DRS] Could not inspect the Skyrim SE DLSS-G setting: {}", api.Describe(result));
			return;
		}

		if (setting.settingType != NVDRS_DWORD_TYPE) {
			logger::warn("[NVAPI DRS] Skyrim SE DLSS-G setting has an unexpected value type");
			return;
		}

		if (setting.u32CurrentValue == static_cast<NvU32>(NGX_DLSSG_MODE_DISABLED)) {
			logger::debug("[NVAPI DRS] Skyrim SE uses the default DLSS-G driver setting");
			return;
		}
		const NvU32 previousValue = setting.u32CurrentValue;

		NVDRS_SETTING replacement{};
		replacement.version = NVDRS_SETTING_VER;
		replacement.settingId = static_cast<NvU32>(NGX_DLSSG_MODE_ID);
		replacement.settingType = NVDRS_DWORD_TYPE;
		replacement.u32CurrentValue = static_cast<NvU32>(NGX_DLSSG_MODE_DISABLED);

		result = api.setSetting(session, profile, &replacement);
		if (result == NVAPI_OK)
			result = api.saveSettings(session);

		if (result != NVAPI_OK) {
			logger::warn(
				"[NVAPI DRS] Could not clear the NVIDIA App DLSS-G override: {}. "
				"Disable the DLSS override for Skyrim in the NVIDIA App",
				api.Describe(result));
			return;
		}

		logger::info(
			"[NVAPI DRS] Cleared the NVIDIA App DLSS-G override for Skyrim SE (was {})",
			previousValue);
	}
}
