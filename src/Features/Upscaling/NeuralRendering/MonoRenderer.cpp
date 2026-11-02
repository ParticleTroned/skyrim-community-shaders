#include "MonoRenderer.h"

#include "CharacterCategoryFormat.h"
#include "MemoryResolutionPolicy.h"
#include "NativeMemoryPolicy.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "FaceDetailPolicy.h"
#endif
#include "ExposureCapture.h"
#include "Globals.h"
#include "GpuPass.h"
#include "MonoCoverage.h"
#include "MonoInterop.h"
#include "MonoPrewarmPolicy.h"
#include "MonoSettingsSerialization.h"
#include "PackedCropPolicy.h"
#include "PeripheralCompressionPolicy.h"
#include "Profiler.h"
#include "Runtime.h"
#include "StallRecoveryPolicy.h"
#include "Utils/ComIdentity.h"
#include "Utils/D3D.h"
#include "Utils/DeviceContextState.h"
#include "Utils/GpuMemoryBudget.h"
#include "Utils/LazyShader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <wrl/client.h>

namespace NeuralRendering
{
	using Microsoft::WRL::ComPtr;
	namespace
	{
		struct Texture
		{
			ComPtr<ID3D11Texture2D> texture;
			ComPtr<ID3D11ShaderResourceView> srv;
			ComPtr<ID3D11UnorderedAccessView> uav;
			void Abandon() noexcept
			{
				(void)texture.Detach();
				(void)srv.Detach();
				(void)uav.Detach();
			}
		};
		struct alignas(16) MonoConstants
		{
			std::array<std::uint32_t, 2> source{}, model{}, guides{}, categorySize{}, modelGuides{}, padding{};
			std::array<float, 4> region{}, strengths{}, selection{}, depth{}, sampling{}, reconstruction{};
			std::array<std::uint32_t, 4> flags{}, inference{};
		};
		static_assert(sizeof(MonoConstants) == 176);
		struct alignas(16) PackedConstants
		{
			std::array<std::uint32_t, 4> configuration{};
			std::array<std::array<std::uint32_t, 4>, kMaximumPackedCrops> source{}, model{}, packed{};
		};
		static_assert(sizeof(PackedConstants) == 208);
		struct alignas(16) ColorConstants
		{
			std::array<std::uint32_t, 2> offset{}, size{};
			std::uint32_t mode = 0, domain = 0, transform = 0, flags = 0x200;
			float exposure = 1.0f, detail = 1.0f, appearance = 0.0f, maximumStops = 1.0f;
			float lighting = 1.0f;
			std::uint32_t regionCount = 0;
			std::array<float, 2> padding{};
			std::array<std::uint32_t, 16> regions{};
		};
		static_assert(sizeof(ColorConstants) == 128);

		bool TextureBelongsTo(ID3D11Texture2D* texture, ID3D11Device* device)
		{
			if (!texture || !device) return false;
			ComPtr<ID3D11Device> owner;
			texture->GetDevice(&owner);
			return Util::SameIdentity(owner.Get(), device);
		}

