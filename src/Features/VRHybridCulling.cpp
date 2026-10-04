#include "VRHybridCulling.h"

#include "State.h"
#include "Util.h"
#include "Utils/RendererContextAccess.h"
#include "VRDepthCullingTemporal.h"
#include "VRHybridCullingHistory.h"
#include "VRHybridCullingPolicy.h"
#include "VRHybridCullingSnapshot.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <d3d11_1.h>
#include <stdexcept>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "GpuPass.h"
#	include "Utils/D3DContextProtection.h"
#	include <cassert>
#	include <optional>

#	define HYBRID_PREPARATION_FAILED(reason, epoch) PreparationFailed(reason, epoch, static_cast<bool>(telemetry))
#	define HYBRID_OUTCOME(state, backend, epoch) PublishOutcome(state, backend, epoch)
#	define HYBRID_REJECT_HISTORY(reason) \
		do {                              \
			rejected = true;              \
			rejection = reason;           \
		} while (false)
#	ifdef TRACY_ENABLE
#		define HYBRID_GPU_PASS(name, enabled)                                                                                                              \
			static constexpr tracy::SourceLocationData CS_GPU_PASS_CONCAT(hybrid_source_, __LINE__){ name, __FUNCTION__, __FILE__, (uint32_t)__LINE__, 0 }; \
			std::optional<ScopedGpuPass> CS_GPU_PASS_CONCAT(hybrid_pass_, __LINE__);                                                                        \
			if (enabled)                                                                                                                                    \
			CS_GPU_PASS_CONCAT(hybrid_pass_, __LINE__).emplace(&CS_GPU_PASS_CONCAT(hybrid_source_, __LINE__), name)
#	else
#		define HYBRID_GPU_PASS(name, enabled)                                       \
			std::optional<ScopedGpuPass> CS_GPU_PASS_CONCAT(hybrid_pass_, __LINE__); \
			if (enabled)                                                             \
			CS_GPU_PASS_CONCAT(hybrid_pass_, __LINE__).emplace(name)
#	endif
#else
#	define HYBRID_PREPARATION_FAILED(reason, epoch) false
#	define HYBRID_OUTCOME(state, backend, epoch) ((void)0)
#	define HYBRID_REJECT_HISTORY(reason) rejected = true
#endif

namespace VRHybridCulling
{
	namespace
	{
		using namespace VRHybridCullingPolicy;
		using VRHybridCullingHistory::Batch;
		using VRHybridCullingHistory::EyePose;

		// These wrappers belong to Skyrim VR 1.4.15, whose callsites gate installation.
		struct NativeBuffer
		{
			ID3D11Buffer* buffer;
			ID3D11ShaderResourceView* srv;
			ID3D11UnorderedAccessView* uav;
			ID3D11Buffer* staging;
			std::uint64_t reserved;
			std::uint32_t count;
		};
		static_assert(offsetof(NativeBuffer, staging) == 0x18);
		static_assert(offsetof(NativeBuffer, count) == 0x28);
		static_assert(sizeof(NativeBuffer) == 0x30);

		template <class T>
		T ReadField(const void* a_object, std::size_t a_offset)
		{
			T value{};
			std::memcpy(&value, static_cast<const std::byte*>(a_object) + a_offset, sizeof(value));
			return value;
		}

		struct Frame
		{
			BuildConstants build{};
			TestConstants test{};
			std::array<EyePose, kEyeCount> poses{};
			RE::NiTransform worldCamera{};
			VRHybridCullingSnapshot::Source source{};
			std::uint64_t epoch = 0;
			winrt::com_ptr<ID3D11ShaderResourceView> depth;
		};

		struct Resources
		{
			winrt::com_ptr<ID3D11Device> device;
			winrt::com_ptr<ID3D11DeviceContext> ownerContext;
			winrt::com_ptr<ID3D11DeviceContext1> context;
			winrt::com_ptr<ID3DDeviceContextState> isolatedState;
			winrt::com_ptr<ID3D11ComputeShader> build;
			winrt::com_ptr<ID3D11ComputeShader> reduce;
			winrt::com_ptr<ID3D11ComputeShader> test;
#ifdef DEVBENCH_BRIDGE_ENABLED
			winrt::com_ptr<ID3D11ComputeShader> diagnosticTest;
			winrt::com_ptr<ID3D11Buffer> diagnosticBuffer, diagnosticStaging;
			winrt::com_ptr<ID3D11UnorderedAccessView> diagnosticUAV;
#endif
			winrt::com_ptr<ID3D11Buffer> constants;
			winrt::com_ptr<ID3D11Texture2D> pyramid;
			winrt::com_ptr<ID3D11ShaderResourceView> allMips;
			std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> mipSRVs;
			std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> mipUAVs;
			PyramidLayout layout{};
			bool failed = false;

			VRHybridCullingSnapshot::PipelineOwner Owner() const
			{
				return { reinterpret_cast<std::uintptr_t>(device.get()), reinterpret_cast<std::uintptr_t>(ownerContext.get()) };
			}
		};

		struct History
		{
			Batch batch{};
			Frame frame{};
			std::array<OBBTransform, VRDepthCullingTemporalPolicy::kMaximumObjects> bounds{};
			bool pending = false;
			bool pipelineInvalidated = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
			winrt::com_ptr<ID3D11Buffer> diagnosticStaging;
			winrt::com_ptr<ID3D11DeviceContext> diagnosticContext;
			std::uint64_t diagnosticWindow = 0;
#endif
		};

		Resources g_resources;
		Frame g_prepared;
		History g_history;
		bool g_ready = false;
		std::atomic_bool g_reloadRequested{ false };
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::atomic_bool g_traversalEnabled{ false }, g_traversalAvailable{ false };
		std::atomic<const char*> g_traversalAvailability{ "not_created" };
		std::atomic_uint64_t g_traversalSubmitted{ 0 }, g_traversalUnavailable{ 0 };
		std::atomic_uint64_t g_traversalWindow{ 1 }, g_traversalBatches{ 0 }, g_traversalNotReady{ 0 }, g_traversalFailed{ 0 }, g_traversalDiscarded{ 0 };
		std::atomic_uint64_t g_traversalObjects{ 0 }, g_traversalLoads{ 0 }, g_traversalRegions{ 0 }, g_traversalTriangles{ 0 };
		std::array<std::atomic_uint64_t, VRHybridCullingDiagnostics::Reasons.size()> g_traversalReasons{};
		std::atomic<const char*> g_status{ "idle" };
		std::atomic<const char*> g_backend{ "pending" }, g_fallbackReason{ "none" }, g_historyRejection{ "none" };
		std::atomic_uint64_t g_statusEpoch{ 0 };
		std::atomic_uint64_t g_historyEpoch{ 0 }, g_lastCountEpoch{ 0 };
		std::atomic_uint64_t g_submitted{ 0 }, g_accepted{ 0 }, g_invalidated{ 0 }, g_fallback{ 0 }, g_promoted{ 0 };
		std::atomic_uint64_t g_unreadable{ 0 };
		std::atomic_uint32_t g_lastCount{ 0 };
		std::atomic_uint32_t g_fallbackReasonFrame{ 0 };

