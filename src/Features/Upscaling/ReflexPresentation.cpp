#include "ReflexPresentation.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "BuildProvenance.h"
#	include "Features/Upscaling.h"
#	include "ReflexDiagnosticSuite.h"
#	include "ReflexDiagnostics.h"
#	include "Utils/FileSystem.h"
#	include <filesystem>
#	include <fstream>
#	include <nlohmann/json.hpp>

namespace ReflexDiagnostics::Presentation
{
	using json = nlohmann::json;
	namespace
	{
		PresentJournal<> journal;
		std::mutex metadataMutex;
		json creation = nullptr;
		bool consumed = false, nextFlip = false;
		std::string configurationError;
		std::atomic<HWND> observedWindow{ nullptr };
		std::atomic<IDXGISwapChain*> observedSwap{ nullptr };

		uint64_t Address(const void* pointer) { return reinterpret_cast<uint64_t>(pointer); }
		const char* EffectName(DXGI_SWAP_EFFECT effect)
		{
			switch (effect) {
			case DXGI_SWAP_EFFECT_DISCARD:
				return "discard";
			case DXGI_SWAP_EFFECT_SEQUENTIAL:
				return "sequential";
			case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
				return "flip_sequential";
			case DXGI_SWAP_EFFECT_FLIP_DISCARD:
				return "flip_discard";
			default:
				return "unknown";
			}
		}
		json Descriptor(const DXGI_SWAP_CHAIN_DESC& desc)
		{
			return { { "swapEffect", static_cast<uint32_t>(desc.SwapEffect) }, { "swapEffectName", EffectName(desc.SwapEffect) },
				{ "width", desc.BufferDesc.Width }, { "height", desc.BufferDesc.Height }, { "format", desc.BufferDesc.Format },
				{ "refreshNumerator", desc.BufferDesc.RefreshRate.Numerator }, { "refreshDenominator", desc.BufferDesc.RefreshRate.Denominator },
				{ "scanlineOrdering", desc.BufferDesc.ScanlineOrdering }, { "scaling", desc.BufferDesc.Scaling },
				{ "sampleCount", desc.SampleDesc.Count }, { "sampleQuality", desc.SampleDesc.Quality },
				{ "bufferUsage", desc.BufferUsage }, { "bufferCount", desc.BufferCount }, { "window", Address(desc.OutputWindow) },
				{ "windowed", desc.Windowed != FALSE }, { "flags", desc.Flags } };
		}
		json Identity(IUnknown* object)
		{
			if (!object)
				return nullptr;
			winrt::com_ptr<IUnknown> identity;
			const auto hr = object->QueryInterface(__uuidof(IUnknown), identity.put_void());
			return { { "pointer", Address(object) }, { "identityResult", hr },
				{ "identity", SUCCEEDED(hr) ? json(Address(identity.get())) : json(nullptr) } };
		}
		json NativeIdentity(IUnknown* object)
		{
			auto& streamline = globals::features::upscaling.streamline;
			if (!object || !streamline.initialized || !streamline.slGetNativeInterface)
				return { { "result", nullptr }, { "identity", nullptr } };
			winrt::com_ptr<IUnknown> native;
			const auto result = streamline.slGetNativeInterface(object, native.put_void());
			return { { "result", static_cast<int32_t>(result) }, { "identity", result == sl::Result::eOk ? Identity(native.get()) : json(nullptr) } };
		}
		std::filesystem::path RequestPath()
		{
			const auto log = Util::PathHelpers::GetLogPath();
			if (log.empty())
				throw std::runtime_error("SKSE log path unavailable");
			return log.parent_path() / "CSX-Reflex-Presentation-NextLaunch.json";
		}
		bool ConsumeRequest()
		{
			if (consumed)
				return creation.is_object() && creation.value("requestedFlip", false);
			consumed = true;
			const auto path = RequestPath();
			if (!std::filesystem::exists(path))
				return false;
			if (std::filesystem::file_size(path) > 4096)
				throw std::runtime_error("presentation request exceeds 4096 bytes; delete the request file to clear it");
			std::ifstream input(path);
			const auto request = json::parse(input, nullptr, false);
			input.close();
			if (!std::filesystem::remove(path))
				throw std::runtime_error("could not consume the one-launch presentation request");
			if (!request.is_object() || request.value("buildId", std::string{}) != BuildProvenance::GetBuildId() ||
				request.value("mode", std::string{}) != "flip_discard")
				throw std::runtime_error("invalid or different-build presentation request consumed; using default");
			return true;
		}
	}

