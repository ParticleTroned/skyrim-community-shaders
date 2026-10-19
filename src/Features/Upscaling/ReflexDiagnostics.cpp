#include "ReflexDiagnostics.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Streamline.h"
#	include "Features/Upscaling.h"
#	include "BuildProvenance.h"
#	include "ReflexDiagnosticSuite.h"
#	include "ReflexPresentation.h"
#	include "Menu.h"
#	include "ShaderCache.h"
#	include "State.h"
#	include "Utils/CryptoHash.h"
#	include "Utils/OpenVRFrameTiming.h"
#	include "Utils/VRFrameTiming.h"
#	include "Utils/WinApi.h"
#	include <filesystem>
#	include <fstream>
#	include <nlohmann/json.hpp>
#	include <nvapi.h>
#	include <nvapi_interface.h>

namespace ReflexDiagnostics
{
	namespace
	{
		using json = nlohmann::json;
		Capture<> capture;
		std::mutex controlMutex;
		json environment;

#	ifdef ENABLE_SKYRIM_VR
		struct HeadsetMetadata
		{
			bool available = false, fault = false;
			float refreshHz = 0;
			uint32_t width = 0, height = 0;
			char model[256]{}, trackingSystem[256]{};
			int refreshError = -1, modelError = -1, trackingError = -1;
			bool settings[4]{};
			int settingsErrors[4]{ -1, -1, -1, -1 };
			vr::Compositor_CumulativeStats cumulative{};
			bool hasCumulative = false;
		};
		void ReadHeadset(HeadsetMetadata* data)
		{
			__try {
				if (auto* system = RE::BSOpenVR::GetIVRSystem()) {
					data->available = true;
					system->GetRecommendedRenderTargetSize(&data->width, &data->height);
					vr::ETrackedPropertyError error;
					data->refreshHz = system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &error);
					data->refreshError = error;
					system->GetStringTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_ModelNumber_String, data->model, sizeof(data->model), &error);
					data->modelError = error;
					system->GetStringTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String, data->trackingSystem, sizeof(data->trackingSystem), &error);
					data->trackingError = error;
				}
				if (auto* settings = RE::BSOpenVR::GetIVRSettings()) {
					const char* names[]{ "allowAsyncReprojection", "allowInterleavedReprojection", "forceReprojection", "motionSmoothing" };
					for (int i = 0; i < 4; ++i) {
						vr::EVRSettingsError error;
						data->settings[i] = settings->GetBool("steamvr", names[i], &error);
						data->settingsErrors[i] = error;
					}
				}
				if (auto* compositor = RE::BSOpenVR::GetIVRCompositor()) {
					compositor->GetCumulativeStats(&data->cumulative, sizeof(data->cumulative));
					data->hasCumulative = true;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				data->fault = true;
			}
		}