		using VRDepthCullingTelemetry::Scope;
		VRDepthCullingTelemetry::TimingCounters g_prepareTiming, g_dispatchTiming, g_readbackTiming;
		VRDepthCullingTelemetry::SnapshotSlot<SourceSnapshot> g_sourceSnapshot;
		std::atomic_uint64_t g_submittedObjects{ 0 }, g_testedObjects{ 0 }, g_acceptedOccluded{ 0 }, g_acceptedVisible{ 0 };
		std::atomic_uint64_t g_pipelineBuildAttempts{ 0 }, g_pipelineBuilds{ 0 }, g_pipelineRecreations{ 0 };
		std::atomic_uint64_t g_pyramidAllocations{ 0 }, g_pyramidBuilds{ 0 }, g_pyramidDispatches{ 0 }, g_boundsDispatches{ 0 };
		std::atomic_uint64_t g_logicalBytesHighWater{ 0 }, g_droppedSourceSnapshots{ 0 };
		std::array<std::atomic_uint64_t, FallbackReasons.size()> g_fallbackReasons{};
		std::array<std::atomic_uint64_t, HistoryRejectionReasons.size()> g_historyReasons{};

		template <std::size_t Count>
		void RecordReason(const char* a_reason, const std::array<const char*, Count>& a_names, std::array<std::atomic_uint64_t, Count>& a_counts)
		{
			for (std::size_t index = 0; index < Count; ++index) {
				if (std::strcmp(a_reason, a_names[index]) == 0) {
					a_counts[index].fetch_add(1, std::memory_order_relaxed);
					return;
				}
			}
			assert(false && "Unregistered Hybrid telemetry reason");
		}

		std::uint64_t LogicalPyramidBytes(const PyramidLayout& a_layout)
		{
			std::uint64_t bytes = 0;
			for (std::uint32_t mip = 0; mip < a_layout.mipCount; ++mip)
				bytes += static_cast<std::uint64_t>(std::max(1u, a_layout.width >> mip)) * std::max(1u, a_layout.height >> mip) * kEyeCount * sizeof(float);
			return bytes;
		}

		void PublishSource(const Frame& a_frame, const char* a_stage)
		{
			SourceSnapshot snapshot{
				.available = true,
				.stage = a_stage,
				.cullingEpoch = a_frame.epoch,
				.source = a_frame.source,
				.eyes = a_frame.build.eyes,
				.pyramid = a_frame.test.pyramid,
				.logicalPyramidBytes = LogicalPyramidBytes(a_frame.test.pyramid),
			};
			static_assert(sizeof(snapshot.viewProjection) == sizeof(a_frame.test.viewProjection));
			static_assert(sizeof(snapshot.cameraAdjust) == sizeof(a_frame.test.cameraAdjust));
			std::memcpy(snapshot.viewProjection.data(), a_frame.test.viewProjection, sizeof(snapshot.viewProjection));
			std::memcpy(snapshot.cameraAdjust.data(), a_frame.test.cameraAdjust, sizeof(snapshot.cameraAdjust));
			for (std::size_t eye = 0; eye < kEyeCount; ++eye)
				std::memcpy(snapshot.unjitteredProjection[eye].data(), a_frame.poses[eye].projection, sizeof(a_frame.poses[eye].projection));
			VRDepthCullingTelemetryPolicy::UpdateMaximum(g_logicalBytesHighWater, snapshot.logicalPyramidBytes);
			if (!g_sourceSnapshot.Publish(snapshot))
				g_droppedSourceSnapshots.fetch_add(1, std::memory_order_relaxed);
		}

		void PublishOutcome(const char* a_state, const char* a_backend, std::uint64_t a_epoch)
		{
			g_status.store(a_state, std::memory_order_relaxed);
			g_backend.store(a_backend, std::memory_order_relaxed);
			g_statusEpoch.store(a_epoch, std::memory_order_release);
		}

		bool PreparationFailed(const char* a_reason, std::uint64_t a_epoch, bool a_telemetry)
		{
			g_fallbackReason.store(a_reason, std::memory_order_relaxed);
			g_fallbackReasonFrame.store(globals::state ? globals::state->frameCount : 0, std::memory_order_relaxed);
			if (a_telemetry) {
				g_sourceSnapshot.Invalidate();
			}
			HYBRID_OUTCOME("native_fallback", "native", a_epoch);
			return false;
		}
#endif

		bool CaptureFrame(Frame& a_frame, std::uint64_t a_epoch, VRHybridCullingSnapshot::Phase a_phase)
		{
			a_frame = {};
			auto* renderer = globals::game::renderer;
			auto* graphics = globals::game::graphicsState;
			auto* camera = RE::Main::WorldRootCamera();
			if (!renderer || !graphics || !camera || !globals::state ||
				globals::state->IsMainOrLoadingMenuOpen() || globals::state->IsSaveLoadSafeModeActive())
				return false;
			const auto publication = globals::state->GetCurrentMainRenderTargetResourcePublicationDiagnostics();
			if (!publication.current)
				return false;
			const auto& dynamic = graphics->GetRuntimeData();
			// Packed subrects require their own source contract; physical render-scale targets remain supported.
			if (!dynamic.dynamicResolutionLock &&
				(dynamic.dynamicResolutionWidthRatio != 1.0f || dynamic.dynamicResolutionHeightRatio != 1.0f))
				return false;
			auto* depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV;
			D3D11_TEXTURE2D_DESC texture{};
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			if (!depth || !Util::GetTexture2DDesc(depth, texture))
				return false;
			depth->GetDesc(&view);
			if (texture.ArraySize != 1 || texture.SampleDesc.Count != 1 || texture.Width % 2 != 0 ||
				view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || view.Texture2D.MostDetailedMip != 0 ||
				(view.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS && view.Format != DXGI_FORMAT_R32_FLOAT))
				return false;
			using FindCamera = void* (*)(RE::BSGraphics::State*, RE::NiCamera*, bool);
			static REL::Relocation<FindCamera> findCamera{ REL::Offset(0xDD1AD0) };
			const auto* cache = findCamera(graphics, camera, false);
			if (!cache || ReadField<std::uint32_t>(cache, 0x6C) != kEyeCount ||
				ReadField<std::uint32_t>(cache, 0x18) != kEyeCount || ReadField<std::uint32_t>(cache, 0x30) != kEyeCount)
				return false;
			const auto* cameras = ReadField<const RE::BSGraphics::ViewData*>(cache, 0x08);
			const auto* positions = ReadField<const RE::NiPoint3*>(cache, 0x20);
			if (!cameras || !positions)
				return false;
			for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
				const auto& data = cameras[eye];
				if (data.viewPort[0] != 0.0f || data.viewPort[1] != 1.0f || data.viewPort[2] != 1.0f || data.viewPort[3] != 0.0f ||
					data.viewDepthRange.x != 0.0f || data.viewDepthRange.y != 1.0f)
					return false;
				const auto matrix = data.viewProjMat.Transpose();
				std::memcpy(a_frame.test.viewProjection[eye], &matrix, sizeof(matrix));
				a_frame.test.cameraAdjust[eye][0] = positions[eye].x;
				a_frame.test.cameraAdjust[eye][1] = positions[eye].y;
				a_frame.test.cameraAdjust[eye][2] = positions[eye].z;
				std::memcpy(a_frame.poses[eye].position, &positions[eye], sizeof(RE::NiPoint3));
				std::memcpy(a_frame.poses[eye].projection, &data.projMatrixUnjittered, sizeof(Matrix));
				for (std::uint32_t row = 0; row < 3; ++row)
					for (std::uint32_t column = 0; column < 3; ++column)
						a_frame.poses[eye].rotation[row][column] = data.viewMat.m[row][column];
				a_frame.test.eyes[eye] = { eye * (texture.Width / 2), 0, texture.Width / 2, texture.Height };
			}
			if (!TryMakePreferredBuildConstants(a_frame.test.eyes, texture.Width, texture.Height,
					a_frame.build, a_frame.test.pyramid))
				return false;
			a_frame.test.objectCount = 1;
			a_frame.test.pixelGuardBand = 2.0f;
			if (!IsValidTestConstants(a_frame.test, texture.Width, texture.Height))
				return false;
			a_frame.depth.copy_from(depth);
			a_frame.source = {
				.view = reinterpret_cast<std::uintptr_t>(depth),
				.resourceGeneration = publication.publishedGeneration,
				.frame = globals::state->frameCount,
				.width = texture.Width,
				.height = texture.Height,
				.textureFormat = static_cast<std::uint32_t>(texture.Format),
				.viewFormat = static_cast<std::uint32_t>(view.Format),
				.sampleCount = texture.SampleDesc.Count,
				.phase = a_phase,
			};
			a_frame.epoch = a_epoch;
			a_frame.worldCamera = camera->world;
			return VRHybridCullingSnapshot::HasCurrentPublication(a_frame.source,
				globals::state->GetCompletedRenderTargetResourcePublicationGeneration(), true);
		}

