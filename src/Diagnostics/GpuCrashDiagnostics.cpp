#include "Diagnostics/GpuCrashDiagnostics.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "BuildProvenance.h"
#	include "Globals.h"
#	include "ShaderCache.h"
#	include "State.h"
#	include "Util.h"
#	include "Utils/FileSystem.h"
#	include <algorithm>
#	include <array>
#	include <atomic>
#	include <cstring>
#	include <filesystem>
#	include <format>
#	include <mutex>
#	include <nlohmann/json.hpp>
#	include <type_traits>
#	include <winrt/base.h>

namespace CSX::Diagnostics::GpuCrash
{
	namespace
	{
#	ifdef CSX_GPU_CRASH_LOGGING_BOOT_ENABLED
		constexpr bool BootEnabled = CSX_GPU_CRASH_LOGGING_BOOT_ENABLED != 0;
#	else
		constexpr bool BootEnabled = false;
#	endif
		constexpr std::size_t QueryCapacity = 1024;
		constexpr std::size_t CompletedCapacity = 64;
		constexpr std::size_t DrawCapacity = 128;
		constexpr std::size_t DispatchCapacity = 1024;
		constexpr UINT SRVCount = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
		constexpr UINT UAVCount = D3D11_PS_CS_UAV_REGISTER_COUNT;
		using Label = std::array<char, 160>;
		std::atomic_bool instrumentationFault{ false };
		std::atomic_bool enabled{ false };

		template <std::size_t N>
		void CopyText(std::array<char, N>& destination, std::string_view source) noexcept
		{
			const auto count = std::min(source.size(), N - 1);
			if (count)
				std::memcpy(destination.data(), source.data(), count);
			destination[count] = '\0';
		}

		std::int64_t Counter() noexcept
		{
			LARGE_INTEGER counter{};
			QueryPerformanceCounter(&counter);
			return counter.QuadPart;
		}

		struct Stamp
		{
			std::uint64_t ordinal = 0;
			std::uint64_t pass = 0;
			std::int64_t qpc = 0;
			DWORD thread = 0;
			std::uint32_t frame = 0;
			Label name{};
			const char* kind = "checkpoint";
		};

		struct Binding
		{
			UINT slot = 0;
			std::uintptr_t view = 0;
			std::uintptr_t resource = 0;
			Label name{};
			D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
			UINT width = 0, height = 0, depth = 0, mips = 0, arraySize = 0, samples = 0;
			UINT byteWidth = 0, stride = 0, bindFlags = 0, miscFlags = 0;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
			std::array<UINT, 6> viewDescriptor{};
			UINT viewDescriptorBytes = 0;
		};

		struct Note
		{
			Stamp stamp;
			std::uint32_t technique = 0;
			std::uintptr_t geometry = 0;
			std::uintptr_t shader = 0;
			Label shaderName{};
			std::array<UINT, 3> groups{};
			bool replacement = false;
			bool bindingsCaptured = false;
			std::uintptr_t indirectArgs = 0;
			UINT indirectOffset = 0, indirectByteWidth = 0;
			Label indirectName{};
			UINT srvCount = 0, uavCount = 0;
			std::array<Binding, SRVCount> srvs{};
			std::array<Binding, UAVCount> uavs{};
		};

		struct DrawNote
		{
			Stamp stamp;
			std::uint32_t technique = 0;
			std::uintptr_t geometry = 0;
		};

		void ObjectName(ID3D11DeviceChild* object, Label& name) noexcept
		{
			name[0] = '\0';
			if (!object)
				return;
			UINT size = static_cast<UINT>(name.size() - 1);
			if (FAILED(object->GetPrivateData(WKPDID_D3DDebugObjectName, &size, name.data())))
				name[0] = '\0';
			else
				name[std::min<std::size_t>(size, name.size() - 1)] = '\0';
		}