#	endif

		template <class T>
		T DriverFunction(std::string_view name)
		{
			const auto module = GetModuleHandleW(L"nvapi64.dll");
			using Query = void*(__cdecl*)(NvU32);
			const auto query = module ? reinterpret_cast<Query>(GetProcAddress(module, "nvapi_QueryInterface")) : nullptr;
			if (query) {
				for (const auto& entry : nvapi_interface_table)
					if (name == entry.func)
						return reinterpret_cast<T>(query(entry.id));
			}
			return nullptr;
		}

		template <class T>
		json Optional(const std::optional<T>& value)
		{
			return value ? json(*value) : json(nullptr);
		}

		json OptionsJson(const Options& options)
		{
			return { { "mode", options.mode }, { "frameLimitUs", options.frameLimitUs },
				{ "markersEffective", options.markers } };
		}

		json ResultJson(const std::optional<int32_t>& result)
		{
			if (!result)
				return nullptr;
			return { { "code", *result }, { "name", magic_enum::enum_name(static_cast<sl::Result>(*result)) } };
		}

		json LoadedModules(bool hashFiles)
		{
			auto modules = json::array();
			for (const auto* name : { L"CommunityShaders.dll", L"sl.interposer.dll", L"sl.common.dll", L"sl.reflex.dll", L"sl.pcl.dll", L"sl.dlss.dll", L"sl.dlss_g.dll", L"nvngx_dlss.dll", L"nvngx_dlssnr.dll", L"nvngx_dlssg.dll", L"nvapi64.dll", L"nvldumdx.dll", L"openvr_api.dll", L"vrclient_x64.dll", L"openxr_loader.dll", L"dxgi.dll", L"d3d11.dll", L"SkyrimVR.exe", L"SkyrimSE.exe" }) {
				const auto module = GetModuleHandleW(name);
				json entry{ { "name", std::filesystem::path(name).string() }, { "loaded", module != nullptr },
					{ "path", nullptr }, { "version", nullptr } };
				if (module) {
					std::wstring path(32768, L'\0');
					const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
					if (length && length < path.size()) {
						path.resize(length);
						entry["path"] = std::filesystem::path(path).string();
						if (const auto version = Util::GetDllVersion(path))
							entry["version"] = version->string();
						if (hashFiles) {
							std::error_code error;
							const auto size = std::filesystem::file_size(path, error);
							entry["sizeBytes"] = error ? json(nullptr) : json(size);
							entry["sha256"] = nullptr;
							if (!error && size <= 512ull * 1024 * 1024) {
								std::ifstream file(std::filesystem::path(path), std::ios::binary);
								std::string bytes(static_cast<std::size_t>(size), '\0');
								if (file.read(bytes.data(), static_cast<std::streamsize>(size)))
									entry["sha256"] = Util::CryptoHash::Sha256Hex(bytes);
								else
									entry["hashError"] = "file read failed";
							} else {
								entry["hashError"] = error ? error.message() : "file exceeds 512 MiB hash bound";
							}
						}
					} else {
						entry["pathError"] = GetLastError();
					}
				}
				modules.push_back(std::move(entry));
			}
			return modules;
		}

		json CompositorJson(const Record::Compositor& c)
		{
			return { { "frame", c.frame }, { "systemTime", c.systemTime }, { "presents", c.presents },
				{ "mispresented", c.mispresented }, { "dropped", c.dropped }, { "reprojectionFlags", c.reprojection },
				{ "clientMs", c.clientMs }, { "gpuMs", c.gpuMs }, { "preGpuMs", c.preGpuMs }, { "postGpuMs", c.postGpuMs },
				{ "compositorGpuMs", c.compositorGpuMs }, { "compositorCpuMs", c.compositorCpuMs }, { "compositorIdleMs", c.compositorIdleMs },
				{ "presentMs", c.presentMs }, { "waitPresentMs", c.waitPresentMs }, { "submitMs", c.submitMs },
				{ "waitPosesMs", c.waitPosesMs }, { "posesReadyMs", c.posesReadyMs }, { "frameReadyMs", c.frameReadyMs },
				{ "compositorUpdateStartMs", c.compositorUpdateStartMs }, { "compositorUpdateEndMs", c.compositorUpdateEndMs },
				{ "compositorRenderStartMs", c.compositorRenderStartMs }, { "hmdPose", c.hmdPose }, { "poseValid", c.poseValid } };
		}

		json RecordJson(const Record& record)
		{
			auto gpus = json::array();
			for (uint32_t i = 0; i < record.gpuCount; ++i) {
				const auto& g = record.gpuTelemetry[i];
				gpus.push_back({ { "index", g.index }, { "clockResult", Optional(g.clockResult) },
					{ "utilizationResult", Optional(g.utilizationResult) }, { "pstateResult", Optional(g.pstateResult) },
					{ "graphicsKHz", Optional(g.graphicsKHz) }, { "memoryKHz", Optional(g.memoryKHz) },
					{ "utilizationPercent", Optional(g.utilizationPercent) }, { "pstate", Optional(g.pstate) } });
			}
			auto history = json::array();
			for (uint32_t i = 0; i < record.compositorCount; ++i)
				history.push_back(CompositorJson(record.compositorHistory[i]));
			return {
				{ "sequence", record.sequence }, { "frame", record.frame }, { "threadId", record.threadId },
				{ "tick", record.tick }, { "hookIntervalTicks", record.intervalTicks },
				{ "context", { { "available", record.contextAvailable }, { "paused", record.paused }, { "menuOpen", record.menuOpen },
								 { "compiling", record.compiling }, { "gameHour", Optional(record.gameHour) }, { "cellId", record.cellId }, { "playerPosition", record.playerPosition } } },
				{ "queryEndTick", record.queryEndTick }, { "observerTicks", record.observerTicks },
				{ "gpuEnumerationResult", Optional(record.gpuEnumerationResult) }, { "gpuTelemetry", std::move(gpus) },
				{ "driver", { { "queryDevice", record.driverDevice }, { "rendererDevice", record.rendererDevice }, { "streamlineDevice", record.streamlineDevice }, { "result", Optional(record.driverStatus) }, { "nativeInterfaceResult", ResultJson(record.nativeInterfaceResult) }, { "queryTicks", record.driverQueryTicks },
								{ "lowLatencyEnabled", Optional(record.driverLowLatency) }, { "gameSleepSeen", Optional(record.driverGameSleep) },
								{ "sleepIntervalUs", Optional(record.driverSleepUs) }, { "fullscreenVrr", Optional(record.driverVrr) },
								{ "controlPanelVsync", Optional(record.driverVsync) }, { "fullscreenIFlip", Optional(record.driverIFlip) },
								{ "frameGenerationMultiplier", Optional(record.driverFrameGeneration) } } },
				{ "compositorQuery", record.compositorQuery }, { "compositorFault", record.compositorFault },
				{ "compositorQueryTicks", record.compositorQueryTicks }, { "compositor", record.compositor ? CompositorJson(*record.compositor) : json(nullptr) },
				{ "compositorHistory", std::move(history) },
				{ "initialized", record.initialized }, { "adapterSupported", record.supported },
				{ "featureLoaded", record.featureLoaded }, { "pclLoaded", record.pclLoaded },
				{ "functions", { { "setOptions", record.setOptionsBound }, { "sleep", record.sleepBound }, { "getState", record.getStateBound } } },
				{ "requested", { { "mode", record.requested.mode }, { "boost", record.boostRequested },
								   { "fpsLimitEnabled", record.capRequested }, { "fpsLimit", record.requestedFPS }, { "markers", record.markersRequested } } },
				{ "lastSuccessfulOptions", record.appliedValid ? OptionsJson(record.applied) : json(nullptr) },
				{ "resolvedOptions", record.resolved ? OptionsJson(*record.resolved) : json(nullptr) },
				{ "optionsCallSequence", record.optionsSequence }, { "lastOptionsResult", ResultJson(record.lastOptionsResult) },
				{ "blockedByFrameGeneration", record.blockedByFrameGeneration }, { "frameGenerationQuarantined", record.frameGenerationQuarantined },
				{ "sleep", { { "reason", record.reason }, { "result", ResultJson(record.sleepResult) },
							   { "durationTicks", record.sleepResult ? json(record.sleepTicks) : json(nullptr) }, { "tokenAddress", Optional(record.tokenAddress) } } },
				{ "state", { { "queryScheduled", record.queryState }, { "result", ResultJson(record.stateResult) },
							   { "tick", record.stateResult ? json(record.stateTick) : json(nullptr) },
							   { "durationTicks", record.stateResult ? json(record.stateTicks) : json(nullptr) },
							   { "lowLatencyAvailable", Optional(record.lowLatencyAvailable) },
							   { "latencyReportAvailable", Optional(record.latencyReportAvailable) } } }
			};
		}

		json SnapshotJson(const Capture<>::Snapshot& snapshot)
		{
			auto records = json::array();
			for (const auto& record : snapshot.records)
				records.push_back(RecordJson(record));
			return { { "schemaVersion", 3 }, { "sessionId", snapshot.sessionId },
				{ "active", snapshot.active }, { "inFlight", snapshot.inFlight }, { "stopReason", snapshot.stopReason },
				{ "startTick", snapshot.startTick }, { "deadlineTick", snapshot.deadlineTick }, { "qpcFrequency", snapshot.frequency },
				{ "retainedRecords", snapshot.retained }, { "capacity", 16384 }, { "droppedRecords", snapshot.dropped },
				{ "retainedTotals", { { "sleepCalls", snapshot.totals.sleepCalls }, { "sleepFailures", snapshot.totals.sleepFailures },
										{ "sleepDurationTicks", snapshot.totals.sleepTicks }, { "sleepSkipped", snapshot.totals.skipped },
										{ "stateQueries", snapshot.totals.stateQueries }, { "stateFailures", snapshot.totals.stateFailures },
										{ "lowLatencyUnavailableQueries", snapshot.totals.lowLatencyUnavailable } } },
				{ "queryState", snapshot.queryState }, { "stateQueryIntervalMs", 1000 },
				{ "observerTimingDefinition", "Context and query observation time; excludes record initialization and journal commit. Export runs outside timed captures." },
				{ "environment", environment }, { "records", std::move(records) },
				{ "interpretation", "Hook intervals are CPU input-hook cadence, not compositor frame times. Availability is not enabled state. eOk or short sleep alone does not prove pacing. State queries can affect SDK availability after driver-query failure; compare queryState=false. No full-frame marker coverage, so latency reports are not end-to-end latency evidence." } };
		}

		bool UnsignedArgument(const json& args, const char* key, uint64_t& value, uint64_t minimum, uint64_t maximum)
		{
			if (!args.contains(key))
				return true;
			const auto& input = args[key];
			if (!input.is_number_integer() || (!input.is_number_unsigned() && input.get<int64_t>() < 0))
				return false;
			value = input.get<uint64_t>();
			return value >= minimum && value <= maximum;
		}
	}

	uint64_t Tick()
	{
		LARGE_INTEGER tick{};
		QueryPerformanceCounter(&tick);
		return static_cast<uint64_t>(tick.QuadPart);
	}

	nlohmann::json Environment(bool hashFiles)
	{
		if (globals::game::quitGame)
			return { { "error", "engine shutdown; runtime pointers were not accessed" } };
		json result{ { "producer", BuildProvenance::GetProducer() },
			{ "runtime", REL::Module::IsVR() ? "VR" : REL::Module::IsAE() ? "AE" :
																			"SE" },
			{ "loadedModules", LoadedModules(hashFiles) }, { "adapter", nullptr }, { "driver", nullptr } };
		if (auto getDriver = DriverFunction<decltype(&NvAPI_SYS_GetDriverAndBranchVersion)>("NvAPI_SYS_GetDriverAndBranchVersion")) {
			NvU32 version{};
			NvAPI_ShortString branch{};
			const auto status = getDriver(&version, branch);
			result["driver"] = { { "result", status }, { "version", status == NVAPI_OK ? json(version) : json(nullptr) },
				{ "branch", status == NVAPI_OK ? json(branch) : json(nullptr) } };
		}
		if (auto enumerate = DriverFunction<decltype(&NvAPI_EnumPhysicalGPUs)>("NvAPI_EnumPhysicalGPUs")) {
			NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS]{};
			NvU32 count = 0;
			const auto status = enumerate(handles, &count);
			result["physicalGpus"] = { { "enumerationResult", status }, { "count", count }, { "sampleLimit", 4 }, { "devices", json::array() } };
			if (auto name = DriverFunction<decltype(&NvAPI_GPU_GetFullName)>("NvAPI_GPU_GetFullName"); status == NVAPI_OK && name) {
				for (NvU32 i = 0; i < std::min<NvU32>(count, 4); ++i) {
					NvAPI_ShortString text{};
					const auto nameStatus = name(handles[i], text);
					result["physicalGpus"]["devices"].push_back({ { "index", i }, { "nameResult", nameStatus }, { "name", nameStatus == NVAPI_OK ? json(text) : json(nullptr) } });
				}
			}
		}
		if (globals::d3d::device) {
			winrt::com_ptr<IDXGIDevice> device;
			winrt::com_ptr<IDXGIAdapter> adapter;
			DXGI_ADAPTER_DESC description{};
			if (SUCCEEDED(globals::d3d::device->QueryInterface(__uuidof(IDXGIDevice), device.put_void())) &&
				SUCCEEDED(device->GetAdapter(adapter.put())) && SUCCEEDED(adapter->GetDesc(&description))) {
				result["adapter"] = { { "name", std::filesystem::path(description.Description).string() },
					{ "adapterLuidLow", description.AdapterLuid.LowPart }, { "adapterLuidHigh", description.AdapterLuid.HighPart },
					{ "vendorId", description.VendorId }, { "deviceId", description.DeviceId },
					{ "dedicatedVideoMemory", description.DedicatedVideoMemory }, { "subsystemId", description.SubSysId } };
			}
		}
		result["presentation"] = Presentation::Environment();
		result["swapChain"] = result["presentation"].value("descriptor", json(nullptr));