		bool IsWorldCameraCoherent(const RE::NiTransform& a_previous, const RE::NiTransform& a_current)
		{
			EyePose previous{}, current{};
			std::memcpy(previous.rotation, a_previous.rotate.entry, sizeof(previous.rotation));
			std::memcpy(current.rotation, a_current.rotate.entry, sizeof(current.rotation));
			std::memcpy(previous.position, &a_previous.translate, sizeof(previous.position));
			std::memcpy(current.position, &a_current.translate, sizeof(current.position));
			return std::isfinite(a_previous.scale) && a_previous.scale > 0.0f && a_previous.scale == a_current.scale &&
			       VRHybridCullingHistory::IsCoherent(previous, current);
		}

		bool ReadBatch(void* a_culler, std::uint64_t a_epoch, Batch& a_batch)
		{
			a_batch = {};
			if (!a_culler || !globals::state)
				return false;
			const auto count = ReadField<std::uint32_t>(a_culler, 0xB0);
			const auto selector = ReadField<std::uint32_t>(a_culler, 0xC0);
			if (count == 0 || count > VRDepthCullingTemporalPolicy::kMaximumObjects || selector > 1)
				return false;
			a_batch = { reinterpret_cast<std::uintptr_t>(a_culler), ReadField<std::uintptr_t>(a_culler, 0xB8),
				ReadField<std::uintptr_t>(a_culler, 0xD0 + selector * sizeof(void*)), a_epoch,
				globals::state->frameCount, count, selector };
			return a_batch.transforms != 0 && a_batch.results != 0;
		}