		template <class View>
		Binding DescribeView(View* view, UINT slot) noexcept
		{
			Binding result;
			result.slot = slot;
			if (!view)
				return result;
			using Description = std::conditional_t<std::is_same_v<View, ID3D11ShaderResourceView>, D3D11_SHADER_RESOURCE_VIEW_DESC, D3D11_UNORDERED_ACCESS_VIEW_DESC>;
			Description viewDescription{};
			view->GetDesc(&viewDescription);
			static_assert(sizeof(viewDescription) <= sizeof(result.viewDescriptor));
			std::memcpy(result.viewDescriptor.data(), &viewDescription, sizeof(viewDescription));
			result.viewDescriptorBytes = sizeof(viewDescription);
			result.view = reinterpret_cast<std::uintptr_t>(view);
			winrt::com_ptr<ID3D11Resource> resource;
			view->GetResource(resource.put());
			if (!resource)
				return result;
			result.resource = reinterpret_cast<std::uintptr_t>(resource.get());
			ObjectName(resource.get(), result.name);
			resource->GetType(&result.dimension);
			if (auto texture = resource.try_as<ID3D11Texture2D>()) {
				D3D11_TEXTURE2D_DESC description{};
				texture->GetDesc(&description);
				result.width = description.Width;
				result.height = description.Height;
				result.mips = description.MipLevels;
				result.arraySize = description.ArraySize;
				result.samples = description.SampleDesc.Count;
				result.format = description.Format;
				result.bindFlags = description.BindFlags;
				result.miscFlags = description.MiscFlags;
			} else if (auto buffer = resource.try_as<ID3D11Buffer>()) {
				D3D11_BUFFER_DESC description{};
				buffer->GetDesc(&description);
				result.byteWidth = description.ByteWidth;
				result.stride = description.StructureByteStride;
				result.bindFlags = description.BindFlags;
				result.miscFlags = description.MiscFlags;
			} else if (auto volume = resource.try_as<ID3D11Texture3D>()) {
				D3D11_TEXTURE3D_DESC description{};
				volume->GetDesc(&description);
				result.width = description.Width;
				result.height = description.Height;
				result.depth = description.Depth;
				result.mips = description.MipLevels;
				result.format = description.Format;
				result.bindFlags = description.BindFlags;
				result.miscFlags = description.MiscFlags;
			}
			return result;
		}

		nlohmann::json Json(const Stamp& stamp)
		{
			return { { "ordinal", stamp.ordinal }, { "pass", stamp.pass }, { "kind", stamp.kind },
				{ "name", stamp.name.data() }, { "qpc", stamp.qpc }, { "thread", stamp.thread }, { "frame", stamp.frame } };
		}

		nlohmann::json Json(const Binding& binding)
		{
			return { { "slot", binding.slot }, { "view", binding.view }, { "resource", binding.resource }, { "name", binding.name.data() },
				{ "dimension", static_cast<unsigned>(binding.dimension) }, { "width", binding.width }, { "height", binding.height },
				{ "depth", binding.depth }, { "mips", binding.mips }, { "arraySize", binding.arraySize }, { "samples", binding.samples },
				{ "format", static_cast<unsigned>(binding.format) }, { "byteWidth", binding.byteWidth }, { "stride", binding.stride },
				{ "bindFlags", binding.bindFlags }, { "miscFlags", binding.miscFlags },
				{ "viewFormat", binding.viewDescriptor[0] }, { "viewDimension", binding.viewDescriptor[1] },
				{ "viewDescriptorWords", binding.viewDescriptor }, { "viewDescriptorBytes", binding.viewDescriptorBytes } };
		}

		struct Recorder
		{
			struct Query
			{
				winrt::com_ptr<ID3D11Query> object;
				Stamp stamp;
			};
			std::mutex mutex;
			winrt::com_ptr<ID3D11Device> device;
			winrt::com_ptr<ID3D11DeviceContext> context;
			std::array<Query, QueryCapacity> queries;
			std::array<Stamp, CompletedCapacity> completed;
			std::array<Note, DispatchCapacity> dispatches;
			std::array<DrawNote, DrawCapacity> draws;
			struct PassLabel
			{
				std::uint64_t token = 0;
				Label name{};
			};
			std::array<PassLabel, 128> passLabels;
			std::size_t queryHead = 0, queryCount = 0, completedCount = 0, drawCount = 0, dispatchCount = 0;
			std::uint64_t nextOrdinal = 0, nextPass = 0, droppedQueries = 0, droppedPassLabels = 0, rejectedThreads = 0;
			DWORD owner = 0;
			bool initialized = false, active = false, failed = false, queryHealthy = true;
			HRESULT initializationResult = S_OK, firstResult = S_OK, deviceReason = S_OK;
			HRESULT nonDeviceResult = S_OK;
			std::uint64_t nonDeviceErrors = 0;
			Stamp nonDeviceError;
			Stamp failure;
			std::filesystem::path reportPath;
			DWORD reportWriteError = ERROR_SUCCESS;