	HRESULT Create(decltype(&D3D11CreateDeviceAndSwapChain) create,
		IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver, HMODULE software, UINT flags,
		const D3D_FEATURE_LEVEL* levels, UINT levelCount, UINT sdk,
		const DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swap, ID3D11Device** device,
		D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context)
	{
		bool requested = false;
		json receipt{ { "requestedFlip", false }, { "attemptedFlip", false }, { "fallback", false },
			{ "trialResult", nullptr }, { "original", desc ? Descriptor(*desc) : json(nullptr) } };
		if (REL::Module::IsVR()) {
			std::scoped_lock lock(metadataMutex);
			try {
				requested = ConsumeRequest();
			} catch (const std::exception& error) {
				configurationError = error.what();
			}
			receipt["configurationError"] = configurationError;
		}
		DXGI_SWAP_CHAIN_DESC trialDesc = desc ? *desc : DXGI_SWAP_CHAIN_DESC{};
		const bool attempt = desc && swap && device && context && PrepareFlip(REL::Module::IsVR(), requested, trialDesc);
		receipt["requestedFlip"] = requested;
		receipt["attemptedFlip"] = attempt;
		receipt["admissionReason"] = !requested ? "default descriptor requested" : attempt ? "VR descriptor supports flip-discard; only swap effect and minimum buffer count may change" :
		                                                                                     "unsupported format, multisampling, GDI/buffer contract or missing outputs; descriptor preserved";
		HRESULT result = E_FAIL;
		if (attempt) {
			winrt::com_ptr<IDXGISwapChain> trialSwap;
			winrt::com_ptr<ID3D11Device> trialDevice;
			winrt::com_ptr<ID3D11DeviceContext> trialContext;
			D3D_FEATURE_LEVEL trialLevel{};
			result = create(adapter, driver, software, flags, levels, levelCount, sdk, &trialDesc,
				trialSwap.put(), trialDevice.put(), &trialLevel, trialContext.put());
			receipt["trialResult"] = result;
			receipt["trialOutputsComplete"] = trialSwap && trialDevice && trialContext;
			receipt["trialDescriptor"] = Descriptor(trialDesc);
			if (SUCCEEDED(result) && trialSwap && trialDevice && trialContext) {
				*swap = trialSwap.detach();
				*device = trialDevice.detach();
				*context = trialContext.detach();
				if (level)
					*level = trialLevel;
			} else {
				result = FAILED(result) ? result : E_UNEXPECTED;
				receipt["fallback"] = true;
			}
		}
		if (!attempt || FAILED(result))
			result = create(adapter, driver, software, flags, levels, levelCount, sdk, desc, swap, device, level, context);
		receipt["creationResult"] = result;
		receipt["deviceFlags"] = flags;
		receipt["driverType"] = driver;
		receipt["deviceBeforeUpgrade"] = SUCCEEDED(result) && device ? Identity(*device) : json(nullptr);
		DXGI_SWAP_CHAIN_DESC actual{};
		if (SUCCEEDED(result) && swap && *swap) {
			receipt["swapBeforeUpgrade"] = Identity(*swap);
			const auto hr = (*swap)->GetDesc(&actual);
			receipt["actualDescriptorResult"] = hr;
			receipt["actual"] = SUCCEEDED(hr) ? Descriptor(actual) : json(nullptr);
		}
		receipt["variantApplied"] = !requested ? SUCCEEDED(result) && receipt.contains("actual") && receipt["actual"].is_object() :
		                                         attempt && !receipt["fallback"].get<bool>() && actual.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
		{
			std::scoped_lock lock(metadataMutex);
			creation = receipt;
		}
		logger::info("[ReflexPresentation] {}", receipt.dump());
		return result;
	}