		bool CheckNativeBuffers(void* a_culler, std::uint32_t a_count, NativeBuffer*& a_bounds, NativeBuffer*& a_results)
		{
			a_bounds = ReadField<NativeBuffer*>(a_culler, 0xF8);
			a_results = ReadField<NativeBuffer*>(a_culler, 0x100);
			if (ReadField<std::uint8_t>(a_culler, 0xC9) != 1 || !a_bounds || !a_results ||
				!a_bounds->buffer || !a_bounds->srv || !a_results->buffer || !a_results->uav || !a_results->staging ||
				a_bounds->count < a_count || a_results->count < a_count)
				return false;
			D3D11_BUFFER_DESC bounds{}, results{}, staging{};
			a_bounds->buffer->GetDesc(&bounds);
			a_results->buffer->GetDesc(&results);
			a_results->staging->GetDesc(&staging);
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			a_bounds->srv->GetDesc(&srv);
			a_results->uav->GetDesc(&uav);
			winrt::com_ptr<ID3D11Resource> boundsResource, resultsResource;
			a_bounds->srv->GetResource(boundsResource.put());
			a_results->uav->GetResource(resultsResource.put());
			if (boundsResource.get() != a_bounds->buffer || resultsResource.get() != a_results->buffer)
				return false;
			return bounds.StructureByteStride == sizeof(OBBTransform) && results.StructureByteStride == sizeof(std::uint32_t) &&
			       (bounds.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 && (results.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0 &&
			       (bounds.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0 &&
			       (results.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0 &&
			       bounds.ByteWidth >= a_count * sizeof(OBBTransform) && results.ByteWidth >= a_count * sizeof(std::uint32_t) &&
			       staging.ByteWidth == results.ByteWidth && staging.Usage == D3D11_USAGE_STAGING &&
			       (staging.CPUAccessFlags & D3D11_CPU_ACCESS_READ) != 0 &&
			       srv.ViewDimension == D3D11_SRV_DIMENSION_BUFFER && srv.Format == DXGI_FORMAT_UNKNOWN &&
			       srv.Buffer.FirstElement == 0 && srv.Buffer.NumElements >= a_count &&
			       uav.ViewDimension == D3D11_UAV_DIMENSION_BUFFER && uav.Format == DXGI_FORMAT_UNKNOWN &&
			       uav.Buffer.FirstElement == 0 && uav.Buffer.NumElements >= a_count && uav.Buffer.Flags == 0;
		}

#ifdef DEVBENCH_BRIDGE_ENABLED
		void CreateTraversalDiagnostics()
		{
			auto& resources = g_resources;
			g_traversalAvailable.store(false, std::memory_order_release);
			resources.diagnosticTest.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data\\Shaders\\VRHybridCulling\\TestBoundsCS.hlsl", { { "CSX_HIZ_DIAGNOSTICS", "1" } }, "cs_5_0")));
			if (!resources.diagnosticTest)
				throw std::runtime_error("Hybrid traversal diagnostic shader unavailable");
			winrt::com_ptr<ID3D11Device> shaderDevice;
			resources.diagnosticTest->GetDevice(shaderDevice.put());
			if (shaderDevice.get() != resources.device.get())
				throw std::runtime_error("Hybrid traversal diagnostic shader device changed");
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = VRDepthCullingTemporalPolicy::kMaximumObjects * sizeof(VRHybridCullingDiagnostics::Record);
			desc.StructureByteStride = sizeof(VRHybridCullingDiagnostics::Record);
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			DX::ThrowIfFailed(resources.device->CreateBuffer(&desc, nullptr, resources.diagnosticBuffer.put()));
			Util::SetResourceName(resources.diagnosticBuffer.get(), "VRHybridCulling::TraversalDiagnostics");
			DX::ThrowIfFailed(resources.device->CreateUnorderedAccessView(resources.diagnosticBuffer.get(), nullptr, resources.diagnosticUAV.put()));
			Util::SetResourceName(resources.diagnosticUAV.get(), "VRHybridCulling::TraversalDiagnostics UAV");
			desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			DX::ThrowIfFailed(resources.device->CreateBuffer(&desc, nullptr, resources.diagnosticStaging.put()));
			Util::SetResourceName(resources.diagnosticStaging.get(), "VRHybridCulling::TraversalReadback");
			g_traversalAvailability.store("ready", std::memory_order_release);
			g_traversalAvailable.store(true, std::memory_order_release);
		}

		bool HasCurrentTraversalDiagnostics()
		{
			return g_history.diagnosticStaging && g_history.diagnosticWindow == g_traversalWindow.load(std::memory_order_acquire);
		}

		void CollectTraversalDiagnostics(const Batch& a_batch)
		{
			if (!HasCurrentTraversalDiagnostics())
				return;
			auto* context = g_history.diagnosticContext.get();
			std::optional<VRHybridCullingDiagnostics::Totals> totals;
			const auto result = Util::TryReadbackWithRendererOwnership(context, g_history.diagnosticStaging.get(),
				context == globals::d3d::context ? Util::GetRendererContextLock(globals::game::renderer, context) : nullptr,
				[&](const D3D11_MAPPED_SUBRESOURCE& mapped) -> HRESULT {
					totals = VRHybridCullingDiagnostics::Summarize(
						{ static_cast<const VRHybridCullingDiagnostics::Record*>(mapped.pData), a_batch.count },
						{ reinterpret_cast<const std::uint32_t*>(a_batch.results), a_batch.count });
					return totals ? S_OK : E_INVALIDARG;
				});
			if (FAILED(result)) {
				(result == DXGI_ERROR_WAS_STILL_DRAWING ? g_traversalNotReady : g_traversalFailed).fetch_add(1, std::memory_order_relaxed);
				return;
			}
			g_traversalBatches.fetch_add(1, std::memory_order_relaxed);
			g_traversalObjects.fetch_add(totals->objects, std::memory_order_relaxed);
			g_traversalLoads.fetch_add(totals->depthLoads, std::memory_order_relaxed);
			g_traversalRegions.fetch_add(totals->faceRegions, std::memory_order_relaxed);
			g_traversalTriangles.fetch_add(totals->faceTriangles, std::memory_order_relaxed);
			for (std::size_t index = 0; index < totals->eyeReasons.size(); ++index)
				g_traversalReasons[index].fetch_add(totals->eyeReasons[index], std::memory_order_relaxed);
		}
#endif

		bool EnsurePipeline()
		{
			auto& resources = g_resources;
			if (resources.failed)
				return false;
			if (resources.test)
				return true;
#ifdef DEVBENCH_BRIDGE_ENABLED
			const VRDepthCullingTelemetryPolicy::WriterScope telemetry(VRDepthCullingTemporal::GetTelemetryGate());
			if (telemetry)
				g_pipelineBuildAttempts.fetch_add(1, std::memory_order_relaxed);
#endif
			auto* device = resources.device.get();
			winrt::com_ptr<ID3D11Device1> device1;
			DX::ThrowIfFailed(device->QueryInterface(__uuidof(ID3D11Device1), device1.put_void()));
			DX::ThrowIfFailed(resources.ownerContext->QueryInterface(__uuidof(ID3D11DeviceContext1), resources.context.put_void()));
			const auto featureLevel = device->GetFeatureLevel();
			DX::ThrowIfFailed(device1->CreateDeviceContextState(device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED,
				&featureLevel, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr, resources.isolatedState.put()));
			Util::SetResourceName(resources.isolatedState.get(), "VRHybridCulling::ContextState");
			resources.build.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VRHybridCulling\\BuildDepthCS.hlsl", {}, "cs_5_0")));
			resources.reduce.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VRHybridCulling\\ReduceDepthCS.hlsl", {}, "cs_5_0")));
			resources.test.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VRHybridCulling\\TestBoundsCS.hlsl", {}, "cs_5_0")));
			if (!resources.build || !resources.reduce || !resources.test) {
				resources.failed = true;
				logger::warn("VR: Hybrid Hi-Z shaders unavailable; using native depth culling");
				return false;
			}
			for (auto* shader : { resources.build.get(), resources.reduce.get(), resources.test.get() }) {
				winrt::com_ptr<ID3D11Device> shaderDevice;
				shader->GetDevice(shaderDevice.put());
				if (shaderDevice.get() != device) {
					resources.failed = true;
					logger::warn("VR: Hybrid Hi-Z shader device changed during setup; using native depth culling");
					return false;
				}
			}
			D3D11_BUFFER_DESC description{};
			description.ByteWidth = sizeof(TestConstants);
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			DX::ThrowIfFailed(device->CreateBuffer(&description, nullptr, resources.constants.put()));
			Util::SetResourceName(resources.constants.get(), "VRHybridCulling::Constants");
#ifdef DEVBENCH_BRIDGE_ENABLED
			try {
				CreateTraversalDiagnostics();
			} catch (const std::exception& error) {
				resources.diagnosticTest = nullptr;
				resources.diagnosticBuffer = nullptr;
				resources.diagnosticStaging = nullptr;
				resources.diagnosticUAV = nullptr;
				g_traversalAvailability.store("setup_failed", std::memory_order_release);
				logger::warn("VR: Hybrid traversal diagnostics unavailable; normal culling retained: {}", error.what());
			}
			if (telemetry)
				g_pipelineBuilds.fetch_add(1, std::memory_order_relaxed);
#endif
			return true;
		}

		void EnsurePyramid(const PyramidLayout& a_layout)
		{
			auto& resources = g_resources;
			if (resources.pyramid && resources.layout.width == a_layout.width && resources.layout.height == a_layout.height)
				return;
#ifdef DEVBENCH_BRIDGE_ENABLED
			const VRDepthCullingTelemetryPolicy::WriterScope telemetry(VRDepthCullingTemporal::GetTelemetryGate());
#endif
			auto* device = resources.device.get();
			winrt::com_ptr<ID3D11Texture2D> pyramid;
			winrt::com_ptr<ID3D11ShaderResourceView> allMips;
			std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> srvs(a_layout.mipCount);
			std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> uavs(a_layout.mipCount);
			D3D11_TEXTURE2D_DESC texture{};
			texture.Width = a_layout.width;
			texture.Height = a_layout.height;
			texture.MipLevels = a_layout.mipCount;
			texture.ArraySize = kEyeCount;
			texture.Format = DXGI_FORMAT_R32_FLOAT;
			texture.SampleDesc.Count = 1;
			texture.Usage = D3D11_USAGE_DEFAULT;
			texture.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			DX::ThrowIfFailed(device->CreateTexture2D(&texture, nullptr, pyramid.put()));
			Util::SetResourceName(pyramid.get(), "VRHybridCulling::DepthPyramid");
			DX::ThrowIfFailed(device->CreateShaderResourceView(pyramid.get(), nullptr, allMips.put()));
			Util::SetResourceName(allMips.get(), "VRHybridCulling::DepthPyramid SRV");
			for (std::uint32_t mip = 0; mip < a_layout.mipCount; ++mip) {
				D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
				srv.Format = texture.Format;
				srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				srv.Texture2DArray.MostDetailedMip = mip;
				srv.Texture2DArray.MipLevels = 1;
				srv.Texture2DArray.ArraySize = kEyeCount;
				DX::ThrowIfFailed(device->CreateShaderResourceView(pyramid.get(), &srv, srvs[mip].put()));
				Util::SetResourceName(srvs[mip].get(), "VRHybridCulling::Mip%u SRV", mip);
				D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
				uav.Format = texture.Format;
				uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
				uav.Texture2DArray.MipSlice = mip;
				uav.Texture2DArray.ArraySize = kEyeCount;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(pyramid.get(), &uav, uavs[mip].put()));
				Util::SetResourceName(uavs[mip].get(), "VRHybridCulling::Mip%u UAV", mip);
			}
			resources.pyramid = std::move(pyramid);
			resources.allMips = std::move(allMips);
			resources.mipSRVs = std::move(srvs);
			resources.mipUAVs = std::move(uavs);
			resources.layout = a_layout;
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (telemetry) {
				g_pyramidAllocations.fetch_add(1, std::memory_order_relaxed);
				VRDepthCullingTelemetryPolicy::UpdateMaximum(g_logicalBytesHighWater, LogicalPyramidBytes(a_layout));
			}
#endif
		}

		template <class T>
		void UploadConstants(const T& a_value)
		{
			static_assert(sizeof(T) <= sizeof(TestConstants));
			std::array<std::byte, sizeof(TestConstants)> data{};
			std::memcpy(data.data(), &a_value, sizeof(T));
			auto* context = g_resources.context.get();
			context->UpdateSubresource(g_resources.constants.get(), 0, nullptr, data.data(), 0, 0);
			auto* constants = g_resources.constants.get();
			context->CSSetConstantBuffers(0, 1, &constants);
		}

		void UnbindCompute()
		{
			ID3D11ShaderResourceView* srvs[2]{};
			ID3D11UnorderedAccessView* uavs[] = { nullptr
#ifdef DEVBENCH_BRIDGE_ENABLED
				,
				nullptr
#endif
			};
			g_resources.context->CSSetShaderResources(0, 2, srvs);
			g_resources.context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(uavs)), uavs, nullptr);
		}

		void BuildPyramid()
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			const VRDepthCullingTelemetryPolicy::WriterScope telemetry(VRDepthCullingTemporal::GetTelemetryGate());
			HYBRID_GPU_PASS("VRHybridCulling::BuildHierarchy", telemetry);
#endif
			auto* context = g_resources.context.get();
			{
#ifdef DEVBENCH_BRIDGE_ENABLED
				HYBRID_GPU_PASS("VRHybridCulling::BuildBase", telemetry);
#endif
				UploadConstants(g_prepared.build);
				auto* source = g_prepared.depth.get();
				auto* destination = g_resources.mipUAVs[0].get();
				context->CSSetShader(g_resources.build.get(), nullptr, 0);
				context->CSSetShaderResources(0, 1, &source);
				context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
				context->Dispatch((g_prepared.build.outputWidth + 7) / 8, (g_prepared.build.outputHeight + 7) / 8, kEyeCount);
				UnbindCompute();
			}
#ifdef DEVBENCH_BRIDGE_ENABLED
			HYBRID_GPU_PASS("VRHybridCulling::ReduceMips", telemetry);
#endif
			context->CSSetShader(g_resources.reduce.get(), nullptr, 0);
			for (std::uint32_t mip = 1; mip < g_resources.layout.mipCount; ++mip) {
				const ReduceConstants constants{ std::max(1u, g_resources.layout.width >> mip), std::max(1u, g_resources.layout.height >> mip), {} };
				UploadConstants(constants);
				auto* source = g_resources.mipSRVs[mip - 1].get();
				auto* destination = g_resources.mipUAVs[mip].get();
				context->CSSetShaderResources(0, 1, &source);
				context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
				context->Dispatch((constants.outputWidth + 7) / 8, (constants.outputHeight + 7) / 8, kEyeCount);
				UnbindCompute();
			}
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (telemetry) {
				g_pyramidBuilds.fetch_add(1, std::memory_order_relaxed);
				g_pyramidDispatches.fetch_add(g_resources.layout.mipCount, std::memory_order_relaxed);
			}
#endif
		}
	}

	bool Prepare(std::uint64_t a_epoch)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		const Scope telemetry(g_prepareTiming, VRDepthCullingTemporal::GetTelemetryGate());
		g_fallbackReason.store("none", std::memory_order_relaxed);