			Stamp MakeStamp(std::string_view name, const char* kind, std::uint64_t pass = 0)
			{
				Stamp stamp{ .ordinal = ++nextOrdinal, .pass = pass, .qpc = Counter(), .thread = GetCurrentThreadId(), .kind = kind };
				if (stamp.thread == owner && globals::state)
					stamp.frame = globals::state->frameCount;
				CopyText(stamp.name, name);
				return stamp;
			}

			bool OwnsContext() noexcept
			{
				if (GetCurrentThreadId() == owner)
					return true;
				++rejectedThreads;
				return false;
			}

			nlohmann::json Snapshot(bool journal)
			{
				nlohmann::json result{
					{ "schema", 1 }, { "bootEnabled", BootEnabled }, { "initialized", initialized }, { "active", active },
					{ "failed", failed }, { "instrumentationFault", instrumentationFault.load(std::memory_order_relaxed) },
					{ "ownerThread", owner }, { "process", GetCurrentProcessId() }, { "queryCapacity", QueryCapacity },
					{ "pendingQueries", queryCount }, { "droppedQueryMarkers", droppedQueries }, { "rejectedContextThreads", rejectedThreads },
					{ "droppedPassLabels", droppedPassLabels },
					{ "queryAcquisitionHealthy", queryHealthy }, { "nonDeviceErrors", nonDeviceErrors },
					{ "lastNonDeviceResult", static_cast<std::uint32_t>(nonDeviceResult) },
					{ "lastNonDeviceError", nonDeviceErrors ? Json(nonDeviceError) : nlohmann::json(nullptr) },
					{ "initializationResult", static_cast<std::uint32_t>(initializationResult) },
					{ "firstFailureResult", static_cast<std::uint32_t>(firstResult) }, { "deviceRemovedReason", static_cast<std::uint32_t>(deviceReason) },
					{ "failure", failed ? Json(failure) : nlohmann::json(nullptr) },
					{ "lastCompleted", completedCount ? Json(completed[(completedCount - 1) % CompletedCapacity]) : nlohmann::json(nullptr) },
					{ "reportPath", reportPath.string() }, { "reportWriteError", reportWriteError },
					{ "interpretation", "Completed event queries prove preceding work finished; pending markers and CPU notes do not identify a guilty GPU instruction. Missing markers and other-thread exclusions are explicit coverage gaps." }
				};
				LARGE_INTEGER frequency{};
				QueryPerformanceFrequency(&frequency);
				result["qpcFrequency"] = frequency.QuadPart;
				if (!journal)
					return result;
				result["producer"] = BuildProvenance::GetProducer();
				if (GetCurrentThreadId() == owner && globals::state) {
					result["engineScreenSize"] = { globals::state->screenSize.x, globals::state->screenSize.y };
					result["frame"] = globals::state->frameCount;
				}
				if (globals::shaderCache) {
					result["shaderCompilation"] = { { "total", globals::shaderCache->GetTotalTasks() },
						{ "completed", globals::shaderCache->GetCompletedTasks() },
						{ "failed", globals::shaderCache->GetFailedTasks() },
						{ "sourceCompiles", globals::shaderCache->GetSourceCompileTasks() },
						{ "diskHits", globals::shaderCache->GetDiskHitTasks() } };
				}
				result["completed"] = nlohmann::json::array();
				for (std::size_t index = completedCount > CompletedCapacity ? completedCount - CompletedCapacity : 0; index < completedCount; ++index)
					result["completed"].push_back(Json(completed[index % CompletedCapacity]));
				result["pending"] = nlohmann::json::array();
				for (std::size_t index = 0; index < queryCount; ++index)
					result["pending"].push_back(Json(queries[(queryHead + index) % QueryCapacity].stamp));
				result["cpuDrawsOverwritten"] = drawCount > DrawCapacity ? drawCount - DrawCapacity : 0;
				result["cpuDispatchesOverwritten"] = dispatchCount > DispatchCapacity ? dispatchCount - DispatchCapacity : 0;
				result["computeBindingCoverage"] = { { "srvSlots", SRVCount }, { "uavSlots", UAVCount },
					{ "uavSlotsAbove7Captured", false }, { "descriptorWordLayout", "Native D3D11 view descriptor DWORDs: format, dimension, then dimension-specific mip/array/element fields; exact byte length retained." } };
				result["cpuDraws"] = nlohmann::json::array();
				for (std::size_t index = drawCount > DrawCapacity ? drawCount - DrawCapacity : 0; index < drawCount; ++index) {
					const auto& draw = draws[index % DrawCapacity];
					auto entry = Json(draw.stamp);
					entry["technique"] = draw.technique;
					entry["geometry"] = draw.geometry;
					result["cpuDraws"].push_back(std::move(entry));
				}
				result["cpuDispatches"] = nlohmann::json::array();
				for (std::size_t index = dispatchCount > DispatchCapacity ? dispatchCount - DispatchCapacity : 0; index < dispatchCount; ++index) {
					const auto& note = dispatches[index % DispatchCapacity];
					auto entry = Json(note.stamp);
					entry["technique"] = note.technique;
					entry["geometry"] = note.geometry;
					entry["groups"] = note.groups;
					entry["groupCountsKnown"] = note.indirectArgs == 0;
					entry["indirectArgs"] = note.indirectArgs;
					entry["indirectOffset"] = note.indirectOffset;
					entry["indirectByteWidth"] = note.indirectByteWidth;
					entry["indirectName"] = note.indirectName.data();
					entry["replacement"] = note.replacement;
					entry["bindingsCaptured"] = note.bindingsCaptured;
					if (note.bindingsCaptured) {
						entry["bindingTime"] = "actual immediate-context dispatch interception";
						entry["computeShader"] = note.shader;
						entry["computeShaderName"] = note.shaderName.data();
						entry["srvs"] = nlohmann::json::array();
						entry["uavs"] = nlohmann::json::array();
						for (UINT srvIndex = 0; srvIndex < note.srvCount; ++srvIndex)
							entry["srvs"].push_back(Json(note.srvs[srvIndex]));
						for (UINT uavIndex = 0; uavIndex < note.uavCount; ++uavIndex)
							entry["uavs"].push_back(Json(note.uavs[uavIndex]));
					}
					result["cpuDispatches"].push_back(std::move(entry));
				}
				return result;
			}