	json Environment()
	{
		json result;
		{
			std::scoped_lock lock(metadataMutex);
			result = { { "creation", creation }, { "nextLaunchFlip", nextFlip }, { "configurationError", configurationError } };
		}
		auto* swap = globals::d3d::swapChain;
		auto* device = globals::d3d::device;
		result["rendererDevice"] = Identity(device);
		result["nativeRendererDevice"] = NativeIdentity(device);
		result["swapChain"] = Identity(swap);
		result["streamlineBoundDevice"] = Identity(globals::features::upscaling.streamline.boundDeviceIdentity);
		result["streamlineBoundToRenderer"] = globals::features::upscaling.streamline.IsBoundToD3DDevice(device);
		if (globals::d3d::context) {
			winrt::com_ptr<ID3D11Device> contextDevice;
			globals::d3d::context->GetDevice(contextDevice.put());
			result["contextDevice"] = Identity(contextDevice.get());
			result["nativeContextDevice"] = NativeIdentity(contextDevice.get());
		}
		if (device) {
			result["deviceRemovedReason"] = device->GetDeviceRemovedReason();
			result["featureLevel"] = device->GetFeatureLevel();
			winrt::com_ptr<IDXGIDevice1> dxgi;
			UINT latency{};
			const auto qi = device->QueryInterface(__uuidof(IDXGIDevice1), dxgi.put_void());
			const auto hr = SUCCEEDED(qi) ? dxgi->GetMaximumFrameLatency(&latency) : qi;
			result["deviceMaximumFrameLatency"] = { { "result", hr }, { "value", SUCCEEDED(hr) ? json(latency) : json(nullptr) } };
		}
		if (swap) {
			winrt::com_ptr<ID3D11Device> swapDevice;
			const auto deviceHr = swap->GetDevice(__uuidof(ID3D11Device), swapDevice.put_void());
			result["swapDeviceResult"] = deviceHr;
			result["swapDevice"] = Identity(swapDevice.get());
			result["nativeSwapDevice"] = NativeIdentity(swapDevice.get());
			result["nativeSwapChain"] = NativeIdentity(swap);
			DXGI_SWAP_CHAIN_DESC desc{};
			const auto hr = swap->GetDesc(&desc);
			result["descriptorResult"] = hr;
			result["descriptor"] = SUCCEEDED(hr) ? Descriptor(desc) : json(nullptr);
			if (SUCCEEDED(hr)) {
				observedWindow = desc.OutputWindow;
				observedSwap = swap;
				RECT client{};
				const bool rectValid = GetClientRect(desc.OutputWindow, &client) != FALSE;
				result["window"] = { { "handle", Address(desc.OutputWindow) }, { "foreground", Address(GetForegroundWindow()) },
					{ "valid", IsWindow(desc.OutputWindow) != FALSE }, { "visible", IsWindowVisible(desc.OutputWindow) != FALSE },
					{ "minimized", IsIconic(desc.OutputWindow) != FALSE }, { "clientRectValid", rectValid },
					{ "clientRect", { client.left, client.top, client.right, client.bottom } } };
			}
			BOOL fullscreen{};
			const auto fullHr = swap->GetFullscreenState(&fullscreen, nullptr);
			result["fullscreen"] = { { "result", fullHr }, { "value", SUCCEEDED(fullHr) ? json(fullscreen != FALSE) : json(nullptr) } };
			UINT count{};
			const auto countHr = swap->GetLastPresentCount(&count);
			result["lastPresentCount"] = { { "result", countHr }, { "value", SUCCEEDED(countHr) ? json(count) : json(nullptr) } };
		}
		return result;
	}

	json Configure(const json& args)
	{
		std::scoped_lock lock(metadataMutex);
		if (args.value("action", std::string{}) == "reflex_presentation_next_launch") {
			if (!REL::Module::IsVR() || Suite::Busy())
				return { { "error", "Requires VR with no guided test running" } };
			if (!args.contains("mode") || !args["mode"].is_string() ||
				(args["mode"] != "default" && args["mode"] != "flip_discard"))
				return { { "error", "mode must be default or flip_discard" } };
			try {
				const auto path = RequestPath();
				if (args["mode"] == "flip_discard") {
					std::string error;
					if (!Util::FileHelpers::WriteTextFileAtomic(path,
							json{ { "mode", "flip_discard" }, { "buildId", BuildProvenance::GetBuildId() } }.dump(), error, false))
						throw std::runtime_error(error);
				} else {
					std::filesystem::remove(path);
				}
				nextFlip = args["mode"] == "flip_discard";
				configurationError.clear();
			} catch (const std::exception& error) {
				configurationError = error.what();
				return { { "error", configurationError } };
			}
		}
		return { { "creation", creation }, { "nextLaunchFlip", nextFlip }, { "configurationError", configurationError } };
	}