#endif
		g_ready = false;
		g_prepared.depth = nullptr;
		if (!REL::Module::IsVR() || !globals::d3d::device || !globals::d3d::context)
			return HYBRID_PREPARATION_FAILED("unsupported_frame", a_epoch);
		if (!globals::state || globals::state->GetCompletedRenderTargetResourcePublicationGeneration() == 0)
			return HYBRID_PREPARATION_FAILED("resource_publication_unavailable", a_epoch);
		const VRHybridCullingSnapshot::PipelineOwner currentOwner{
			reinterpret_cast<std::uintptr_t>(globals::d3d::device), reinterpret_cast<std::uintptr_t>(globals::d3d::context)
		};
		if (VRHybridCullingSnapshot::ShouldRecreatePipeline(g_resources.Owner(), currentOwner,
				g_reloadRequested.exchange(false, std::memory_order_acq_rel))) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (telemetry && g_resources.device)
				g_pipelineRecreations.fetch_add(1, std::memory_order_relaxed);
#endif
			g_history.pipelineInvalidated = g_history.pending;
			g_resources = {};
#ifdef DEVBENCH_BRIDGE_ENABLED
			g_traversalAvailable.store(false, std::memory_order_release);
			g_traversalAvailability.store("not_created", std::memory_order_release);
#endif
			g_resources.device.copy_from(globals::d3d::device);
			g_resources.ownerContext.copy_from(globals::d3d::context);
		}
		if (g_resources.failed)
			return HYBRID_PREPARATION_FAILED("pipeline_unavailable", a_epoch);
		if (!CaptureFrame(g_prepared, a_epoch, VRHybridCullingSnapshot::Phase::NativeDownscale))
			return HYBRID_PREPARATION_FAILED("unsupported_frame", a_epoch);
		try {
			if (!EnsurePipeline())
				return HYBRID_PREPARATION_FAILED("pipeline_unavailable", a_epoch);
			EnsurePyramid(g_prepared.test.pyramid);
			g_ready = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (telemetry)
				PublishSource(g_prepared, "prepared");
#endif
			HYBRID_OUTCOME("hybrid_prepared", "pending", a_epoch);
			return true;
		} catch (const std::exception& error) {
			g_resources.failed = true;
			logger::warn("VR: Hybrid Hi-Z resource setup failed: {}", error.what());
			return HYBRID_PREPARATION_FAILED("resource_setup_failed", a_epoch);
		}
	}

	bool Dispatch(void* a_culler, std::uint64_t a_epoch)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		const Scope telemetry(g_dispatchTiming, VRDepthCullingTemporal::GetTelemetryGate());