		DXGI_FORMAT RawColorFormat(DXGI_FORMAT format)
		{
			switch (format) {
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
			case DXGI_FORMAT_R11G11B10_FLOAT:
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
			case DXGI_FORMAT_R10G10B10A2_UNORM: return format;
			default: return DXGI_FORMAT_UNKNOWN;
			}
		}
		HRESULT MakeTexture(ID3D11Device* device, std::uint32_t width, std::uint32_t height,
			DXGI_FORMAT format, bool unordered, const char* name, Texture& output)
		{
			D3D11_TEXTURE2D_DESC description{};
			description.Width = width; description.Height = height;
			description.MipLevels = description.ArraySize = 1;
			description.SampleDesc.Count = 1;
			description.Format = format;
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE | (unordered ? D3D11_BIND_UNORDERED_ACCESS : 0);
			Texture candidate;
			auto result = device->CreateTexture2D(&description, nullptr, &candidate.texture);
			if (FAILED(result)) return result;
			Util::SetResourceName(candidate.texture.Get(), "%s", name);
			if (FAILED(result = device->CreateShaderResourceView(candidate.texture.Get(), nullptr, &candidate.srv))) return result;
			Util::SetResourceName(candidate.srv.Get(), "%s SRV", name);
			if (unordered) {
				if (FAILED(result = device->CreateUnorderedAccessView(candidate.texture.Get(), nullptr, &candidate.uav))) return result;
				Util::SetResourceName(candidate.uav.Get(), "%s UAV", name);
			}
			output = std::move(candidate);
			return S_OK;
		}
		template <class T>
		bool Upload(ID3D11Device* device, ID3D11DeviceContext* context, ComPtr<ID3D11Buffer>& buffer, const T& data, const char* name)
		{
			static_assert(sizeof(T) % 16 == 0);
			if (!buffer) {
				D3D11_BUFFER_DESC description{};
				description.ByteWidth = sizeof(T); description.Usage = D3D11_USAGE_DYNAMIC;
				description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				if (FAILED(device->CreateBuffer(&description, nullptr, &buffer))) return false;
				Util::SetResourceName(buffer.Get(), "%s", name);
			}
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
			std::memcpy(mapped.pData, &data, sizeof(T));
			context->Unmap(buffer.Get(), 0);
			return true;
		}
		MemoryBudgetSample MemorySample(ID3D11Device* device, bool force = false)
		{
			const auto sample = Util::GpuMemoryBudget::Get().Sample(device, 250, force);
			return { sample.local.Budget, sample.local.CurrentUsage, SUCCEEDED(sample.result),
				static_cast<std::int32_t>(sample.result), false, sample.sampledAtMs };
		}
		ComputeSubrect ExpandNativeRegion(ComputeSubrect region, UpscalingDLSS::Extent model)
		{
			const auto expand = [](std::uint32_t& origin, std::uint32_t& extent, std::uint32_t capacity) {
				const auto wanted = std::min(capacity, std::max(extent, kMinimumExperimentalContextExtent));
				origin = std::min(origin > (wanted - extent) / 2 ? origin - (wanted - extent) / 2 : 0u, capacity - wanted);
				extent = wanted;
			};
			expand(region.baseX, region.width, model.width);
			expand(region.baseY, region.height, model.height);
			return region;
		}
		NativeMemoryKey NativeKey(const NativeEvaluationLayout& layout, std::uint32_t network)
		{
			return { Runtime::Instance().AllocationVersion(), network, layout.creation, layout.color.backing,
				DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16_FLOAT,
				layout.featureUpscaling, false, layout.nativeControlMask };
		}
		std::uint64_t ModelStorageBytes(UpscalingDLSS::Extent model, UpscalingDLSS::Extent guides, bool mask)
		{
			return std::uint64_t(model.width) * model.height * (mask ? 20u : 16u) + std::uint64_t(guides.width) * guides.height * 8u;
		}
		bool SameHistoryLayout(NativeEvaluationLayout left, NativeEvaluationLayout right)
		{
			left.jitterPixels = right.jitterPixels = {};
			return left == right;
		}
		void Transition(ID3D12GraphicsCommandList* commands, MonoSharedTexture& texture, D3D12_RESOURCE_STATES state, bool enter)
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = texture.shared.Get();
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = enter ? D3D12_RESOURCE_STATE_COMMON : state;
			barrier.Transition.StateAfter = enter ? state : D3D12_RESOURCE_STATE_COMMON;
			commands->ResourceBarrier(1, &barrier);
		}
	}

	struct MonoRenderer::State
	{
		mutable std::mutex mutex;
		MonoSnapshot snapshot;
#ifdef DEVBENCH_BRIDGE_ENABLED
		struct ExecutionRecord
		{
			std::uint32_t frame = UINT32_MAX;
			bool prewarm = false, submitted = false, committed = false, historyReset = false;
			NativeEvaluationLayout layout{};
			MonoSettings settings{};
			MonoExperiments experiments{};
			RuntimeExecutionEvidence native{};
		};
		std::array<ExecutionRecord, 16> executions{};
		std::size_t executionCursor = 0;
		MonoExperiments experiments{};
		FaceDetailRegions faces{};
		std::array<std::uint32_t, kFaceDetailMaximumActors> faceActors{};
		std::uint32_t faceFrame = UINT32_MAX;
		std::atomic_bool wantsFaceDetail{ false };
		bool experimentsChanged = false;
		std::string footprintDefine = "2";
#endif
		struct ActorEntry
		{
			ActorAdmission result;
			CharacterActorAdmissionState size;
			CharacterFocusState focus;
		};
		std::unordered_map<std::uintptr_t, ActorEntry> actors;
		MonoSettings actorSettings{}, appliedSettings{};
		std::uint32_t actorFrame = UINT32_MAX;
		CharacterRect actorUnion{};
		std::vector<CharacterRect> actorRegions;
		StablePackedCropSources packedSources;
		PackedCropCapacityState packedCapacity;
		PackedCropLayout packedLayout;
		bool actorUncertain = false, settingsValid = false;
		ComPtr<ID3D11ShaderResourceView> categories, authoredDepth;
		std::uint32_t categoryFrame = UINT32_MAX, categoryWidth = 0, categoryHeight = 0;
		Texture guideDepth, guideMotion, source, result;
		std::uint32_t guideFrame = UINT32_MAX, guideWidth = 0, guideHeight = 0;
		D3D11_TEXTURE2D_DESC sourceDescription{};
		MonoSharedTexture modelColor, modelDepth, modelMotion, modelOutput, modelControlMask;
		UpscalingDLSS::Extent modelExtent{}, modelGuideExtent{};
		NativeEvaluationLayout layout{};
		PeripheralMapping mapping{};
		MonoInterop interop;
		MonoCoverage coverage;
		std::uint32_t coveredFrame = UINT32_MAX;
		winrt::com_ptr<ID3D11DeviceContext1> context1;
		winrt::com_ptr<ID3DDeviceContextState> isolated;
		ComPtr<ID3D11Device> device;
		Util::LazyShader<ID3D11ComputeShader> guideShader, prepareShader, reconstructShader, coverageShader;
		Util::LazyShader<ID3D11VertexShader> presentVertex;
		Util::LazyShader<ID3D11PixelShader> presentPixel;
		ComPtr<ID3D11Buffer> monoBuffer, colorBuffer, guideBuffer, packedBuffer, peripheralBuffer;
		Color::ExposureBinding exposure;
		NativeMemoryPolicy nativeMemory;
		NativeStartupObservation nativeStartup;
		Util::GpuMemoryBudget::Reservation nativeReservation;
		std::uint64_t nativeProjectedBytes = 0, nativeObservations = 0;
		std::vector<std::pair<const char*, const char*>> shaderDefines;
		bool compressedShaders = false, shaderConfigurationValid = false;
		MemoryRecoveryPolicy memory;
		MemoryResolutionPolicy resolution;
		StallRecoveryPolicy stall;
		MonoPrewarmPolicy prewarm;
		bool recoveringStall = false;
		std::array<std::uint64_t, 9> memoryCapacity{};
		std::atomic<std::uint64_t> dlssPressureAtMs{ 0 };
		std::uint32_t evaluatedFrame = UINT32_MAX, presentedFrame = UINT32_MAX;
		std::uint64_t lastRequestedMs = 0;
		bool requested = false;
		bool resultReady = false, historyValid = false, retiring = false, shutdown = false, quarantined = false, runtimeFailed = false;

		void Status(const char* status, const std::string& detail = {})
		{
			snapshot.status = status;
			snapshot.detail = detail;
		}
		void PublishBudget()
		{
			if (!requested && !retiring && !nativeStartup.active) {
				Util::GpuMemoryBudget::Get().SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering, false, 0);
				return;
			}
			const bool pending = retiring || nativeStartup.active || memory.phase == MemoryRecoveryPhase::Retiring || memory.phase == MemoryRecoveryPhase::Rebuilding;
			Util::GpuMemoryBudget::Get().SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering, pending,
				memory.phase == MemoryRecoveryPhase::Waiting   ? memory.admissionDemandBytes :
				memory.phase == MemoryRecoveryPhase::Probation ? MemoryRecoveryPolicy::kMinimumHeadroom :
																 0);
		}
		bool BindState(ID3D11Device* owner, ID3D11DeviceContext* context)
		{
			if (!owner || !context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
			ComPtr<ID3D11Device> actual;
			context->GetDevice(&actual);
			if (!Util::SameIdentity(actual.Get(), owner) || (device && !Util::SameIdentity(device.Get(), owner))) return false;
			if (!isolated && !Util::CreateIsolatedDeviceContextState(owner, context, context1, isolated, "NeuralRendering::MonoState")) return false;
			device = owner;
			return true;
		}
		void Quarantine(const char* reason)
		{
			if (quarantined)
				return;
			quarantined = true;
			snapshot.quarantined = true;
			Runtime::Instance().AbandonUnsafe();
			modelColor.Abandon();
			modelDepth.Abandon();
			modelMotion.Abandon();
			modelOutput.Abandon();
			modelControlMask.Abandon();
			nativeReservation.Reset();
			nativeStartup = {};
			source.Abandon();
			result.Abandon();
			guideDepth.Abandon();
			guideMotion.Abandon();
			exposure.Abandon();
			Color::ExposureCapture::Instance().Abandon();
			interop.Abandon();
			coverage.Abandon();
			(void)isolated.detach();
			(void)context1.detach();
			(void)device.Detach();
			Status("quarantined", reason);
			logger::error("[Neural Rendering] {}. Native ownership retained until process exit.", reason);
			Util::GpuMemoryBudget::Get().SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering, false, 0);
		}
		bool PrepareStorage(const MonoFrame& frame, const MonoSettings& settings,
			const D3D11_TEXTURE2D_DESC& description, DXGI_FORMAT format, UpscalingDLSS::Extent model, UpscalingDLSS::Extent modelGuides, bool rebuild, std::uint64_t now, bool nativeControlMask = false)
		{
			if (!interop.Initialize(frame.device, frame.context)) {
				if (interop.OwnsQueuedWork())
					Quarantine("The mono interop device lost a submitted queue");
				else {
					Status("interop_unavailable", "The device does not support the required shared D3D11/D3D12 fence and resource path.");
					interop = {};
					runtimeFailed = true;
					appliedSettings = settings;
					settingsValid = true;
				}
				return false;
			}
			auto& runtime = Runtime::Instance();
			if (!runtime.Initialize(interop.Device())) {
				runtimeFailed = true;
				appliedSettings = settings;
				settingsValid = true;
				Status("runtime_unavailable", runtime.Detail());
				return false;
			}
			if (rebuild) {
				const auto allocate = [&](HRESULT result) {
					if (SUCCEEDED(result))
						return true;
					memory.Suspend(now, result == E_OUTOFMEMORY);
					Status("allocation_failed");
					return false;
				};
				if (!allocate(interop.CreateTexture(model.width, model.height, DXGI_FORMAT_R16G16B16A16_FLOAT, modelColor, "NeuralRendering::ModelColor")) ||
					!allocate(interop.CreateTexture(modelGuides.width, modelGuides.height, DXGI_FORMAT_R32_FLOAT, modelDepth, "NeuralRendering::ModelDepth")) ||
					!allocate(interop.CreateTexture(modelGuides.width, modelGuides.height, DXGI_FORMAT_R16G16_FLOAT, modelMotion, "NeuralRendering::ModelMotion")) ||
					!allocate(interop.CreateTexture(model.width, model.height, DXGI_FORMAT_R16G16B16A16_FLOAT, modelOutput, "NeuralRendering::ModelOutput")) ||
					(nativeControlMask && !allocate(interop.CreateTexture(model.width, model.height, DXGI_FORMAT_R8G8B8A8_UNORM, modelControlMask, "NeuralRendering::ModelControlMask"))) ||
					!allocate(MakeTexture(frame.device, description.Width, description.Height, format, false, "NeuralRendering::SceneBaseline", source)) ||
					!allocate(MakeTexture(frame.device, description.Width, description.Height,
						format == DXGI_FORMAT_R32G32B32A32_FLOAT ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT,
						true, "NeuralRendering::SceneResult", result))) {
					appliedSettings = settings;
					settingsValid = true;
					(void)Retire();
					return false;
				}
				modelExtent = model;
				modelGuideExtent = modelGuides;
				sourceDescription = description;
				sourceDescription.Format = format;
				historyValid = false;
			}
			return true;
		}

		MonoPrewarmKey CreationKey(const MonoFrame& frame, std::uint32_t mode, DXGI_FORMAT format, const NativeEvaluationLayout& native, std::uint32_t network) const
		{
			const auto deviceIdentity = Util::GetComIdentity(frame.device);
			const auto contextIdentity = Util::GetComIdentity(frame.context);
			D3D11_TEXTURE2D_DESC description{};
			frame.color->GetDesc(&description);
			auto creationLayout = native;
			creationLayout.jitterPixels = {};
			return { creationLayout, reinterpret_cast<std::uintptr_t>(deviceIdentity.Get()), reinterpret_cast<std::uintptr_t>(contextIdentity.Get()),
				description.Width, description.Height, static_cast<std::uint32_t>(format), frame.width, frame.height, mode, network };
		}

		void PollNativeStartup(std::uint64_t now)
		{
			if (!nativeStartup.active || !device)
				return;
			const auto current = interop.Snapshot();
			const auto sample = MemorySample(device.Get(), nativeStartup.Completed(current));
			now = GetTickCount64();
			nativeStartup.Sample(sample, now);
			if (!nativeStartup.evaluated || !nativeStartup.Completed(current))
				return;
			if (!sample.IsFresh(now)) {
				if (now >= nativeStartup.startedMs && now - nativeStartup.startedMs > NativeMemoryPolicy::kMaximumObservationMs) {
					memory.Suspend(now, false);
					retiring = true;
					Status("native_startup_budget_unavailable");
				}
				return;
			}
			if (nativeStartup.valid) {
				nativeMemory.Observe(nativeStartup.key, nativeStartup.peak - nativeStartup.baseline);
				++nativeObservations;
			}
			nativeReservation.Reset();
			nativeStartup = {};
		}
		void SampleNativeStartup()
		{
			const auto sample = MemorySample(device.Get(), true);
			nativeStartup.Sample(sample, GetTickCount64());
		}
		void RecordNativeSubmission()
		{
			if (!nativeStartup.active)
				return;
			const auto submitted = interop.Snapshot();
			nativeStartup.fence = submitted.fence;
			nativeStartup.issued = submitted.issued;
		}
		bool ReserveNative(const NativeMemoryKey& key, std::uint64_t bytes, std::uint64_t now, bool evaluation)
		{
			if (nativeStartup.active) {
				if (nativeStartup.key != key || nativeStartup.evaluated)
					return false;
				nativeStartup.evaluated = evaluation;
				return true;
			}
			if (!bytes)
				return true;
			nativeReservation = Util::GpuMemoryBudget::Get().Reserve(Util::GpuMemoryBudget::Owner::NeuralRendering, device.Get(), bytes);
			if (!nativeReservation)
				return false;
			const auto baseline = MemorySample(device.Get(), true);
			now = GetTickCount64();
			nativeStartup.Begin(key, 0, baseline, now, evaluation);
			nativeStartup.reservedBytes = bytes;
			return true;
		}
		void ConfigureShaders(bool compressed)
		{
			if (shaderConfigurationValid && compressedShaders == compressed)
				return;
			compressedShaders = compressed;
			shaderConfigurationValid = true;
			prepareShader.Reset();
			reconstructShader.Reset();
			coverageShader.Reset();
			shaderDefines.clear();
			if (compressed)
				shaderDefines.emplace_back("NR_PERIPHERAL_COMPRESSION", "1");
#ifdef DEVBENCH_BRIDGE_ENABLED
			shaderDefines.emplace_back("DEVBENCH_BRIDGE_ENABLED", "1");
			shaderDefines.emplace_back("NR_FACE_DETAIL_PROTECTION", "1");
			footprintDefine = std::to_string(experiments.reconstruction.footprintSubdivisions);
			shaderDefines.emplace_back("NR_FOOTPRINT_SUBDIVISIONS", footprintDefine.c_str());
			if (experiments.nvidiaReferenceColor)
				shaderDefines.emplace_back("NR_NVIDIA_REFERENCE", "1");
			if (experiments.nativeControlMask)
				shaderDefines.emplace_back("NR_NATIVE_CONTROL_MASK", "1");
#endif
		}

		bool ServiceStall(std::uint64_t now)
		{
			interop.PollGpuTiming();
			PollNativeStartup(now);
			if (quarantined)
				return true;
			if (interop.Failed() && interop.OwnsQueuedWork()) {
				Quarantine("The native queue lost its completion proof");
				return true;
			}
			if (!stall.pending && !recoveringStall && interop.Stalled(now)) {
				if (!stall.Arm(now)) {
					Quarantine("The neural queue exhausted its bounded recovery attempts");
					return true;
				}
				historyValid = resultReady = false;
				Status("waiting_for_stalled_queue");
			}
			if (stall.ShouldPoll(now)) {
				const auto decision = stall.Observe(now, interop.Snapshot());
				if (decision == StallRecoveryDecision::Unsafe || decision == StallRecoveryDecision::Expired) {
					stall.Cancel();
					Quarantine("The stalled queue could not establish safe bounded recovery");
					return true;
				}
				if (decision == StallRecoveryDecision::Recover) {
					stall.BeginAttempt();
					recoveringStall = true;
					retiring = true;
					shutdown = true;
				}
			}
			if (recoveringStall && Retire()) {
				recoveringStall = false;
				if (!quarantined) {
					runtimeFailed = false;
					++snapshot.stallRecoveries;
					Status("queue_recovered");
				}
			}
			snapshot.stallAttempts = stall.attempts;
			snapshot.recoveryPending = stall.pending || recoveringStall;
			return snapshot.recoveryPending || quarantined;
		}

		bool Retire()
		{
			resultReady = historyValid = false;
			retiring = true;
			PublishBudget();
			if (quarantined)
				return true;
			if (interop.Failed()) {
				Quarantine("D3D11/D3D12 completion is indeterminate");
				return true;
			}
			if (!interop.PollRetired()) {
				Status("retiring");
				return false;
			}
			if (interop.Initialized() && !Runtime::Instance().ReleasePassResources()) {
				Quarantine("The neural runtime could not release a retired feature");
				return true;
			}
			if (context1 && isolated) {
				Util::ScopedDeviceContextState state(context1.get(), isolated.get());
				context1->ClearState();
			}
			if (prewarm.captured)
				prewarm.Cancel();
			modelColor = {};
			modelDepth = {};
			modelMotion = {};
			modelOutput = {};
			modelControlMask = {};
			nativeReservation.Reset();
			nativeStartup = {};
			source = {};
			result = {};
			sourceDescription = {};
			modelExtent = {};
			modelGuideExtent = {};
			layout = {};
			packedLayout = {};
			mapping = {};
			interop.ReleaseRetired();
			const auto now = GetTickCount64();
			if (memory.phase == MemoryRecoveryPhase::Retiring) {
				memory.Retired(now);
				(void)resolution.OnRetired(appliedSettings.modelResolutionPercent, now);
			} else {
				memory.ResourcesRetired(now);
				resolution.MarkResourcesRetired(now);
			}
			memory.SetStartupKind(MemoryStartupKind::Warm);
			retiring = false;
			if (shutdown) {
				if (interop.Initialized() && !Runtime::Instance().Shutdown()) {
					Quarantine("The neural runtime could not shut down after retirement");
					return true;
				}
				interop = {};
				coverage = {};
				coveredFrame = UINT32_MAX;
				guideDepth = {};
				guideMotion = {};
				guideFrame = UINT32_MAX;
				exposure = {};
				Color::ExposureCapture::Instance().Reset();
				categories.Reset();
				authoredDepth.Reset();
				categoryFrame = UINT32_MAX;
				isolated = nullptr;
				context1 = nullptr;
				device.Reset();
				memory.SetStartupKind(MemoryStartupKind::Cold);
				shutdown = false;
			}
			Status("inactive");
			PublishBudget();
			return true;
		}
	};

	MonoRenderer& MonoRenderer::Instance() { static MonoRenderer value; return value; }
	MonoRenderer::MonoRenderer() : state_(new State)
	{
		(void)Runtime::Instance();
		(void)Util::GpuMemoryBudget::Get();
	}
	MonoRenderer::~MonoRenderer()
	{
		if (!Retire(true) && !state_->quarantined)
			state_->Quarantine("Renderer destruction preceded the pending GPU tail");
		delete state_;
	}

	ActorAdmission MonoRenderer::ObserveActor(const ActorObservation& observation, const MonoSettings& requested)
	{
		std::scoped_lock lock(state_->mutex);
		auto settings = requested; Sanitize(settings);
		if (state_->actorFrame != observation.frame) {
			state_->actorFrame = observation.frame; state_->actorUnion = {}; state_->actorUncertain = false; state_->actorRegions.clear();
			if (state_->actorSettings != settings) { state_->actors.clear(); state_->actorSettings = settings; }
			std::erase_if(state_->actors, [&](const auto& pair) { return observation.frame - pair.second.result.frame > 120u; });
		}
		const auto group = static_cast<std::uint32_t>(observation.group);
		if (group >= kCharacterActorGroupCount || !(settings.actorGroupMask & (1u << group)))
			return { false, 255u, observation.frame, observation.rectangle };
		if (const auto found = state_->actors.find(observation.identity); found != state_->actors.end() && found->second.result.frame == observation.frame)
			return found->second.result;
		if (state_->actors.size() >= CharacterPolicy::kMaximumObservationsPerFrame) {
			state_->actorUncertain = true;
			return { true, 0u, observation.frame, observation.rectangle };
		}
		auto& entry = state_->actors[observation.identity];
		const bool uncertain = observation.projectionUncertain || !observation.identity || !observation.rectangle.IsValid();
		bool selected = ResolveCharacterActorAdmission(observation.frame, observation.facePixelSize, observation.distanceMeters,
			uncertain, settings.minimumFacePixelSize, settings.holdFrames, settings.adaptiveActorSelection, entry.size);
		selected &= uncertain || CharacterRegionPolicy::IsWithinMaximumDistance(observation.distanceMeters, settings.maximumDistanceMeters);
		const auto distance = CharacterFocusDistance(observation.rectangle, observation.viewportWidth, observation.viewportHeight,
			settings.focusScale, { 1.0f, settings.horizontalScale, settings.regionOffset });
		auto fade = ResolveCharacterFocusFade(observation.frame, distance, settings.actorFocus, uncertain, settings.holdFrames, entry.focus);
		if (!uncertain) {
			const auto distanceWeight = CharacterRegionPolicy::ResolveDistanceWeight(observation.distanceMeters, settings.maximumDistanceMeters);
			fade = 255u - static_cast<std::uint32_t>(std::round((255u - fade) * distanceWeight));
		}
		if (!selected) fade = 255u;
		selected &= fade < 255u;
		entry.result = { selected, fade, observation.frame, observation.rectangle };
		if (selected) {
			state_->actorUnion = CharacterRegionPolicy::Union(state_->actorUnion, observation.rectangle);
			state_->actorUncertain |= uncertain;
			if (!uncertain) state_->actorRegions.push_back(observation.rectangle);
		}
		return entry.result;
	}

	void MonoRenderer::SetSceneCategories(ID3D11ShaderResourceView* categories, ID3D11ShaderResourceView* depth,
		std::uint32_t frame, std::uint32_t width, std::uint32_t height)
	{
		std::scoped_lock lock(state_->mutex);
		D3D11_TEXTURE2D_DESC description{}, depthDescription{};
		D3D11_SHADER_RESOURCE_VIEW_DESC view{}, depthView{};
		if (depth) depth->GetDesc(&depthView);
		if (categories) categories->GetDesc(&view);
		ComPtr<ID3D11Device> categoryDevice, depthDevice;
		if (categories) categories->GetDevice(&categoryDevice);
		if (depth) depth->GetDevice(&depthDevice);
		const bool valid = categories && depth && width && height && Util::GetTexture2DDesc(categories, description) &&
			Util::SameIdentity(categoryDevice.Get(), depthDevice.Get()) &&
			(!state_->device || Util::SameIdentity(categoryDevice.Get(), state_->device.Get())) &&
			view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && view.Texture2D.MostDetailedMip == 0 && view.Format == kCharacterCategoryFormat &&
			description.Width >= width && description.Height >= height && description.ArraySize == 1 && description.SampleDesc.Count == 1 &&
			Util::GetTexture2DDesc(depth, depthDescription) && depthView.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && depthView.Texture2D.MostDetailedMip == 0 &&
			depthDescription.Width >= width && depthDescription.Height >= height && depthDescription.ArraySize == 1 && depthDescription.SampleDesc.Count == 1;
		state_->categories = valid ? categories : nullptr;
		state_->authoredDepth = valid ? depth : nullptr;
		state_->categoryFrame = valid ? frame : UINT32_MAX;
		state_->categoryWidth = valid ? width : 0; state_->categoryHeight = valid ? height : 0;
	}

	bool MonoRenderer::CaptureGuides(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* depth,
		ID3D11ShaderResourceView* motion, std::uint32_t frame, std::uint32_t width, std::uint32_t height)
	{
		std::scoped_lock lock(state_->mutex);
		state_->guideFrame = UINT32_MAX;
		if (state_->quarantined || !depth || !motion || !width || !height || width > 16384 || height > 16384 || !state_->BindState(device, context)) return false;
		D3D11_TEXTURE2D_DESC depthDescription{}, motionDescription{};
		D3D11_SHADER_RESOURCE_VIEW_DESC depthView{}, motionView{};
		depth->GetDesc(&depthView); motion->GetDesc(&motionView);
		ComPtr<ID3D11Device> depthDevice, motionDevice;
		depth->GetDevice(&depthDevice); motion->GetDevice(&motionDevice);
		if (!Util::SameIdentity(depthDevice.Get(), device) || !Util::SameIdentity(motionDevice.Get(), device) ||
			depthView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || motionView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
			depthView.Texture2D.MostDetailedMip != 0 || motionView.Texture2D.MostDetailedMip != 0 ||
			!Util::GetTexture2DDesc(depth, depthDescription) || !Util::GetTexture2DDesc(motion, motionDescription) ||
			width > depthDescription.Width || height > depthDescription.Height || width > motionDescription.Width || height > motionDescription.Height ||
			depthDescription.SampleDesc.Count != 1 || motionDescription.SampleDesc.Count != 1 ||
			depthDescription.ArraySize != 1 || motionDescription.ArraySize != 1) return false;
		auto* shader = state_->guideShader.Get(L"Data/Shaders/Upscaling/NeuralRendering/MonoGuidesCS.hlsl", {}, "cs_5_0", "main", "NeuralRendering::CaptureGuides");
		if (!shader) return false;
		if (!state_->guideDepth.texture || state_->guideWidth != width || state_->guideHeight != height) {
			const std::uint64_t bytes = std::uint64_t(width) * height * 8u;
			const auto budget = MemorySample(device);
			if (!state_->memory.CanAllocateOutput(budget, bytes, GetTickCount64(), Util::GpuMemoryBudget::Get().PendingBytes(Util::GpuMemoryBudget::Owner::NeuralRendering))) return false;
			auto reservation = Util::GpuMemoryBudget::Get().Reserve(Util::GpuMemoryBudget::Owner::NeuralRendering, device, bytes);
			Texture newDepth, newMotion;
			if (!reservation || FAILED(MakeTexture(device, width, height, DXGI_FORMAT_R32_FLOAT, true, "NeuralRendering::SceneDepth", newDepth)) ||
				FAILED(MakeTexture(device, width, height, DXGI_FORMAT_R16G16_FLOAT, true, "NeuralRendering::SceneMotion", newMotion))) return false;
			state_->guideDepth = std::move(newDepth); state_->guideMotion = std::move(newMotion);
			state_->guideWidth = width; state_->guideHeight = height;
		}
		Util::ScopedDeviceContextState pipeline(state_->context1.get(), state_->isolated.get());
		context->ClearState();
		const std::array<std::uint32_t, 4> dimensions{ width, height, 0, 0 };
		if (!Upload(device, context, state_->guideBuffer, dimensions, "NeuralRendering::GuideDimensions")) return false;
		ID3D11ShaderResourceView* inputs[]{ depth, motion };
		ID3D11UnorderedAccessView* outputs[]{ state_->guideDepth.uav.Get(), state_->guideMotion.uav.Get() };
		auto* buffer = state_->guideBuffer.Get();
		Util::BindFrameBufferConstantBuffersForCS(context);
		context->CSSetShader(shader, nullptr, 0); context->CSSetConstantBuffers(0, 1, &buffer);
		context->CSSetShaderResources(0, 2, inputs); context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
		{ CS_GPU_PASS("NeuralRendering::CaptureGuides"); context->Dispatch((width + 7) / 8, (height + 7) / 8, 1); }
		context->ClearState();
		state_->guideFrame = frame;
		return true;
	}

	void MonoRenderer::Prewarm(const MonoFrame& frame, std::uint32_t guideWidth, std::uint32_t guideHeight, const MonoSettings& requested)
	{
		std::scoped_lock lock(state_->mutex);
		auto& state = *state_;
		auto settings = requested;
		Sanitize(settings);
		auto now = GetTickCount64();
		if (const auto* blocker = MonoPrewarmConfigurationBlocker(settings)) {
			if (state.prewarm.captured) {
				state.prewarm.Cancel();
				state.retiring = true;
			}
			state.snapshot.prewarmStatus = blocker;
			return;
		}
		if (state.settingsValid && state.appliedSettings != settings) {
			state.runtimeFailed = false;
			state.prewarm.suppressed = false;
		}
		state.resolution.SetEnabled(settings.pressureResolution);
		if (state.ServiceStall(now) || state.quarantined || state.runtimeFailed || state.retiring || !frame.color ||
			!frame.width || !frame.height || !guideWidth || !guideHeight || !TextureBelongsTo(frame.color, frame.device) || !state.BindState(frame.device, frame.context))
			return;
		D3D11_TEXTURE2D_DESC description{};
		frame.color->GetDesc(&description);
		const auto format = RawColorFormat(description.Format);
		if (format == DXGI_FORMAT_UNKNOWN || description.MipLevels != 1 || description.ArraySize != 1 || description.SampleDesc.Count != 1 ||
			frame.width > description.Width || frame.height > description.Height || description.Width > 16384 || description.Height > 16384) {
			state.snapshot.prewarmStatus = "waiting_for_authoritative_scene_format";
			return;
		}
		const auto compression = BuildMonoCompressionPlan({ frame.width, frame.height }, { guideWidth, guideHeight },
			state.resolution.EffectivePercent(settings.modelResolutionPercent), settings.peripheralCompressionPercent, false, settings.regionPercent != 100);
		const auto model = compression.model;
		const auto modelGuides = BuildMonoGuideExtent({ frame.width, frame.height }, { guideWidth, guideHeight }, model);
		if (model.width < kMinimumExperimentalContextExtent || model.height < kMinimumExperimentalContextExtent) {
			state.snapshot.prewarmStatus = "inference_extent_too_small";
			return;
		}
		const auto region = ExpandNativeRegion(MapComputeSubrect(BuildMonoRegion(frame.width, frame.height, settings),
												   frame.width, frame.height, model.width, model.height),
			model);
		bool nativeControlMask = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		nativeControlMask = state.experiments.nativeControlMask;
#endif
		auto layout = BuildNativeEvaluationLayout(model, modelGuides, model, nativeControlMask ? model : UpscalingDLSS::Extent{}, region,
			{ true, float(modelGuides.width), float(modelGuides.height) }, modelGuides != model);
		layout.nativeControlMask = nativeControlMask;
		auto& runtime = Runtime::Instance();
		state.appliedSettings = settings;
		state.settingsValid = true;
		const auto key = state.CreationKey(frame, settings.mode, format, layout, runtime.GetNetworkPreset(settings.network));
		if (state.prewarm.captured && state.prewarm.key != key) {
			state.prewarm.Cancel();
			state.prewarm.suppressed = false;
			(void)state.Retire();
			return;
		}
		if (state.prewarm.unused || (state.modelColor.texture && runtime.HasCompatibleFeature(0, layout, false, key.network)))
			return;
		if (!state.prewarm.Begin(frame.frame, now))
			return;
		if (state.modelColor.texture) {
			(void)state.Retire();
			return;
		}
		const auto nativeKey = NativeKey(layout, runtime.GetNetworkPreset(settings.network));
		const auto estimate = state.nativeMemory.Project(nativeKey);
		if (!estimate) {
			state.snapshot.prewarmStatus = "invalid_native_memory_geometry";
			return;
		}
		state.nativeProjectedBytes = estimate->bytes;
		const auto bytes = ModelStorageBytes(model, modelGuides, nativeControlMask) + estimate->bytes +
		                   std::uint64_t(description.Width) * description.Height * (format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 32u : format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 16u :
																																												12u);
		const auto budget = MemorySample(frame.device);
		now = GetTickCount64();
		if (!state.memory.Admit(budget, bytes, now,
				Util::GpuMemoryBudget::Get().PendingBytes(Util::GpuMemoryBudget::Owner::NeuralRendering))) {
			if (state.memory.phase == MemoryRecoveryPhase::Retiring)
				state.retiring = true;
			state.snapshot.prewarmStatus = "waiting_for_gpu_memory";
			state.PublishBudget();
			return;
		}
		auto reservation = Util::GpuMemoryBudget::Get().Reserve(Util::GpuMemoryBudget::Owner::NeuralRendering, frame.device, bytes);
		if (!reservation)
			return;
		state.prewarm.Capture(key, now);
		state.historyValid = false;
		if (!state.PrepareStorage(frame, settings, description, format, model, modelGuides, true, now, nativeControlMask)) {
			state.prewarm.failed = true;
			state.snapshot.prewarmStatus = "prewarm_storage_failed";
			return;
		}
		// Discovery can resolve a symbolic network selection; capture the actual native preset.
		state.prewarm.key = state.CreationKey(frame, settings.mode, format, layout, runtime.GetNetworkPreset(settings.network));
		Util::ScopedDeviceContextState pipeline(state.context1.get(), state.isolated.get());
		frame.context->ClearState();
		const float black[4]{};
		frame.context->ClearUnorderedAccessViewFloat(state.modelColor.uav.Get(), black);
		auto* commands = state.interop.Begin();
		if (!commands) {
			state.snapshot.prewarmStatus = "waiting_for_inference_queue";
			return;
		}
		if (!state.ReserveNative(nativeKey, estimate->bytes, now, false)) {
			state.retiring = true;
			state.snapshot.prewarmStatus = "native_reservation_unavailable";
			if (!state.interop.Submit())
				state.Quarantine("The prewarm reservation failure lost its completion fence");
			return;
		}
		Transition(commands, state.modelColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
		if (nativeControlMask)
			Transition(commands, state.modelControlMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
		RuntimeExecutionEvidence* nativeEvidence = nullptr;
#ifdef DEVBENCH_BRIDGE_ENABLED
		auto& recorded = state.executions[state.executionCursor++ % state.executions.size()];
		recorded = { .frame = frame.frame, .prewarm = true, .layout = layout, .settings = settings, .experiments = state.experiments };
		nativeEvidence = &recorded.native;
#endif
		const bool prepared = runtime.PrepareFeature(commands, 0, layout, state.modelColor.shared.Get(), state.modelControlMask.shared.Get(), false, nativeEvidence, state.prewarm.key.network);
		state.SampleNativeStartup();
		Transition(commands, state.modelColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
		if (nativeControlMask)
			Transition(commands, state.modelControlMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
		if (!state.interop.Submit()) {
			state.Quarantine("The menu prewarm submission lost its completion fence");
			return;
		}
		state.RecordNativeSubmission();
#ifdef DEVBENCH_BRIDGE_ENABLED
		recorded.submitted = true;
#endif
		if (!prepared) {
			state.prewarm.failed = true;
			state.retiring = true;
			if (runtime.CreationCapacityFailure() == CapacityFailure::Pressure)
				state.memory.Suspend(now, true);
			else
				state.runtimeFailed = true;
			state.snapshot.prewarmStatus = "native_prewarm_failed";
			state.Status("prewarm_failed", runtime.Detail());
			return;
		}
		state.prewarm.Prepared();
		state.snapshot.prewarmStatus = state.prewarm.stage;
		state.lastRequestedMs = now;
		state.layout = layout;
		state.snapshot.modelWidth = model.width;
		state.snapshot.modelHeight = model.height;
		state.snapshot.effectivePercent = state.resolution.EffectivePercent(settings.modelResolutionPercent);
		++state.snapshot.prewarms;
		state.Status("prewarmed_at_menu");
		state.PublishBudget();
	}

	ID3D11Texture2D* MonoRenderer::Apply(const MonoFrame& frame, const MonoSettings& requested)
	{
		std::scoped_lock lock(state_->mutex);
		auto& state = *state_;
		state.resultReady = false;
		auto now = GetTickCount64();
		state.lastRequestedMs = now;
		state.requested = true;
		auto settings = requested;
		Sanitize(settings);
		if (state.quarantined || !settings.enabled || !frame.color || !frame.width || !frame.height ||
			frame.width > 16384 || frame.height > 16384 || state.guideFrame != frame.frame ||
			!TextureBelongsTo(frame.color, frame.device) || !state.BindState(frame.device, frame.context))
			return nullptr;
		if (state.ServiceStall(now))
			return nullptr;
		if (state.retiring) {
			(void)state.Retire();
			return nullptr;
		}
		if (state.evaluatedFrame == frame.frame)
			return nullptr;
		if (const auto pressureAt = state.dlssPressureAtMs.exchange(0, std::memory_order_acq_rel);
			pressureAt && pressureAt <= now && now - pressureAt <= MemoryBudgetSample::kMaximumAgeMs)
			state.memory.ReportDlssWarning(now);
		state.resolution.SetEnabled(settings.pressureResolution);
		if (state.memory.phase == MemoryRecoveryPhase::Retiring) {
			(void)state.Retire();
			return nullptr;
		}
		bool experimentChanged = false, nativeControlMask = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		experimentChanged = state.experimentsChanged;
		nativeControlMask = state.experiments.nativeControlMask;
#endif
		const bool changed = !state.settingsValid || state.appliedSettings != settings || experimentChanged;
		const bool domainChanged = state.settingsValid &&
		                           (state.appliedSettings.mode != settings.mode || state.appliedSettings.network != settings.network);
		if (changed) {
			state.historyValid = false;
			state.packedSources = {};
			state.packedCapacity = {};
		}
		if (state.runtimeFailed && !changed) {
			state.Status("runtime_failure", Runtime::Instance().Detail());
			return nullptr;
		}
		if (changed)
			state.runtimeFailed = false;
		D3D11_TEXTURE2D_DESC description{};
		frame.color->GetDesc(&description);
		const auto format = RawColorFormat(description.Format);
		if (format == DXGI_FORMAT_UNKNOWN || description.SampleDesc.Count != 1 || description.ArraySize != 1 || description.MipLevels != 1 ||
			frame.width > description.Width || frame.height > description.Height) {
			state.Status("unsupported_scene_format");
			return nullptr;
		}
		const auto percent = state.resolution.EffectivePercent(settings.modelResolutionPercent);
		const auto unpackedModel = BuildModelResolutionExtent(frame.width, frame.height, percent);
		if (unpackedModel.width < kMinimumExperimentalContextExtent || unpackedModel.height < kMinimumExperimentalContextExtent) {
			state.Status("inference_extent_too_small");
			return nullptr;
		}
		const bool categoryRequested = settings.charactersOnly || settings.sceneCategoryStrengths;
		const bool categoryReady = state.categories && state.authoredDepth && state.categoryFrame == frame.frame &&
		                           state.categoryWidth == state.guideWidth && state.categoryHeight == state.guideHeight;
		if (categoryRequested && !categoryReady) {
			state.historyValid = false;
			state.Status("waiting_for_scene_categories");
			return nullptr;
		}
		const auto screenRegion = BuildMonoRegion(frame.width, frame.height, settings);
		auto region = screenRegion;
		if (settings.charactersOnly && settings.cropToCharacters && !state.actorUncertain) {
			if (state.actorFrame != frame.frame || !state.actorUnion.IsValid()) {
				state.historyValid = false;
				state.Status("empty_actor_selection");
				return nullptr;
			}
			const auto& bounds = state.actorUnion;
			const ComputeSubrect actorRegion{ bounds.minX, bounds.minY, bounds.maxX - bounds.minX, bounds.maxY - bounds.minY };
			auto projected = MapComputeSubrect(actorRegion, state.guideWidth, state.guideHeight, frame.width, frame.height);
			constexpr std::uint32_t margin = 16;
			const auto left = projected.baseX > margin ? projected.baseX - margin : 0u;
			const auto top = projected.baseY > margin ? projected.baseY - margin : 0u;
			const auto right = std::min(frame.width, projected.baseX + projected.width + margin);
			const auto bottom = std::min(frame.height, projected.baseY + projected.height + margin);
			region = IntersectComputeSubrect(region, { left, top, right - left, bottom - top });
		}
		if (!region.IsValid()) {
			state.historyValid = false;
			state.Status("empty_region");
			return nullptr;
		}
		auto modelRegion = MapComputeSubrect(region, frame.width, frame.height, unpackedModel.width, unpackedModel.height);
		modelRegion = ExpandNativeRegion(modelRegion, unpackedModel);
		PackedCropLayout packed{};
		PackedCropSources selectedSources{};
		if (settings.charactersOnly && settings.cropToCharacters && settings.packedCharacters &&
			!state.actorUncertain && state.actorFrame == frame.frame) {
			std::vector<CharacterRect> regions;
			regions.reserve(state.actorRegions.size());
			for (const auto& actor : state.actorRegions) {
				const auto projected = MapComputeSubrect({ actor.minX, actor.minY, actor.maxX - actor.minX, actor.maxY - actor.minY },
					state.guideWidth, state.guideHeight, frame.width, frame.height);
				const auto clipped = IntersectComputeSubrect(projected, screenRegion);
				if (clipped.IsValid())
					regions.push_back({ clipped.baseX, clipped.baseY, clipped.baseX + clipped.width, clipped.baseY + clipped.height });
			}
			selectedSources = ResolvePackedCropSources(regions, frame.width, frame.height, screenRegion, state.packedSources);
			packed = ResolvePackedCropLayout(selectedSources, { frame.width, frame.height }, unpackedModel, screenRegion,
				modelRegion.Area(), frame.frame, state.packedCapacity);
		} else {
			state.packedSources = {};
			state.packedCapacity = {};
		}
		auto compression = BuildMonoCompressionPlan({ frame.width, frame.height }, { state.guideWidth, state.guideHeight },
			percent, settings.peripheralCompressionPercent, settings.charactersOnly || settings.sceneCategoryStrengths || packed.count != 0,
			region != ComputeSubrect{ 0, 0, frame.width, frame.height });
		state.resolution.projectedCheaperPercent = 0;
		if (settings.pressureResolution) {
			for (auto candidate = percent > MemoryResolutionPolicy::kStepPercent ? percent - MemoryResolutionPolicy::kStepPercent : 0u;
				candidate >= kMinimumModelResolutionPercent; --candidate) {
				const auto projected = BuildMonoCompressionPlan({ frame.width, frame.height }, { state.guideWidth, state.guideHeight },
					candidate, settings.peripheralCompressionPercent, categoryRequested, settings.regionPercent != 100);
				auto candidateExtent = projected.model;
				if (packed.count) {
					const auto projectedUnpacked = BuildModelResolutionExtent(frame.width, frame.height, candidate);
					const auto projectedRegion = ExpandNativeRegion(MapComputeSubrect(region, frame.width, frame.height, projectedUnpacked.width, projectedUnpacked.height), projectedUnpacked);
					auto projectedCapacity = state.packedCapacity;
					const auto projectedPacked = ResolvePackedCropLayout(selectedSources, { frame.width, frame.height }, projectedUnpacked, screenRegion,
						projectedRegion.Area(), frame.frame, projectedCapacity);
					candidateExtent = projectedPacked.count ? projectedPacked.extent : projectedUnpacked;
				}
				const auto currentExtent = packed.count ? packed.extent : compression.model;
				if (candidateExtent.width >= kMinimumExperimentalContextExtent && candidateExtent.height >= kMinimumExperimentalContextExtent &&
					std::uint64_t(candidateExtent.width) * candidateExtent.height < std::uint64_t(currentExtent.width) * currentExtent.height) {
					state.resolution.projectedCheaperPercent = candidate;
					break;
				}
			}
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		state.faces.current = state.faceFrame == frame.frame;
		const float densityFloor = std::max(compression.maximumFootprint[0], compression.maximumFootprint[1]) / kPeripheralMaximumFootprint;
		ApplyFaceDetailProtection(compression, state.faces, state.experiments.reconstruction.faceDetailProtectionPercent, densityFloor);
#endif
		state.snapshot.compressionStatus = compression.reason;
		const auto model = packed.count ? packed.extent : compression.model;
		const auto modelGuides = packed.count ? model : BuildMonoGuideExtent({ frame.width, frame.height }, { state.guideWidth, state.guideHeight }, model);
		if (packed.count || compression.active)
			modelRegion = { 0, 0, model.width, model.height };
		const bool rebuild = !state.modelColor.texture || state.modelExtent != model || state.modelGuideExtent != modelGuides ||
		                     state.sourceDescription.Width != description.Width || state.sourceDescription.Height != description.Height ||
		                     state.sourceDescription.Format != format || domainChanged || (state.modelControlMask.texture != nullptr) != nativeControlMask;
		auto layout = BuildNativeEvaluationLayout(model, modelGuides, model, nativeControlMask ? model : UpscalingDLSS::Extent{}, modelRegion,
			{ true, float(modelGuides.width), float(modelGuides.height) }, modelGuides != model);
		layout.nativeControlMask = nativeControlMask;
#ifdef DEVBENCH_BRIDGE_ENABLED
		// Nonlinear and atlas preparation consume jitter; only a uniform full grid can forward its phase.
		if (state.experiments.nativeJitter && !compression.active && !packed.count)
			layout.jitterPixels = { frame.nativeJitter[0] * modelGuides.width / state.guideWidth, frame.nativeJitter[1] * modelGuides.height / state.guideHeight };
#endif
		const auto nativeKey = NativeKey(layout, Runtime::Instance().GetNetworkPreset(settings.network));
		const auto estimate = state.nativeMemory.Project(nativeKey);
		if (!estimate) {
			state.Status("invalid_native_memory_geometry");
			return nullptr;
		}
		state.nativeProjectedBytes = estimate->bytes;
		if (rebuild && state.modelColor.texture && !changed && percent > state.snapshot.effectivePercent) {
			const auto growthBytes = ModelStorageBytes(model, modelGuides, nativeControlMask) + estimate->bytes +
			                         std::uint64_t(description.Width) * description.Height *
			                             (format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 32u : format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 16u :
																																	  12u);
			const auto growthBudget = MemorySample(frame.device);
			now = GetTickCount64();
			if (!state.memory.CanAllocateOutput(growthBudget, growthBytes, now,
					Util::GpuMemoryBudget::Get().PendingBytes(Util::GpuMemoryBudget::Owner::NeuralRendering))) {
				(void)state.resolution.RejectRestoration(state.snapshot.effectivePercent);
				state.Status("restoration_deferred");
				return nullptr;
			}
		}
		if (rebuild && state.modelColor.texture) {
			state.appliedSettings = settings;
			state.settingsValid = true;
			(void)state.Retire();
			return nullptr;
		}
		if (const auto proof = state.coverage.Poll(frame.context)) {
			if (proof->covered) {
				state.coveredFrame = proof->frame;
				if (state.presentedFrame == proof->frame)
					state.historyValid = true;
			} else {
				++state.snapshot.packedCoverageRejected;
				state.historyValid = false;
			}
		}
		if (state.prewarm.unused) {
			const bool exact = state.prewarm.Adopt(state.CreationKey(frame, settings.mode, format, layout,
				Runtime::Instance().GetNetworkPreset(settings.network)));
			state.snapshot.prewarmStatus = state.prewarm.stage;
			if (!exact) {
				(void)state.Retire();
				return nullptr;
			}
		}
		const std::uint64_t sourceBytes = std::uint64_t(description.Width) * description.Height *
		                                  (format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 32u : format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 16u :
																																	   12u);
		const bool needsNativeFeature = !Runtime::Instance().HasCompatibleFeature(0, layout, false,
			Runtime::Instance().GetNetworkPreset(settings.network));
		// ROI/network changes can recreate a native feature while retaining our textures.
		// Reserve that peak as well as first-allocation storage until submission completes.
		const auto nativeBytes = needsNativeFeature ? estimate->bytes : 0u;
		const auto additional = (rebuild ? ModelStorageBytes(model, modelGuides, nativeControlMask) + sourceBytes : 0u) + nativeBytes;
		if (state.nativeStartup.active && state.nativeStartup.evaluated) {
			state.Status("waiting_for_native_startup");
			state.PublishBudget();
			return nullptr;
		}
		const std::array<std::uint64_t, 9> memoryCapacity{ description.Width, description.Height, static_cast<std::uint64_t>(format), model.width, model.height, modelGuides.width, modelGuides.height, settings.network, nativeControlMask };
		if (state.memoryCapacity != memoryCapacity) {
			state.memory.InvalidateCapacityLearning();
			state.memoryCapacity = memoryCapacity;
		}
		const auto budget = MemorySample(frame.device);
		now = GetTickCount64();
		const auto otherPending = Util::GpuMemoryBudget::Add(Util::GpuMemoryBudget::Get().PendingBytes(Util::GpuMemoryBudget::Owner::NeuralRendering), state.nativeReservation.Bytes());
		if (!state.memory.Admit(budget, additional, now, otherPending)) {
			if (state.memory.phase == MemoryRecoveryPhase::Retiring)
				(void)state.Retire();
			else
				(void)state.resolution.WhileWaiting(settings.modelResolutionPercent, budget, now, true);
			state.Status("waiting_for_gpu_memory");
			state.PublishBudget();
			return nullptr;
		}
		state.PublishBudget();
		Util::GpuMemoryBudget::Reservation reservation;
		if (additional) {
			reservation = Util::GpuMemoryBudget::Get().Reserve(Util::GpuMemoryBudget::Owner::NeuralRendering, frame.device, additional);
			if (!reservation)
				return nullptr;
		}
		if (!state.PrepareStorage(frame, settings, description, format, model, modelGuides, rebuild, now, nativeControlMask))
			return nullptr;
		auto& runtime = Runtime::Instance();
		state.appliedSettings = settings;
		state.settingsValid = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
		state.experimentsChanged = false;
#endif
		state.ConfigureShaders(compression.active);
		const auto& defines = state.shaderDefines;
		state.snapshot.modelWidth = model.width;
		state.snapshot.modelHeight = model.height;
		state.snapshot.effectivePercent = percent;
		state.snapshot.packedRegions = packed.count;
		auto* prepare = state.prepareShader.Get(L"Data/Shaders/Upscaling/NeuralRendering/MonoPrepareCS.hlsl", defines, "cs_5_0", "main", "NeuralRendering::Prepare");
		auto* reconstruct = state.reconstructShader.Get(L"Data/Shaders/Upscaling/NeuralRendering/MonoReconstructCS.hlsl", defines, "cs_5_0", "main", "NeuralRendering::Reconstruct");
		auto* coverage = packed.count ? state.coverageShader.Get(L"Data/Shaders/Upscaling/NeuralRendering/MonoCoverageCS.hlsl", defines, "cs_5_0", "main", "NeuralRendering::PackedCoverage CS") : nullptr;
		if (!prepare || !reconstruct || (packed.count && (!coverage || !state.coverage.Initialize(frame.device)))) {
			state.Status("shader_or_coverage_unavailable");
			return nullptr;
		}
		Util::ScopedDeviceContextState pipeline(state.context1.get(), state.isolated.get());
		frame.context->ClearState();
		MonoConstants constants{};
		constants.source = { frame.width, frame.height };
		constants.model = { model.width, model.height };
		constants.guides = { state.guideWidth, state.guideHeight };
		constants.modelGuides = { modelGuides.width, modelGuides.height };
		constants.categorySize = { state.categoryWidth, state.categoryHeight };
		constants.region = { float(settings.regionPercent) * 0.01f, settings.horizontalScale, settings.regionOffset[0], settings.regionOffset[1] };
		std::copy_n(settings.categoryStrengths.begin(), 4, constants.strengths.begin());
		constants.selection = { settings.categoryStrengths[4], settings.charactersOnly ? 0.0f : 1.0f, float(settings.featherPixels), 0.002f };
		constants.depth = frame.depthLinearization;
		constants.sampling = { frame.guideJitter[0], frame.guideJitter[1], 0.0f, 0.0f };
		constants.flags = { categoryRequested ? 1u : 0u, settings.categoryMask, settings.guidedReconstruction ? 1u : 0u, settings.sceneCategoryStrengths ? 1u : 0u };
		constants.inference = { modelRegion.baseX, modelRegion.baseY, modelRegion.width, modelRegion.height };
		constants.reconstruction = { settings.guidedConfidenceRejectionPercent ? 200.0f / settings.guidedConfidenceRejectionPercent : 0.0f, 1.0f, 1.0f, 1.0f };
#ifdef DEVBENCH_BRIDGE_ENABLED
		constants.reconstruction[1] = state.experiments.reconstruction.shadowTransferPercent * 0.01f;
		constants.reconstruction[2] = state.experiments.reconstruction.perChannelClamping ? 1.0f : 0.0f;
		constants.reconstruction[3] = state.experiments.reconstruction.depthSampleMotion ? 1.0f : 0.0f;
#endif
		PackedConstants atlas{};
		atlas.configuration = { packed.count, unpackedModel.width, unpackedModel.height, 0 };
		for (std::uint32_t index = 0; index < packed.count; ++index) {
			const auto rect = [](const ComputeSubrect& value) { return std::array{ value.baseX, value.baseY, value.width, value.height }; };
			atlas.source[index] = rect(packed.tiles[index].source);
			atlas.model[index] = rect(packed.tiles[index].model);
			atlas.packed[index] = rect(packed.tiles[index].packed);
		}
		ColorConstants color{};
		color.size = { frame.width, frame.height };
		color.mode = settings.colorMode == 3 ? 2 : settings.colorMode;
		color.domain = frame.linearColor ? 1u : 2u;
		color.transform = frame.linearColor && settings.colorMode != 0 ? 2u : 0u;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (state.experiments.nvidiaReferenceColor && frame.linearColor && settings.colorMode != 0)
			color.transform = 3;
#endif
		color.exposure = settings.exposure;
		color.detail = settings.detailStrength;
		color.lighting = settings.colorMode == 3 ? 0.0f : settings.lightingPreservation;
		if (format == DXGI_FORMAT_R32G32B32A32_FLOAT)
			color.flags = 0;
		else if (format == DXGI_FORMAT_R11G11B10_FLOAT)
			color.flags = 0x100;
		else if (format != DXGI_FORMAT_R16G16B16A16_FLOAT)
			color.flags = 0x300;
		Color::ExposureStamp usedExposure{};
		if (settings.exposureSource != 0 && color.transform != 0) {
			const Color::ExposureTransaction transaction{ frame.frame, frame.frame, settings.mode == 2 ? 1u : 0u, 0u, 0u,
				settings.exposureSource == 2 ? Color::ExposureSource::CapturedHDRPrevious : Color::ExposureSource::CapturedHDR };
			if (!Color::ExposureCapture::Instance().Bind(frame.context, state.exposure, transaction) ||
				state.exposure.state != Color::ExposureBindingState::SnapshotQueued) {
				state.historyValid = false;
				state.Status("waiting_for_captured_exposure", Color::ExposureBindingName(state.exposure.state));
				return nullptr;
			}
			color.flags |= 4u;
			usedExposure = state.exposure.evidence.stamp;
		}
		if (!Upload(frame.device, frame.context, state.monoBuffer, constants, "NeuralRendering::MonoConstants") ||
			!Upload(frame.device, frame.context, state.colorBuffer, color, "NeuralRendering::ColorConstants") ||
			!Upload(frame.device, frame.context, state.packedBuffer, atlas, "NeuralRendering::PackedConstants") ||
			!Upload(frame.device, frame.context, state.peripheralBuffer, compression.mapping, "NeuralRendering::PeripheralMapping"))
			return nullptr;
		frame.context->CopyResource(state.source.texture.Get(), frame.color);
		ID3D11Buffer* buffers[]{ state.colorBuffer.Get(), state.monoBuffer.Get(), state.packedBuffer.Get(), state.peripheralBuffer.Get() };
		ID3D11ShaderResourceView* inputs[]{ state.source.srv.Get(), state.guideDepth.srv.Get(), state.guideMotion.srv.Get(), state.exposure.srv.Get(),
			state.categories.Get(), state.authoredDepth.Get(), state.guideDepth.srv.Get() };
		ID3D11UnorderedAccessView* outputs[]{ state.modelColor.uav.Get(), state.modelDepth.uav.Get(), state.modelMotion.uav.Get(), state.modelControlMask.uav.Get() };
		Util::BindFrameBufferConstantBuffersForCS(frame.context);
		frame.context->CSSetConstantBuffers(0, 4, buffers);
		frame.context->CSSetShaderResources(0, 7, inputs);
		if (packed.count) {
			state.coverage.Clear(frame.context);
			auto* proofOutput = state.coverage.Output();
			frame.context->CSSetUnorderedAccessViews(0, 1, &proofOutput, nullptr);
			frame.context->CSSetShader(coverage, nullptr, 0);
			{
				CS_GPU_PASS("NeuralRendering::PackedCoverage");
				frame.context->Dispatch((frame.width + 7) / 8, (frame.height + 7) / 8, 1);
			}
			ID3D11UnorderedAccessView* empty = nullptr;
			frame.context->CSSetUnorderedAccessViews(0, 1, &empty, nullptr);
			state.coverage.QueueReadback(frame.context, frame.frame);
		}
		frame.context->CSSetUnorderedAccessViews(0, nativeControlMask ? 4 : 3, outputs, nullptr);
		frame.context->CSSetShader(prepare, nullptr, 0);
		{
			CS_GPU_PASS("NeuralRendering::Prepare");
			frame.context->Dispatch((std::max(modelRegion.width, layout.depth.valid.width) + 7) / 8, (std::max(modelRegion.height, layout.depth.valid.height) + 7) / 8, 1);
		}
		frame.context->ClearState();
		auto* commands = state.interop.Begin();
		if (!commands) {
			if (state.interop.Failed())
				state.Quarantine("The mono inference queue could not prove its previous completion");
			else
				state.Status("waiting_for_inference_queue");
			state.historyValid = false;
			return nullptr;
		}
		Transition(commands, state.modelColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
		if (!state.ReserveNative(nativeKey, nativeBytes, now, true)) {
			state.retiring = true;
			state.Status("native_reservation_unavailable");
			Transition(commands, state.modelColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
			if (!state.interop.Submit())
				state.Quarantine("The native reservation failure lost its completion fence");
			return nullptr;
		}
		if (nativeControlMask)
			Transition(commands, state.modelControlMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
		Transition(commands, state.modelDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
		Transition(commands, state.modelMotion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
		Transition(commands, state.modelOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
		const auto network = runtime.GetNetworkPreset(settings.network);
		const bool reset = changed || frame.reset || settings.mode == static_cast<std::uint32_t>(RenderingMode::ReducedResolution) ||
		                   !state.historyValid || state.evaluatedFrame + 1u != frame.frame || !SameHistoryLayout(state.layout, layout) || state.packedLayout != packed || state.mapping != compression.mapping;
		Tuning tuning{};
		tuning.networkPreset = network;
		tuning.style = settings.style;
		tuning.intensity = settings.intensity;
		tuning.localToneStrength = settings.localTone;
		tuning.localStructureStrength = settings.localStructure;
		tuning.skinStructureStrength = settings.skinStructure;
		RuntimeExecutionEvidence* nativeEvidence = nullptr;
#ifdef DEVBENCH_BRIDGE_ENABLED
		auto& recorded = state.executions[state.executionCursor++ % state.executions.size()];
		recorded = { .frame = frame.frame, .historyReset = reset, .layout = layout, .settings = settings, .experiments = state.experiments };
		nativeEvidence = &recorded.native;
#endif
		bool preparedNative = true;
		RuntimeExecutionEvidence creationEvidence{};
		if (needsNativeFeature) {
			preparedNative = runtime.PrepareFeature(commands, 0, layout, state.modelColor.shared.Get(), state.modelControlMask.shared.Get(), false, nativeEvidence, network);
			if (nativeEvidence)
				creationEvidence = *nativeEvidence;
			state.SampleNativeStartup();
		}
		const bool succeeded = preparedNative && runtime.Execute(commands, 0, state.modelColor.shared.Get(), state.modelDepth.shared.Get(), state.modelMotion.shared.Get(),
													 state.modelOutput.shared.Get(), state.modelControlMask.shared.Get(), layout, tuning, reset, nullptr, nativeEvidence,
													 &state.interop, globals::profiler ? globals::profiler->GetGpuCaptureEpoch() : 0, frame.frame);
		if (nativeEvidence && creationEvidence.createAttempted) {
			nativeEvidence->createAttempted = creationEvidence.createAttempted;
			nativeEvidence->createSucceeded = creationEvidence.createSucceeded;
			nativeEvidence->createReason = creationEvidence.createReason;
			nativeEvidence->createResult = creationEvidence.createResult;
			nativeEvidence->createCpuMicroseconds = creationEvidence.createCpuMicroseconds;
		}
		if (state.nativeStartup.active)
			state.SampleNativeStartup();
		Transition(commands, state.modelColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
		Transition(commands, state.modelDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
		Transition(commands, state.modelMotion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
		Transition(commands, state.modelOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
		if (nativeControlMask)
			Transition(commands, state.modelControlMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
		if (!state.interop.Submit()) {
			state.Quarantine("The mono inference submission has no proven completion fence");
			return nullptr;
		}
		state.RecordNativeSubmission();
#ifdef DEVBENCH_BRIDGE_ENABLED
		recorded.submitted = true;
#endif
		if (!succeeded) {
			state.historyValid = false;
			if (runtime.CreationCapacityFailure() == CapacityFailure::Pressure)
				state.memory.Suspend(now, true);
			else
				state.runtimeFailed = true;
			state.retiring = true;
			state.Status("inference_failed", runtime.Detail());
			return nullptr;
		}
		++state.snapshot.evaluations;
		if (reset)
			++state.snapshot.historyResets;
		state.evaluatedFrame = frame.frame;
		state.layout = layout;
		state.packedLayout = packed;
		state.mapping = compression.mapping;
		state.snapshot.evaluationFrame = frame.frame;
		state.snapshot.sourceWidth = frame.width;
		state.snapshot.sourceHeight = frame.height;
		state.snapshot.exposureFrame = usedExposure.frame;
		state.snapshot.exposureEpoch = usedExposure.epoch;
		state.snapshot.exposureSequence = usedExposure.sequence;
		state.snapshot.packedCoverageProven = packed.count == 0;
		state.historyValid = false;
		ID3D11ShaderResourceView* reconstruction[]{ state.source.srv.Get(), state.modelOutput.srv.Get(), state.modelColor.srv.Get(), state.exposure.srv.Get(),
			state.categories.Get(), state.authoredDepth.Get(), state.guideDepth.srv.Get(), packed.count ? state.coverage.Input() : nullptr };
		auto* output = state.result.uav.Get();
		frame.context->CSSetShader(reconstruct, nullptr, 0);
		frame.context->CSSetShaderResources(0, 8, reconstruction);
		frame.context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		Util::BindFrameBufferConstantBuffersForCS(frame.context);
		frame.context->CSSetConstantBuffers(0, 4, buffers);
		{
			CS_GPU_PASS("NeuralRendering::Reconstruct");
			frame.context->Dispatch((frame.width + 7) / 8, (frame.height + 7) / 8, 1);
		}
		frame.context->ClearState();
		state.resultReady = true;
		state.Status("ready");
		state.PublishBudget();
		return state.result.texture.Get();
	}

	bool MonoRenderer::Commit(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* destination)
	{
		std::scoped_lock lock(state_->mutex);
		auto& state = *state_;
		if (!state.resultReady || !TextureBelongsTo(destination, device) || !state.BindState(device, context)) return false;
		D3D11_TEXTURE2D_DESC description{}; destination->GetDesc(&description);
		if (description.Width != state.sourceDescription.Width || description.Height != state.sourceDescription.Height ||
			description.ArraySize != 1 || description.MipLevels != 1 || description.SampleDesc.Count != 1 || description.SampleDesc.Quality != 0) return false;
		D3D11_RENDER_TARGET_VIEW_DESC view{};
		view.Format = RawColorFormat(description.Format); view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		if (view.Format == DXGI_FORMAT_UNKNOWN) return false;
		ComPtr<ID3D11RenderTargetView> target;
		if (FAILED(device->CreateRenderTargetView(destination, &view, &target))) return false;
		Util::SetResourceName(target.Get(), "NeuralRendering::SceneCommit RTV");
		auto* vertex = state.presentVertex.Get(L"Data/Shaders/Upscaling/NeuralRendering/MonoPresent.hlsl", {}, "vs_5_0", "VSMain", "NeuralRendering::Commit VS");
		auto* pixel = state.presentPixel.Get(L"Data/Shaders/Upscaling/NeuralRendering/MonoPresent.hlsl", {}, "ps_5_0", "PSMain", "NeuralRendering::Commit PS");
		if (!vertex || !pixel) return false;
		Util::ScopedDeviceContextState pipeline(state.context1.get(), state.isolated.get());
		context->ClearState();
		auto* rtv = target.Get(); auto* input = state.result.srv.Get();
		const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, float(description.Width), float(description.Height), 0.0f, 1.0f };
		context->RSSetViewports(1, &viewport); context->OMSetRenderTargets(1, &rtv, nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertex, nullptr, 0); context->PSSetShader(pixel, nullptr, 0); context->PSSetShaderResources(0, 1, &input);
		{ CS_GPU_PASS("NeuralRendering::Commit"); context->Draw(3, 0); }
		context->ClearState();
		return true;
	}

	void MonoRenderer::AcknowledgePresentation()
	{
		std::scoped_lock lock(state_->mutex);
		if (!state_->resultReady || state_->presentedFrame == state_->evaluatedFrame) return;
		Runtime::Instance().AcknowledgePresentedFeatures(1u);
#ifdef DEVBENCH_BRIDGE_ENABLED
		for (auto& record : state_->executions)
			if (record.frame == state_->evaluatedFrame && !record.prewarm && record.native.evaluateSucceeded) record.committed = true;
#endif
		state_->presentedFrame = state_->evaluatedFrame;
		state_->historyValid = state_->packedLayout.count == 0 || state_->coveredFrame == state_->evaluatedFrame;
		state_->snapshot.presentationFrame = state_->presentedFrame;
		state_->snapshot.packedCoverageProven = state_->historyValid;
		++state_->snapshot.presented;
		const auto budget = MemorySample(state_->device.Get());
		const auto now = GetTickCount64();
		state_->memory.Succeeded(now);
		(void)state_->resolution.ObserveSuccess(state_->appliedSettings.modelResolutionPercent, budget,
			now, Util::GpuMemoryBudget::Add(Util::GpuMemoryBudget::Get().PendingBytes(Util::GpuMemoryBudget::Owner::NeuralRendering), state_->nativeReservation.Bytes()), false,
			state_->memory.phase == MemoryRecoveryPhase::Ready);
		state_->PublishBudget();
	}
	void MonoRenderer::ReportDlssMemoryPressure() noexcept { state_->dlssPressureAtMs.store(GetTickCount64(), std::memory_order_release); }
	void MonoRenderer::Service(bool requested, bool configured, bool prewarmEnabled)
	{
		std::scoped_lock lock(state_->mutex);
		state_->requested = requested;
		const auto now = GetTickCount64();
		if (state_->ServiceStall(now))
			return;
		if (!configured)
			state_->runtimeFailed = false;
		if (!configured || !prewarmEnabled) {
			if (state_->prewarm.captured) {
				state_->prewarm.Cancel();
				state_->retiring = true;
			}
			state_->prewarm.suppressed = false;
		}
		if (state_->prewarm.unused && state_->prewarm.Expired(now)) {
			state_->prewarm.Cancel(true);
			state_->retiring = true;
		}
		state_->snapshot.prewarmStatus = state_->prewarm.stage;
		if (requested)
			state_->lastRequestedMs = GetTickCount64();
		else {
			state_->historyValid = false;
			state_->memory.BreakActiveObservation();
			state_->resolution.ClearHealthyWindow();
		}
		if (state_->retiring || (!requested && !state_->prewarm.unused && state_->modelColor.texture && now - state_->lastRequestedMs > 5000u))
			(void)state_->Retire();
		state_->PublishBudget();
	}
	bool MonoRenderer::Retire(bool shutdown)
	{
		std::scoped_lock lock(state_->mutex);
		state_->shutdown |= shutdown;
		if (shutdown) state_->requested = false;
		return state_->Retire();
	}
	void MonoRenderer::ResetWorldHistory()
	{
		std::scoped_lock lock(state_->mutex);
		state_->historyValid = state_->resultReady = false;
		state_->evaluatedFrame = state_->presentedFrame = state_->coveredFrame = UINT32_MAX;
		state_->guideFrame = state_->categoryFrame = state_->actorFrame = UINT32_MAX;
		state_->actors.clear();
		state_->actorRegions.clear();
		state_->actorUnion = {};
		state_->actorUncertain = false;
		state_->packedSources = {};
		state_->packedCapacity = {};
		state_->categories.Reset();
		state_->authoredDepth.Reset();
		state_->snapshot.evaluationFrame = state_->snapshot.presentationFrame = UINT32_MAX;
		state_->snapshot.packedCoverageProven = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		state_->executions = {};
		state_->executionCursor = 0;
		state_->faces = {};
		state_->faceActors = {};
		state_->faceFrame = UINT32_MAX;
#endif
		state_->memory.BreakActiveObservation();
		state_->resolution.ClearHealthyWindow();
		Color::ExposureCapture::Instance().Request(false);
	}

	void MonoRenderer::ResetShaderCache()
	{
		std::scoped_lock lock(state_->mutex);
		state_->guideShader.Reset(); state_->prepareShader.Reset(); state_->reconstructShader.Reset(); state_->coverageShader.Reset();
		Color::ExposureCapture::Instance().InvalidateShaders();
		state_->presentVertex.Reset(); state_->presentPixel.Reset(); state_->historyValid = false; state_->runtimeFailed = false;
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	bool MonoRenderer::SetExperiments(const MonoExperiments& experiments)
	{
		if (!experiments.reconstruction.Valid())
			return false;
		std::scoped_lock lock(state_->mutex);
		if (state_->quarantined || state_->retiring || state_->interop.Snapshot().recording)
			return false;
		if (state_->experiments == experiments)
			return true;
		state_->experiments = experiments;
		state_->experimentsChanged = true;
		state_->wantsFaceDetail.store(experiments.reconstruction.faceDetailProtectionPercent != 0, std::memory_order_release);
		state_->faces = {};
		state_->faceFrame = UINT32_MAX;
		state_->prepareShader.Reset();
		state_->reconstructShader.Reset();
		state_->coverageShader.Reset();
		state_->shaderConfigurationValid = false;
		state_->historyValid = false;
		return true;
	}
	MonoExperiments MonoRenderer::Experiments() const
	{
		std::scoped_lock lock(state_->mutex);
		return state_->experiments;
	}
	bool MonoRenderer::WantsFaceDetail() const noexcept { return state_->wantsFaceDetail.load(std::memory_order_acquire); }
	void MonoRenderer::ObserveFaceDetail(std::uint32_t frame, std::uint32_t actor, const CharacterRect& rectangle,
		std::uint32_t width, std::uint32_t height)
	{
		if (!actor || !width || !height || !rectangle.IsValid() || rectangle.maxX > width || rectangle.maxY > height || !WantsFaceDetail())
			return;
		std::scoped_lock lock(state_->mutex);
		if (state_->faceFrame != frame) {
			state_->faces = {};
			state_->faceActors = {};
			state_->faceFrame = frame;
		}
		for (std::uint32_t index = 0; index < state_->faces.count; ++index)
			if (state_->faceActors[index] == actor)
				return;
		if (state_->faces.count == state_->faces.bounds.size())
			return;
		const auto index = state_->faces.count++;
		state_->faceActors[index] = actor;
		state_->faces.bounds[index] = { float(rectangle.minX) / width, float(rectangle.minY) / height, float(rectangle.maxX) / width, float(rectangle.maxY) / height };
	}
	nlohmann::json MonoRenderer::Diagnostics(std::uint32_t sourceFrame) const
	{
		using Json = nlohmann::json;
		std::scoped_lock lock(state_->mutex);
		const auto& state = *state_;
		Json records = Json::array();
		const auto optional = [](const auto& value) -> Json { return value ? Json(*value) : Json(nullptr); };
		for (std::size_t offset = 0; offset < state.executions.size(); ++offset) {
			const auto& record = state.executions[(state.executionCursor + offset) % state.executions.size()];
			if (record.frame == UINT32_MAX || (sourceFrame != UINT32_MAX && record.frame != sourceFrame))
				continue;
			const auto& native = record.native;
			records.push_back({ { "frame", record.frame }, { "prewarm", record.prewarm }, { "submitted", record.submitted },
				{ "committed", record.committed }, { "historyReset", record.historyReset }, { "settings", Json(record.settings) }, { "experiments", DescribeMonoExperiments(record.experiments) },
				{ "inputWidth", record.layout.creation.input.width }, { "inputHeight", record.layout.creation.input.height },
				{ "outputWidth", record.layout.creation.output.width }, { "outputHeight", record.layout.creation.output.height },
				{ "createAttempted", native.createAttempted }, { "createSucceeded", native.createSucceeded },
				{ "evaluateAttempted", native.evaluateAttempted }, { "evaluateSucceeded", native.evaluateSucceeded },
				{ "createReason", native.createReason ? Json(native.createReason) : Json(nullptr) },
				{ "createResult", optional(native.createResult) }, { "evaluateResult", optional(native.evaluateResult) },
				{ "createCpuMicroseconds", optional(native.createCpuMicroseconds) }, { "evaluateCpuMicroseconds", optional(native.evaluateCpuMicroseconds) } });
		}
		const auto fences = state.interop.Snapshot();
		const auto lifecycle = Runtime::Instance().GetLifecycleCounts();
		return { { "viewCount", 1 }, { "sourceFrame", sourceFrame == UINT32_MAX ? Json(nullptr) : Json(sourceFrame) },
			{ "nativeCalls", std::move(records) }, { "nativeTimingsAreCpu", true },
			{ "experiments", DescribeMonoExperiments(state.experiments) }, { "experimentsSessionOnly", true },
			{ "faceDetail", { { "sourceFrame", state.faceFrame }, { "observedRegions", state.faces.count }, { "current", state.faces.current }, { "active", state.mapping.faceDetail.enabled != 0 } } },
			{ "lifecycle", { { "creates", lifecycle.creates }, { "releases", lifecycle.releases },
							   { "residentFeatureMask", Runtime::Instance().GetResidentFeatureMask() }, { "networkPresets", Runtime::Instance().GetFeatureNetworkPresets() } } },
			{ "memory", { { "phase", ToString(state.memory.phase) }, { "startup", ToString(state.memory.startupKind) },
							{ "nativeEstimatedBytes", state.nativeProjectedBytes }, { "nativePendingBytes", state.nativeReservation.Bytes() },
							{ "nativeStartupPending", state.nativeStartup.active }, { "nativeObservations", state.nativeObservations },
							{ "adaptiveCeilingPercent", state.resolution.ceilingPercent }, { "reductions", state.resolution.reductions }, { "restorations", state.resolution.restorations } } },
			{ "fences", { { "initialized", fences.initialized }, { "issued", fences.issued }, { "completed", fences.completed },
							{ "completedKnown", fences.completedKnown }, { "recording", fences.recording }, { "deviceRemoved", fences.deviceRemoved } } } };
	}
#endif
	MonoSnapshot MonoRenderer::Snapshot() const { std::scoped_lock lock(state_->mutex); return state_->snapshot; }
}