#	ifdef ENABLE_SKYRIM_VR
		if (globals::game::isVR) {
			HeadsetMetadata h;
			ReadHeadset(&h);
			result["headset"] = { { "available", h.available }, { "fault", h.fault },
				{ "refreshHz", h.refreshError == 0 && !h.fault ? json(h.refreshHz) : json(nullptr) },
				{ "model", h.modelError == 0 && !h.fault ? json(h.model) : json(nullptr) },
				{ "trackingSystem", h.trackingError == 0 && !h.fault ? json(h.trackingSystem) : json(nullptr) },
				{ "recommendedWidth", h.width }, { "recommendedHeight", h.height },
				{ "refreshError", h.refreshError }, { "modelError", h.modelError }, { "trackingError", h.trackingError } };
			const char* names[]{ "allowAsyncReprojection", "allowInterleavedReprojection", "forceReprojection", "motionSmoothing" };
			for (int i = 0; i < 4; ++i)
				result["headset"]["runtimeSettings"][names[i]] = { { "error", h.settingsErrors[i] }, { "value", h.settingsErrors[i] == 0 && !h.fault ? json(h.settings[i]) : json(nullptr) } };
			result["headset"]["cumulative"] = h.hasCumulative && !h.fault ? json{ { "pid", h.cumulative.m_nPid },
				{ "presents", h.cumulative.m_nNumFramePresents }, { "dropped", h.cumulative.m_nNumDroppedFrames },
				{ "reprojected", h.cumulative.m_nNumReprojectedFrames } } :
			                                                                json(nullptr);
		}