#endif
		Batch batch;
		NativeBuffer* bounds = nullptr;
		NativeBuffer* results = nullptr;
		if (!g_ready)
			return HYBRID_PREPARATION_FAILED("not_prepared", a_epoch);
		if (!ReadBatch(a_culler, a_epoch, batch))
			return HYBRID_PREPARATION_FAILED("empty_or_invalid_batch", a_epoch);
		if (g_prepared.epoch != a_epoch || g_prepared.source.frame != batch.frame)
			return HYBRID_PREPARATION_FAILED("preparation_expired", a_epoch);
		if (!CheckNativeBuffers(a_culler, batch.count, bounds, results))
			return HYBRID_PREPARATION_FAILED("native_buffers_invalid", a_epoch);
		g_ready = false;
		g_prepared.test.objectCount = batch.count;
		const Util::RendererOwnership ownership(Util::GetRendererContextLock(globals::game::renderer, globals::d3d::context), true);
		if (!ownership)
			return HYBRID_PREPARATION_FAILED("renderer_unavailable", a_epoch);
		const auto publication = globals::state->GetCurrentMainRenderTargetResourcePublicationDiagnostics();
		if (!VRHybridCullingSnapshot::HasCurrentPublication(g_prepared.source, publication.publishedGeneration, publication.current))
			return HYBRID_PREPARATION_FAILED("resource_publication_changed", a_epoch);
		if (!VRHybridCullingSnapshot::MatchesPipelineOwner(g_resources.Owner(),
				{ reinterpret_cast<std::uintptr_t>(globals::d3d::device), reinterpret_cast<std::uintptr_t>(globals::d3d::context) }))
			return HYBRID_PREPARATION_FAILED("pipeline_owner_changed", a_epoch);
		const auto* depth = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV;
		if (!VRHybridCullingSnapshot::CanDispatch(g_prepared.source, publication.publishedGeneration, publication.current,
				globals::state->frameCount, reinterpret_cast<std::uintptr_t>(depth)))
			return HYBRID_PREPARATION_FAILED("prepared_source_changed", a_epoch);
#ifdef DEVBENCH_BRIDGE_ENABLED
		HYBRID_GPU_PASS("VRHybridCulling::Visibility", telemetry);
#endif
		auto* context = g_resources.context.get();
		winrt::com_ptr<ID3DDeviceContextState> previous;
		context->SwapDeviceContextState(g_resources.isolatedState.get(), previous.put());
		const SKSE::stl::scope_exit restore([&]() noexcept {
			UnbindCompute();
			context->SwapDeviceContextState(previous.get(), nullptr);
		});
		BuildPyramid();
#ifdef DEVBENCH_BRIDGE_ENABLED
		const bool diagnosticRequested = telemetry && g_traversalEnabled.load(std::memory_order_acquire);
		const bool diagnostic = diagnosticRequested && g_traversalAvailable.load(std::memory_order_acquire);
		if (diagnosticRequested && !diagnostic)
			g_traversalUnavailable.fetch_add(1, std::memory_order_relaxed);
		if (telemetry && HasCurrentTraversalDiagnostics())
			g_traversalDiscarded.fetch_add(1, std::memory_order_relaxed);
		g_history.diagnosticStaging = nullptr;
		g_history.diagnosticContext = nullptr;
		g_history.diagnosticWindow = g_traversalWindow.load(std::memory_order_acquire);
#endif
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			HYBRID_GPU_PASS("VRHybridCulling::TestBounds", telemetry);
#endif
			UploadConstants(g_prepared.test);
			ID3D11ShaderResourceView* srvs[]{ bounds->srv, g_resources.allMips.get() };
			context->CSSetShaderResources(0, 2, srvs);
			context->CSSetUnorderedAccessViews(0, 1, &results->uav, nullptr);
			auto* testShader = g_resources.test.get();
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (diagnostic) {
				auto* diagnosticUAV = g_resources.diagnosticUAV.get();
				context->CSSetUnorderedAccessViews(1, 1, &diagnosticUAV, nullptr);
				testShader = g_resources.diagnosticTest.get();
			}
#endif
			context->CSSetShader(testShader, nullptr, 0);
			context->Dispatch((batch.count + 63) / 64, 1, 1);
			UnbindCompute();
		}
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			HYBRID_GPU_PASS("VRHybridCulling::CopyResults", telemetry);
			if (diagnostic) {
				context->CopyResource(g_resources.diagnosticStaging.get(), g_resources.diagnosticBuffer.get());
				g_history.diagnosticStaging = g_resources.diagnosticStaging;
				g_history.diagnosticContext = g_resources.ownerContext;
				g_traversalSubmitted.fetch_add(1, std::memory_order_relaxed);
			}
#endif
			context->CopyResource(results->staging, results->buffer);
		}
		g_history.batch = batch;
		g_history.frame = g_prepared;
		g_prepared.depth = nullptr;
		std::memcpy(g_history.bounds.data(), reinterpret_cast<const void*>(batch.transforms), batch.count * sizeof(OBBTransform));
		g_history.pending = true;
		g_history.pipelineInvalidated = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (telemetry) {
			g_lastCount.store(batch.count, std::memory_order_relaxed);
			g_lastCountEpoch.store(a_epoch, std::memory_order_release);
			g_submitted.fetch_add(1, std::memory_order_relaxed);
			g_submittedObjects.fetch_add(batch.count, std::memory_order_relaxed);
			g_boundsDispatches.fetch_add(1, std::memory_order_relaxed);
			PublishSource(g_history.frame, "submitted");
		}
		g_fallbackReason.store("none", std::memory_order_relaxed);