			void Fail(HRESULT result, std::string_view operation)
			{
				if (failed || !BootEnabled || !FAILED(result))
					return;
				deviceReason = device ? device->GetDeviceRemovedReason() : S_OK;
				const bool deviceFailure = FAILED(deviceReason) || result == DXGI_ERROR_DEVICE_REMOVED ||
				                           result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DEVICE_RESET || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
				if (!deviceFailure) {
					nonDeviceResult = result;
					nonDeviceError = MakeStamp(operation, "non_device_error");
					if (++nonDeviceErrors == 1)
						logger::warn("[GpuCrash] Non-device error '{}' result=0x{:08X}; GPU failure latch remains open.", operation, static_cast<std::uint32_t>(result));
					return;
				}
				failed = true;
				active = false;
				firstResult = result;
				failure = MakeStamp(operation, "failure");
				logger::critical("[GpuCrash] First GPU failure '{}' result=0x{:08X} device=0x{:08X} frame={} thread={}; writing completion journal.",
					operation, static_cast<std::uint32_t>(result), static_cast<std::uint32_t>(deviceReason), failure.frame, failure.thread);
				if (auto log = spdlog::default_logger())
					log->flush();
				reportPath = Util::PathHelpers::GetLogPath().parent_path() /
				             std::format("CSX-GPUCrash-{}-{}.json", GetCurrentProcessId(), GetTickCount64());
				const auto serialized = Snapshot(true).dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
				const HANDLE file = CreateFileW(reportPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file == INVALID_HANDLE_VALUE) {
					reportWriteError = GetLastError();
				} else {
					const SKSE::stl::scope_exit close([&] { CloseHandle(file); });
					DWORD written = 0;
					if (!WriteFile(file, serialized.data(), static_cast<DWORD>(serialized.size()), &written, nullptr))
						reportWriteError = GetLastError();
					else if (written != serialized.size())
						reportWriteError = ERROR_WRITE_FAULT;
					else if (!FlushFileBuffers(file))
						reportWriteError = GetLastError();
				}
				logger::critical("[GpuCrash] First failure '{}' result=0x{:08X} device=0x{:08X}; last completed marker={} pending={} dropped={} wrong-thread={} report={} writeError={}",
					operation, static_cast<std::uint32_t>(result), static_cast<std::uint32_t>(deviceReason),
					completedCount ? completed[(completedCount - 1) % CompletedCapacity].ordinal : 0,
					queryCount, droppedQueries, rejectedThreads, reportPath.string(), reportWriteError);
				for (std::size_t index = 0; index < queryCount; ++index) {
					const auto& stamp = queries[(queryHead + index) % QueryCapacity].stamp;
					logger::critical("[GpuCrash] Pending ordinal={} pass={} frame={} thread={} {} '{}'", stamp.ordinal, stamp.pass, stamp.frame, stamp.thread, stamp.kind, stamp.name.data());
				}
				if (auto log = spdlog::default_logger())
					log->flush();
			}