#	endif
		return result;
	}

	static void ReadDriverState(Record& record, Streamline& streamline)
	{
		const auto function = DriverFunction<decltype(&NvAPI_D3D_GetSleepStatus)>("NvAPI_D3D_GetSleepStatus");
		if (!function || !globals::d3d::device)
			return;
		NV_GET_SLEEP_STATUS_PARAMS status{};
		status.version = NV_GET_SLEEP_STATUS_PARAMS_VER;
		const auto start = Tick();
		winrt::com_ptr<IUnknown> nativeDevice;
		if (streamline.initialized && streamline.slGetNativeInterface) {
			record.nativeInterfaceResult = static_cast<int32_t>(streamline.slGetNativeInterface(globals::d3d::device, nativeDevice.put_void()));
			if (*record.nativeInterfaceResult != static_cast<int32_t>(sl::Result::eOk) || !nativeDevice) {
				record.driverQueryTicks = Tick() - start;
				return;
			}
		}
		record.driverDevice = reinterpret_cast<uint64_t>(nativeDevice ? nativeDevice.get() : globals::d3d::device);
		record.rendererDevice = reinterpret_cast<uint64_t>(globals::d3d::device);
		record.streamlineDevice = reinterpret_cast<uint64_t>(streamline.boundDeviceIdentity);
		record.driverStatus = function(nativeDevice ? nativeDevice.get() : globals::d3d::device, &status);
		record.driverQueryTicks = Tick() - start;
		if (*record.driverStatus == NVAPI_OK) {
			record.driverLowLatency = status.bLowLatencyMode != 0;
			record.driverGameSleep = status.bUseGameSleep != 0;
			record.driverVrr = status.bFsVrr != 0;
			record.driverVsync = status.bCplVsyncOn != 0;
			record.driverIFlip = status.bFullscreenIFlip != 0;
			record.driverSleepUs = status.sleepIntervalUs;
			record.driverFrameGeneration = status.fgMultiplier;
		}
	}

	static void ReadGpuTelemetry(Record& record)
	{
		const auto enumerate = DriverFunction<decltype(&NvAPI_EnumPhysicalGPUs)>("NvAPI_EnumPhysicalGPUs");
		if (!enumerate)
			return;
		NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS]{};
		NvU32 count = 0;
		record.gpuEnumerationResult = enumerate(handles, &count);
		if (*record.gpuEnumerationResult != NVAPI_OK)
			return;
		const auto clocks = DriverFunction<decltype(&NvAPI_GPU_GetAllClockFrequencies)>("NvAPI_GPU_GetAllClockFrequencies");
		const auto utilization = DriverFunction<decltype(&NvAPI_GPU_GetDynamicPstatesInfoEx)>("NvAPI_GPU_GetDynamicPstatesInfoEx");
		const auto pstate = DriverFunction<decltype(&NvAPI_GPU_GetCurrentPstate)>("NvAPI_GPU_GetCurrentPstate");
		record.gpuCount = static_cast<uint32_t>(std::min<std::size_t>(count, record.gpuTelemetry.size()));
		for (uint32_t i = 0; i < record.gpuCount; ++i) {
			auto& g = record.gpuTelemetry[i];
			g.index = i;
			if (clocks) {
				NV_GPU_CLOCK_FREQUENCIES values{};
				values.version = NV_GPU_CLOCK_FREQUENCIES_VER;
				values.ClockType = NV_GPU_CLOCK_FREQUENCIES_CURRENT_FREQ;
				g.clockResult = clocks(handles[i], &values);
				if (*g.clockResult == NVAPI_OK) {
					if (values.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].bIsPresent)
						g.graphicsKHz = values.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency;
					if (values.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].bIsPresent)
						g.memoryKHz = values.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].frequency;
				}
			}
			if (utilization) {
				NV_GPU_DYNAMIC_PSTATES_INFO_EX values{};
				values.version = NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER;
				g.utilizationResult = utilization(handles[i], &values);
				if (*g.utilizationResult == NVAPI_OK && values.utilization[0].bIsPresent)
					g.utilizationPercent = values.utilization[0].percentage;
			}
			if (pstate) {
				NV_GPU_PERF_PSTATE_ID value{};
				g.pstateResult = pstate(handles[i], &value);
				if (*g.pstateResult == NVAPI_OK)
					g.pstate = static_cast<uint32_t>(value);
			}
		}
	}

	static void ReadCompositor(Record& record)
	{
#	ifdef ENABLE_SKYRIM_VR
		if (!globals::game::isVR)
			return;
		static std::mutex timingMutex;
		std::unique_lock timingLock(timingMutex, std::try_to_lock);
		if (!timingLock.owns_lock())
			return;
		static uint64_t session = 0;
		static uint32_t lastFrame = UINT32_MAX;
		static bool disabled = false;
		static Util::VRFrameTiming::Cursor cursor;
		if (session != record.sessionId) {
			session = record.sessionId;
			lastFrame = UINT32_MAX;
			disabled = false;
			cursor = {};
		}
		if (lastFrame == record.frame || disabled)
			return;
		lastFrame = record.frame;
		record.compositorQuery = true;
		const auto start = Tick();
		auto* compositor = Util::OpenVRFrameTiming::TryResolveCompositor(&record.compositorFault);
		std::array<vr::Compositor_FrameTiming, Util::VRFrameTiming::kMaxCatchUpFrames + Util::VRFrameTiming::kCompletionLagFrames> history{};
		const auto count = record.compositorFault ? 0 : Util::VRFrameTiming::ReadRecentHistory(std::span<vr::Compositor_FrameTiming>(history), cursor.lastFrameIndex, [&](auto& timing, uint32_t framesAgo) {
			return Util::OpenVRFrameTiming::TryGetFrameTiming(compositor, &timing, &record.compositorFault, framesAgo);
		});
		cursor.Consume(std::span<const vr::Compositor_FrameTiming>(history.data(), count), [&](const auto& t) {
			Record::Compositor c;
			c.frame = t.m_nFrameIndex;
			c.systemTime = t.m_flSystemTimeInSeconds;
			c.presents = t.m_nNumFramePresents;
			c.mispresented = t.m_nNumMisPresented;
			c.dropped = t.m_nNumDroppedFrames;
			c.reprojection = t.m_nReprojectionFlags;
			c.clientMs = t.m_flClientFrameIntervalMs;
			c.gpuMs = t.m_flTotalRenderGpuMs;
			c.preGpuMs = t.m_flPreSubmitGpuMs;
			c.postGpuMs = t.m_flPostSubmitGpuMs;
			c.compositorGpuMs = t.m_flCompositorRenderGpuMs;
			c.compositorCpuMs = t.m_flCompositorRenderCpuMs;
			c.compositorIdleMs = t.m_flCompositorIdleCpuMs;
			c.presentMs = t.m_flPresentCallCpuMs;
			c.waitPresentMs = t.m_flWaitForPresentCpuMs;
			c.submitMs = t.m_flSubmitFrameMs;
			c.waitPosesMs = t.m_flWaitGetPosesCalledMs;
			c.posesReadyMs = t.m_flNewPosesReadyMs;
			c.frameReadyMs = t.m_flNewFrameReadyMs;
			c.compositorUpdateStartMs = t.m_flCompositorUpdateStartMs;
			c.compositorUpdateEndMs = t.m_flCompositorUpdateEndMs;
			c.compositorRenderStartMs = t.m_flCompositorRenderStartMs;
			c.poseValid = t.m_HmdPose.bPoseIsValid;
			for (int row = 0; row < 3; ++row)
				std::copy_n(t.m_HmdPose.mDeviceToAbsoluteTracking.m[row], 4, c.hmdPose.begin() + row * 4);
			record.compositor = c;
			if (record.compositorCount < record.compositorHistory.size())
				record.compositorHistory[record.compositorCount++] = c;
		});
		record.compositorQueryTicks = Tick() - start;
		disabled = record.compositorFault;
#	else
		(void)record;
#	endif
	}

	Scope::Scope(Streamline& streamline) : streamline_(streamline)
	{
		if (globals::game::quitGame)
			return;
		Suite::RestorePending();
		const auto entryTick = Tick();
		active_ = capture.Begin(entryTick, record);
		if (!active_)
			return;
		record.frame = globals::state ? globals::state->frameCount : 0;
		record.threadId = GetCurrentThreadId();
		record.initialized = streamline.initialized;
		record.supported = streamline.reflexSupportedOnCurrentAdapter;
		record.featureLoaded = streamline.featureReflex;
		record.pclLoaded = streamline.featurePCL;
		record.setOptionsBound = streamline.slReflexSetOptions != nullptr;
		record.sleepBound = streamline.slReflexSleep != nullptr;
		record.getStateBound = streamline.slReflexGetState != nullptr;
		const auto& settings = globals::features::upscaling.settings;
		record.requested.mode = !settings.reflexLowLatencyMode ? 0 : settings.reflexLowLatencyBoost ? 2 :
		                                                                                              1;
		record.boostRequested = settings.reflexLowLatencyBoost;
		record.markersRequested = settings.reflexUseMarkersToOptimize;
		record.capRequested = settings.reflexUseFPSLimit;
		record.requestedFPS = settings.reflexFPSLimit;
		record.blockedByFrameGeneration = globals::features::upscaling.IsFrameGenerationDx12PathActive();
		record.reason = "unavailable";
		record.contextAvailable = globals::game::ui && globals::menu && globals::shaderCache;
		record.paused = globals::game::ui && globals::game::ui->GameIsPaused();
		record.menuOpen = globals::menu && globals::menu->IsEnabled;
		record.compiling = globals::shaderCache && globals::shaderCache->IsCompiling();
		if (globals::game::calendar)
			record.gameHour = globals::game::calendar->GetHour();
		if (const auto player = RE::PlayerCharacter::GetSingleton()) {
			if (const auto cell = player->GetParentCell())
				record.cellId = cell->GetFormID();
			const auto position = player->GetPosition();
			record.playerPosition = { position.x, position.y, position.z };
		}
		record.observerTicks = Tick() - entryTick;
	}

	Scope::~Scope()
	{
		if (!active_)
			return;
		if (globals::game::quitGame) {
			record.contextAvailable = false;
			capture.Finish(record);
			return;
		}
		const auto observationStart = Tick();
		ReadCompositor(record);
		const auto& cache = streamline_.reflexOptionsCache;
		record.appliedValid = cache.valid;
		record.applied = { static_cast<int>(cache.mode), cache.frameLimitUs, cache.useMarkersToOptimize };
		record.optionsSequence = streamline_.reflexOptionsSequence.load();
		record.lastOptionsResult = streamline_.reflexLastOptionsResult.load();
		record.frameGenerationQuarantined = streamline_.frameGenerationQuarantinedByReflex;
		// The SDK state query is not thread safe and must stay on the Reflex hook thread.
		if (record.queryState && record.initialized && record.supported && record.featureLoaded && streamline_.slReflexGetState) {
			sl::ReflexState state{};
			record.stateTick = Tick();
			const auto result = streamline_.slReflexGetState(state);
			record.stateTicks = Tick() - record.stateTick;
			record.stateResult = static_cast<int32_t>(result);
			if (result == sl::Result::eOk) {
				record.lowLatencyAvailable = state.lowLatencyAvailable;
				record.latencyReportAvailable = state.latencyReportAvailable;
			}
		}
		if (record.queryState) {
			ReadDriverState(record, streamline_);
			ReadGpuTelemetry(record);
			record.queryEndTick = Tick();
		}
		record.observerTicks += Tick() - observationStart;
		capture.Finish(record);
	}

	static nlohmann::json DispatchLocked(const nlohmann::json& args)
	{
		const auto action = args.value("action", std::string{});
		uint64_t session = 0, seconds = 30, after = 0, presentAfter = 0, limit = 32;
		if (!UnsignedArgument(args, "expectedSessionId", session, 1, UINT64_MAX) ||
			!UnsignedArgument(args, "durationSeconds", seconds, 1, 120) ||
			!UnsignedArgument(args, "afterSequence", after, 0, UINT64_MAX) ||
			!UnsignedArgument(args, "afterPresentSequence", presentAfter, 0, UINT64_MAX) ||
			!UnsignedArgument(args, "limit", limit, 1, 256))
			return { { "error", "invalid Reflex capture integer argument" } };
		if (args.contains("queryState") && !args["queryState"].is_boolean())
			return { { "error", "queryState must be boolean" } };
		if (action == "reflex_trace_start") {
			if (session)
				return { { "error", "reflex_trace_start allocates a new session; omit expectedSessionId" } };
			LARGE_INTEGER frequency{};
			QueryPerformanceFrequency(&frequency);
			auto metadata = Environment(false);
			if (!capture.Start(Tick(), frequency.QuadPart, static_cast<uint32_t>(seconds), args.value("queryState", true)))
				return { { "error", "stop, drain, read and reset the retained Reflex capture before starting another" }, { "capture", SnapshotJson(capture.Read(Tick())) } };
			const auto started = capture.Read(Tick());
			if (!Presentation::Start(started.sessionId, started.startTick, started.deadlineTick)) {
				(void)capture.Stop(started.sessionId);
				(void)capture.Reset(started.sessionId);
				return { { "error", "Present journal is still active or draining; retained evidence must be recovered" } };
			}
			environment = std::move(metadata);
		} else if (action == "reflex_trace_stop") {
			if (!capture.Stop(session))
				return { { "error", "expectedSessionId must own the retained Reflex capture" } };
			Presentation::Stop();
		} else if (action == "reflex_trace_reset") {
			if (!Presentation::Drained() || !capture.Reset(session))
				return { { "error", "reset requires expectedSessionId and a stopped, drained Reflex capture" } };
			Presentation::Stop();
			environment = nullptr;
		} else if (action != "reflex_trace_read" && action != "reflex_trace_status") {
			return { { "error", "unknown Reflex diagnostic action" } };
		}
		const auto snapshot = capture.Read(Tick(), after, action == "reflex_trace_read" ? static_cast<std::size_t>(limit) : 0);
		if (session && snapshot.sessionId != session && action != "reflex_trace_reset")
			return { { "error", "Reflex capture session changed" } };
		auto result = SnapshotJson(snapshot);
		if (snapshot.sessionId)
			result["desktopPresents"] = Presentation::Read(presentAfter, action == "reflex_trace_read" ? static_cast<size_t>(limit) : 0);
		return { { "action", action }, { "capture", result } };
	}

	nlohmann::json Dispatch(const nlohmann::json& args)
	{
		if (!args.is_object() || !args.contains("action") || !args["action"].is_string())
			return { { "error", "Reflex action must be a string" } };
		const auto action = args["action"].get<std::string>();
		if (action == "reflex_presentation_status" || action == "reflex_presentation_next_launch")
			return Presentation::Configure(args);
		if (action.starts_with("reflex_report_"))
			return Suite::Dispatch(args);
		std::scoped_lock lock(controlMutex);
		if (Suite::Busy() && action != "reflex_trace_status" && action != "reflex_trace_read")
			return { { "error", "a guided Reflex report owns the capture; cancel it through reflex_report_cancel" } };
		return DispatchLocked(args);
	}

	nlohmann::json DispatchOwned(const nlohmann::json& args)
	{
		std::scoped_lock lock(controlMutex);
		return DispatchLocked(args);
	}
}
#endif