#endif
		HYBRID_OUTCOME("hybrid_submitted", "hybrid", a_epoch);
		return true;
	}

	bool CompleteReadback(void* a_culler, std::uint64_t a_epoch, bool a_selected)
	{
		if (!g_history.pending)
			return false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		const Scope telemetry(g_readbackTiming, VRDepthCullingTemporal::GetTelemetryGate());
		const SKSE::stl::scope_exit releaseDiagnostic([&]() noexcept {
			g_history.diagnosticStaging = nullptr;
			g_history.diagnosticContext = nullptr;
		});
#endif
		Batch batch;
		if (!ReadBatch(a_culler, a_epoch, batch)) {
			g_history.pending = false;
			g_history.frame.depth = nullptr;
#ifdef DEVBENCH_BRIDGE_ENABLED
			g_historyRejection.store("unreadable_batch", std::memory_order_relaxed);
			g_historyEpoch.store(a_epoch, std::memory_order_release);
			if (telemetry) {
				g_unreadable.fetch_add(1, std::memory_order_relaxed);
				if (HasCurrentTraversalDiagnostics())
					g_traversalDiscarded.fetch_add(1, std::memory_order_relaxed);
				RecordReason("unreadable_batch", HistoryRejectionReasons, g_historyReasons);
				g_sourceSnapshot.Invalidate();
			}
#endif
			HYBRID_OUTCOME("history_unreadable", "hybrid", g_history.batch.epoch);
			return true;
		}
		Frame current;
		bool rejected = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		const char* rejection = "none";
#endif
		if (!a_selected || !VRHybridCullingHistory::Matches(g_history.batch, batch))
			HYBRID_REJECT_HISTORY("batch_mismatch");
		else if (g_history.pipelineInvalidated || g_reloadRequested.load(std::memory_order_acquire))
			HYBRID_REJECT_HISTORY("pipeline_changed");
		else if (std::memcmp(g_history.bounds.data(), reinterpret_cast<const void*>(batch.transforms), batch.count * sizeof(OBBTransform)) != 0)
			HYBRID_REJECT_HISTORY("bounds_changed");
		else if (!VRHybridCullingSnapshot::HasCurrentPublication(g_history.frame.source,
					 globals::state->GetCompletedRenderTargetResourcePublicationGeneration(), true))
			HYBRID_REJECT_HISTORY("resource_publication_changed");
		else if (!CaptureFrame(current, a_epoch, VRHybridCullingSnapshot::Phase::NativeReadback))
			HYBRID_REJECT_HISTORY("frame_unavailable");
		else if (!VRHybridCullingSnapshot::MatchesReadback(g_history.frame.source, current.source) ||
				 std::memcmp(current.build.eyes.data(), g_history.frame.build.eyes.data(), sizeof(current.build.eyes)) != 0)
			HYBRID_REJECT_HISTORY("depth_changed");
		else if (!IsWorldCameraCoherent(g_history.frame.worldCamera, current.worldCamera) ||
				 !VRHybridCullingHistory::IsStereoCoherent(g_history.frame.poses, current.poses))
			HYBRID_REJECT_HISTORY("view_changed");
		g_history.pending = false;
		g_history.frame.depth = nullptr;
#ifdef DEVBENCH_BRIDGE_ENABLED
		g_historyRejection.store(rejection, std::memory_order_relaxed);
		g_historyEpoch.store(a_epoch, std::memory_order_release);
		if (telemetry)
			g_testedObjects.fetch_add(batch.count, std::memory_order_relaxed);
#endif
		if (!rejected) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (telemetry) {
				CollectTraversalDiagnostics(batch);
				g_accepted.fetch_add(1, std::memory_order_relaxed);
				const auto* results = reinterpret_cast<const std::uint32_t*>(batch.results);
				const auto occluded = std::count(results, results + batch.count, 0u);
				g_acceptedOccluded.fetch_add(occluded, std::memory_order_relaxed);
				g_acceptedVisible.fetch_add(batch.count - occluded, std::memory_order_relaxed);
				PublishSource(current, "readback");
			}
#endif
			HYBRID_OUTCOME("history_accepted", "hybrid", g_history.batch.epoch);
			return true;
		}
		auto* results = reinterpret_cast<std::uint32_t*>(batch.results);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (telemetry) {
			if (HasCurrentTraversalDiagnostics())
				g_traversalDiscarded.fetch_add(1, std::memory_order_relaxed);
			g_invalidated.fetch_add(1, std::memory_order_relaxed);
			g_promoted.fetch_add(std::count(results, results + batch.count, 0u), std::memory_order_relaxed);
			RecordReason(rejection, HistoryRejectionReasons, g_historyReasons);
			if (current.source.view != 0)
				PublishSource(current, "readback_rejected");
			else
				g_sourceSnapshot.Invalidate();
		}