	void DrawControls()
	{
		if (!REL::Module::IsVR())
			return;
		const auto status = Configure({ { "action", "reflex_presentation_status" } });
		const auto& receipt = status["creation"];
		const bool flip = receipt.is_object() && receipt.value("requestedFlip", false);
		const bool verified = receipt.is_object() && receipt.value("variantApplied", false);
		ImGui::TextWrapped("Desktop presentation this launch: %s (%s).", flip ? "flip-discard test" : "default", verified ? "creation verified" : "not verified / fallback");
		ImGui::TextWrapped("Next launch: %s. This changes only VR desktop swap-chain creation. Run the full test on default, then on flip-discard after restarting Skyrim. A further restart returns to default automatically.", status["nextLaunchFlip"].get<bool>() ? "one-time flip-discard test" : "default");
		if (!Suite::Busy()) {
			if (ImGui::Button("Use flip-discard on next launch only"))
				(void)Configure({ { "action", "reflex_presentation_next_launch" }, { "mode", "flip_discard" } });
			if (ImGui::Button("Use default on next launch"))
				(void)Configure({ { "action", "reflex_presentation_next_launch" }, { "mode", "default" } });
		}
		if (!status["configurationError"].get<std::string>().empty())
			ImGui::TextWrapped("Presentation request error: %s", status["configurationError"].get<std::string>().c_str());
	}

	bool Start(uint64_t session, uint64_t start, uint64_t deadline) { return journal.Start(session, start, deadline); }
	void Stop() { journal.Stop(); }
	bool Drained() { return journal.Drained(); }
	json Read(uint64_t after, size_t limit)
	{
		const auto snapshot = journal.Read(Tick(), after, limit);
		json records = json::array();
		for (const auto& r : snapshot.records)
			records.push_back({ { "sequence", r.sequence }, { "entryTick", r.entry }, { "beginTick", r.begin }, { "endTick", r.end },
				{ "observerTicks", r.observerTicks }, { "swapChain", r.swap }, { "completedFrame", r.completedFrame }, { "threadId", r.thread },
				{ "syncInterval", r.sync }, { "flags", r.flags }, { "returned", r.returned }, { "result", r.returned ? json(r.result) : json(nullptr) },
				{ "window", r.window }, { "foreground", r.foreground }, { "windowValid", r.windowValid }, { "visible", r.visible }, { "minimized", r.minimized } });
		return { { "sessionId", snapshot.session }, { "startTick", snapshot.start }, { "deadlineTick", snapshot.deadline },
			{ "active", snapshot.active }, { "inFlight", snapshot.inFlight }, { "droppedRecords", snapshot.dropped },
			{ "retainedRecords", snapshot.retained }, { "capacity", 32768 }, { "records", records },
			{ "definition", "Original desktop IDXGISwapChain::Present call only; not HMD presentation. completedFrame is captured before State::Reset. Window state observed before the call. Occlusion is reported only by real Present HRESULTs; no synthetic probes. QPC shares the input-hook clock. Frame numbers and timestamps support correlation, not assumed one-to-one pairing." } };
	}
	PresentScope::PresentScope(IDXGISwapChain* swap, UINT sync, UINT flags, uint32_t completedFrame)
	{
		active_ = journal.Begin(Tick(), record_);
		if (!active_)
			return;
		record_.swap = Address(swap);
		record_.thread = GetCurrentThreadId();
		record_.sync = sync;
		record_.flags = flags;
		record_.completedFrame = completedFrame;
		const auto window = swap == observedSwap.load() ? observedWindow.load() : nullptr;
		record_.window = Address(window);
		record_.foreground = Address(GetForegroundWindow());
		record_.windowValid = window && IsWindow(window);
		record_.visible = window && IsWindowVisible(window);
		record_.minimized = window && IsIconic(window);
		record_.observerTicks = Tick() - record_.entry;
	}
	void PresentScope::BeginCall()
	{
		if (active_)
			record_.begin = Tick();
	}
	void PresentScope::EndCall(HRESULT result)
	{
		if (!active_)
			return;
		record_.end = Tick();
		record_.result = result;
		record_.returned = true;
	}
	PresentScope::~PresentScope()
	{
		if (active_)
			journal.Finish(record_);
	}
}
#endif