			void Poll()
			{
				while (active && queryHealthy && queryCount) {
					auto& query = queries[queryHead];
					BOOL complete = FALSE;
					const auto result = context->GetData(query.object.get(), &complete, sizeof(complete), D3D11_ASYNC_GETDATA_DONOTFLUSH);
					if (FAILED(result)) {
						queryHealthy = false;
						Fail(result, "completion query GetData");
						return;
					}
					if (result == S_FALSE || !complete)
						break;
					completed[completedCount++ % CompletedCapacity] = query.stamp;
					queryHead = (queryHead + 1) % QueryCapacity;
					--queryCount;
				}
				if (active) {
					const auto reason = device->GetDeviceRemovedReason();
					if (FAILED(reason))
						Fail(reason, "device health checkpoint");
				}
			}

			void Marker(std::string_view name, const char* kind, std::uint64_t pass = 0)
			{
				if (!active || !OwnsContext())
					return;
				Poll();
				if (!active)
					return;
				if (!queryHealthy) {
					++droppedQueries;
					return;
				}
				if (queryCount == QueryCapacity) {
					++droppedQueries;
					return;
				}
				auto& query = queries[(queryHead + queryCount) % QueryCapacity];
				query.stamp = MakeStamp(name, kind, pass);
				context->End(query.object.get());
				++queryCount;
			}
		};

		Recorder& Get()
		{
			static Recorder recorder;
			return recorder;
		}

		template <class Operation>
		void Guarded(Operation&& operation) noexcept
		{
			if (!BootEnabled || instrumentationFault.load(std::memory_order_relaxed))
				return;
			try {
				auto& recorder = Get();
				std::scoped_lock lock(recorder.mutex);
				operation(recorder);
			} catch (...) {
				instrumentationFault.store(true, std::memory_order_relaxed);
				enabled.store(false, std::memory_order_release);
				try {
					logger::critical("[GpuCrash] Diagnostic recorder threw; further acquisition disabled. Any existing failure report remains authoritative.");
					if (auto log = spdlog::default_logger())
						log->flush();
				} catch (...) {
					OutputDebugStringA("CSX GPU crash diagnostics failed; recording disabled.\n");
				}
			}
		}
	}

	void Initialize(ID3D11Device* device, ID3D11DeviceContext* context) noexcept
	{
		Guarded([&](Recorder& recorder) {
			if (recorder.initialized || recorder.failed)
				return;
			recorder.initialized = true;
			recorder.owner = GetCurrentThreadId();
			if (!device || !context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
				recorder.initializationResult = E_INVALIDARG;
				logger::error("[GpuCrash] Cannot arm without an immediate renderer context.");
				return;
			}
			recorder.device.copy_from(device);
			recorder.context.copy_from(context);
			const D3D11_QUERY_DESC description{ D3D11_QUERY_EVENT, 0 };
			for (std::size_t index = 0; index < QueryCapacity; ++index) {
				const auto result = device->CreateQuery(&description, recorder.queries[index].object.put());
				if (FAILED(result)) {
					recorder.initializationResult = result;
					for (auto& query : recorder.queries)
						query.object = nullptr;
					logger::error("[GpuCrash] Query initialization failed result=0x{:08X}; acquisition disabled.", static_cast<std::uint32_t>(result));
					return;
				}
				Util::SetResourceName(recorder.queries[index].object.get(), "GpuCrash::Completion[%u]", static_cast<unsigned>(index));
			}
			recorder.active = true;
			enabled.store(true, std::memory_order_release);
			logger::info("[GpuCrash] Armed {} nonblocking completion queries on thread {}; pass boundaries and bounded CPU dispatch/draw journal enabled.", QueryCapacity, recorder.owner);
		});
	}

	bool IsEnabled() noexcept
	{
		return enabled.load(std::memory_order_acquire);
	}