#endif
		std::fill_n(results, batch.count, 1u);
		HYBRID_OUTCOME("history_invalid_visible", "hybrid", g_history.batch.epoch);
		return true;
	}

	void CancelPreparation(bool a_hybridSelected, [[maybe_unused]] std::uint64_t a_epoch)
	{
		g_ready = false;
		g_prepared.depth = nullptr;
		g_history.pending = false;
		g_history.frame.depth = nullptr;
#ifdef DEVBENCH_BRIDGE_ENABLED
		{
			const VRDepthCullingTelemetryPolicy::WriterScope telemetry(VRDepthCullingTemporal::GetTelemetryGate());
			if (telemetry && HasCurrentTraversalDiagnostics())
				g_traversalDiscarded.fetch_add(1, std::memory_order_relaxed);
			g_history.diagnosticStaging = nullptr;
			g_history.diagnosticContext = nullptr;
		}
#endif
		if (a_hybridSelected) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			const VRDepthCullingTelemetryPolicy::WriterScope telemetry(VRDepthCullingTemporal::GetTelemetryGate());
			if (g_statusEpoch.load(std::memory_order_acquire) != a_epoch ||
				!globals::state || g_fallbackReasonFrame.load(std::memory_order_relaxed) != globals::state->frameCount ||
				std::strcmp(g_fallbackReason.load(std::memory_order_relaxed), "none") == 0) {
				g_fallbackReason.store("replacement_not_prepared", std::memory_order_relaxed);
			}
			if (telemetry) {
				g_fallback.fetch_add(1, std::memory_order_relaxed);
				RecordReason(g_fallbackReason.load(std::memory_order_relaxed), FallbackReasons, g_fallbackReasons);
				g_sourceSnapshot.Invalidate();
			}
#endif
			HYBRID_OUTCOME("native_fallback", "native", a_epoch);
		}
	}

	void ClearShaderCache()
	{
		g_reloadRequested.store(true, std::memory_order_release);
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	void SetTraversalDiagnosticsEnabled(bool a_enabled) noexcept
	{
		if (g_traversalEnabled.exchange(a_enabled, std::memory_order_acq_rel) != a_enabled)
			g_traversalWindow.fetch_add(1, std::memory_order_acq_rel);
	}

	Status GetStatus(std::uint64_t a_epoch, bool a_selected, bool a_enabled, bool a_installed)
	{
		const bool current = a_selected && a_enabled && a_installed && g_statusEpoch.load(std::memory_order_acquire) == a_epoch;
		Status result{
			.state = current ? g_status.load(std::memory_order_relaxed) : (a_selected && a_enabled ? (a_installed ? "pending" : "native_fallback") : "inactive"),
			.effectiveBackend = !a_enabled ? "disabled" : (!a_selected || !a_installed ? "native" : (current ? g_backend.load(std::memory_order_relaxed) : "pending")),
			.fallbackReason = a_selected && a_enabled && !a_installed ? "hooks_unavailable" : (current ? g_fallbackReason.load(std::memory_order_relaxed) : "none"),
			.historyRejectionReason = current && g_historyEpoch.load(std::memory_order_acquire) == a_epoch ? g_historyRejection.load(std::memory_order_relaxed) : "none",
			.submittedBatches = g_submitted.load(std::memory_order_relaxed),
			.acceptedBatches = g_accepted.load(std::memory_order_relaxed),
			.invalidatedBatches = g_invalidated.load(std::memory_order_relaxed),
			.fallbackBatches = g_fallback.load(std::memory_order_relaxed),
			.promotedObjects = g_promoted.load(std::memory_order_relaxed),
			.unreadableBatches = g_unreadable.load(std::memory_order_relaxed),
			.submittedObjects = g_submittedObjects.load(std::memory_order_relaxed),
			.testedObjects = g_testedObjects.load(std::memory_order_relaxed),
			.acceptedOccludedObjects = g_acceptedOccluded.load(std::memory_order_relaxed),
			.acceptedVisibleObjects = g_acceptedVisible.load(std::memory_order_relaxed),
			.pipelineBuildAttempts = g_pipelineBuildAttempts.load(std::memory_order_relaxed),
			.pipelineBuilds = g_pipelineBuilds.load(std::memory_order_relaxed),
			.pipelineRecreations = g_pipelineRecreations.load(std::memory_order_relaxed),
			.pyramidAllocations = g_pyramidAllocations.load(std::memory_order_relaxed),
			.pyramidBuilds = g_pyramidBuilds.load(std::memory_order_relaxed),
			.pyramidDispatches = g_pyramidDispatches.load(std::memory_order_relaxed),
			.boundsDispatches = g_boundsDispatches.load(std::memory_order_relaxed),
			.logicalPyramidBytesHighWater = g_logicalBytesHighWater.load(std::memory_order_relaxed),
			.droppedSourceSnapshots = g_droppedSourceSnapshots.load(std::memory_order_relaxed),
			.lastObjectCount = current && g_lastCountEpoch.load(std::memory_order_acquire) == a_epoch ? g_lastCount.load(std::memory_order_relaxed) : 0,
			.prepare = g_prepareTiming.Read(),
			.dispatch = g_dispatchTiming.Read(),
			.readback = g_readbackTiming.Read()
		};
		result.traversalDiagnosticsEnabled = g_traversalEnabled.load(std::memory_order_acquire);
		result.traversalDiagnosticsAvailable = g_traversalAvailable.load(std::memory_order_acquire);
		result.traversalDiagnosticsAvailability = g_traversalAvailability.load(std::memory_order_acquire);
		result.traversalSubmittedBatches = g_traversalSubmitted.load(std::memory_order_relaxed);
		result.traversalUnavailableBatches = g_traversalUnavailable.load(std::memory_order_relaxed);
		result.traversalBatches = g_traversalBatches.load(std::memory_order_relaxed);
		result.traversalNotReadyBatches = g_traversalNotReady.load(std::memory_order_relaxed);
		result.traversalFailedBatches = g_traversalFailed.load(std::memory_order_relaxed);
		result.traversalDiscardedBatches = g_traversalDiscarded.load(std::memory_order_relaxed);
		result.traversal.objects = g_traversalObjects.load(std::memory_order_relaxed);
		result.traversal.depthLoads = g_traversalLoads.load(std::memory_order_relaxed);
		result.traversal.faceRegions = g_traversalRegions.load(std::memory_order_relaxed);
		result.traversal.faceTriangles = g_traversalTriangles.load(std::memory_order_relaxed);
		for (std::size_t index = 0; index < result.traversal.eyeReasons.size(); ++index)
			result.traversal.eyeReasons[index] = g_traversalReasons[index].load(std::memory_order_relaxed);
		for (std::size_t index = 0; index < FallbackReasons.size(); ++index)
			result.fallbackReasonCounts[index] = g_fallbackReasons[index].load(std::memory_order_relaxed);
		for (std::size_t index = 0; index < HistoryRejectionReasons.size(); ++index)
			result.historyRejectionReasonCounts[index] = g_historyReasons[index].load(std::memory_order_relaxed);
		if (!VRDepthCullingTemporal::GetTelemetryGate().IsEnabled()) {
			result.snapshot.validity = "telemetry_disabled";
		} else {
			bool busy = false;
			if (!g_sourceSnapshot.Read(result.snapshot, &busy)) {
				result.snapshot = {};
				result.snapshot.busy = busy;
				result.snapshot.validity = busy ? "snapshot_busy" : "snapshot_unavailable_or_superseded";
			} else if (std::strcmp(result.snapshot.stage, "readback_rejected") == 0) {
				result.snapshot.validity = "history_rejected";
			} else if (!current || (std::strcmp(result.state, "hybrid_prepared") != 0 &&
									   std::strcmp(result.state, "hybrid_submitted") != 0 && std::strcmp(result.state, "history_accepted") != 0)) {
				result.snapshot.validity = "inactive_backend";
			} else if (result.snapshot.cullingEpoch != a_epoch) {
				result.snapshot.validity = "epoch_changed";
			} else if (!globals::state || result.snapshot.source.frame != globals::state->frameCountAtomic.load(std::memory_order_relaxed)) {
				result.snapshot.validity = "older_frame";
			} else if (result.snapshot.source.resourceGeneration != globals::state->GetCompletedRenderTargetResourcePublicationGeneration()) {
				result.snapshot.validity = "resource_publication_changed";
			} else {
				result.snapshot.valid = result.snapshot.current = true;
				result.snapshot.validity = "current_observation";
			}
		}
		return result;
	}

	void ResetTelemetryUnderLock() noexcept
	{
		g_traversalWindow.fetch_add(1, std::memory_order_acq_rel);
		for (auto* counter : { &g_traversalSubmitted, &g_traversalUnavailable, &g_traversalBatches, &g_traversalNotReady, &g_traversalFailed, &g_traversalDiscarded,
				 &g_traversalObjects, &g_traversalLoads, &g_traversalRegions, &g_traversalTriangles })
			counter->store(0, std::memory_order_relaxed);
		for (auto& count : g_traversalReasons)
			count.store(0, std::memory_order_relaxed);
		g_submitted.store(0, std::memory_order_relaxed);
		g_accepted.store(0, std::memory_order_relaxed);
		g_invalidated.store(0, std::memory_order_relaxed);
		g_fallback.store(0, std::memory_order_relaxed);
		g_promoted.store(0, std::memory_order_relaxed);
		g_unreadable.store(0, std::memory_order_relaxed);
		for (auto* counter : { &g_submittedObjects, &g_testedObjects, &g_acceptedOccluded, &g_acceptedVisible,
				 &g_pipelineBuildAttempts, &g_pipelineBuilds, &g_pipelineRecreations, &g_pyramidAllocations,
				 &g_pyramidBuilds, &g_pyramidDispatches, &g_boundsDispatches, &g_logicalBytesHighWater, &g_droppedSourceSnapshots })
			counter->store(0, std::memory_order_relaxed);
		for (auto& count : g_fallbackReasons)
			count.store(0, std::memory_order_relaxed);
		for (auto& count : g_historyReasons)
			count.store(0, std::memory_order_relaxed);
		g_sourceSnapshot.Invalidate();
		g_lastCount.store(0, std::memory_order_relaxed);
		g_lastCountEpoch.store(0, std::memory_order_release);
		g_prepareTiming.Reset();
		g_dispatchTiming.Reset();
		g_readbackTiming.Reset();
	}
#endif
}