	std::uint64_t BeginPass(std::string_view name) noexcept
	{
		std::uint64_t token = 0;
		Guarded([&](Recorder& recorder) {
			if (!recorder.active || !recorder.OwnsContext())
				return;
			token = ++recorder.nextPass;
			const auto label = std::find_if(recorder.passLabels.begin(), recorder.passLabels.end(), [](const auto& candidate) { return candidate.token == 0; });
			if (label != recorder.passLabels.end()) {
				label->token = token;
				CopyText(label->name, name);
			} else {
				++recorder.droppedPassLabels;
			}
			recorder.Marker(name, "begin", token);
		});
		return token;
	}

	void EndPass(std::uint64_t token) noexcept
	{
		if (token)
			Guarded([&](Recorder& recorder) {
				const auto label = std::find_if(recorder.passLabels.begin(), recorder.passLabels.end(), [token](const auto& candidate) { return candidate.token == token; });
				recorder.Marker(label != recorder.passLabels.end() ? std::string_view(label->name.data()) : std::string_view("pass label unavailable"), "end", token);
				if (label != recorder.passLabels.end())
					label->token = 0;
			});
	}

	void Checkpoint(std::string_view name) noexcept
	{
		Guarded([&](Recorder& recorder) { recorder.Marker(name, "checkpoint"); });
	}

	void ReportFailure(HRESULT result, std::string_view operation) noexcept
	{
		Guarded([&](Recorder& recorder) { recorder.Fail(result, operation); });
	}

	void RecordDraw(std::string_view name, std::uint32_t technique, std::uintptr_t geometry) noexcept
	{
		Guarded([&](Recorder& recorder) {
			if (!recorder.active)
				return;
			auto& note = recorder.draws[recorder.drawCount++ % DrawCapacity];
			note = {};
			note.stamp = recorder.MakeStamp(name, "cpu_draw");
			note.technique = technique;
			note.geometry = geometry;
		});
	}

	void RecordDispatch(std::string_view name, std::uint32_t technique, UINT x, UINT y, UINT z, bool replacement,
		ID3D11Buffer* indirectArgs, UINT indirectOffset) noexcept
	{
		Guarded([&](Recorder& recorder) {
			if (!recorder.active)
				return;
			auto& note = recorder.dispatches[recorder.dispatchCount++ % DispatchCapacity];
			note.bindingsCaptured = false;
			note.srvCount = note.uavCount = 0;
			note.indirectByteWidth = 0;
			note.indirectName[0] = '\0';
			note.stamp = recorder.MakeStamp(name, "cpu_dispatch");
			note.technique = technique;
			note.groups = { x, y, z };
			note.replacement = replacement;
			note.indirectArgs = reinterpret_cast<std::uintptr_t>(indirectArgs);
			note.indirectOffset = indirectOffset;
			if (!recorder.OwnsContext())
				return;
			if (indirectArgs) {
				D3D11_BUFFER_DESC description{};
				indirectArgs->GetDesc(&description);
				note.indirectByteWidth = description.ByteWidth;
				ObjectName(indirectArgs, note.indirectName);
			}
			winrt::com_ptr<ID3D11ComputeShader> shader;
			recorder.context->CSGetShader(shader.put(), nullptr, nullptr);
			note.shader = reinterpret_cast<std::uintptr_t>(shader.get());
			ObjectName(shader.get(), note.shaderName);
			std::array<ID3D11ShaderResourceView*, SRVCount> srvs{};
			std::array<ID3D11UnorderedAccessView*, UAVCount> uavs{};
			recorder.context->CSGetShaderResources(0, SRVCount, srvs.data());
			recorder.context->CSGetUnorderedAccessViews(0, UAVCount, uavs.data());
			for (UINT slot = 0; slot < SRVCount; ++slot) {
				if (!srvs[slot])
					continue;
				winrt::com_ptr<ID3D11ShaderResourceView> view;
				view.attach(srvs[slot]);
				note.srvs[note.srvCount++] = DescribeView(view.get(), slot);
			}
			for (UINT slot = 0; slot < UAVCount; ++slot) {
				if (!uavs[slot])
					continue;
				winrt::com_ptr<ID3D11UnorderedAccessView> view;
				view.attach(uavs[slot]);
				note.uavs[note.uavCount++] = DescribeView(view.get(), slot);
			}
			note.bindingsCaptured = true;
		});
	}

	nlohmann::json Status()
	{
		auto& recorder = Get();
		std::scoped_lock lock(recorder.mutex);
		return recorder.Snapshot(false);
	}
}
#endif
